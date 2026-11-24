#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace iron;
using namespace iron::rpg;
namespace {
void input(Session &s,const Input &in,float dt=.02f){s.command(1,{{"op","input"},{"epoch",s.epoch},{"input",coop::inputWire(in)}});s.step(dt);}
Session fixture(){
  static const Session original=[] {
    Session s(713);s.add(1,"Atlas");s.add(2,"Beacon");for(auto &m:s.members)finishCreation(m.sheet);
    GameSettings options;options.maps=1;options.enemy=3;options.enemyCount=1;options.objective=0;options.opener=5;options.environment=3;options.location=0;
    s.command(1,{{"op","settings"},{"settings",options}});
    s.command(1,{{"op","ready"},{"value",true}});s.command(2,{{"op","ready"},{"value",true}});s.command(1,{{"op","deploy"}});
    s.enemies[0].disabled=10000;s.enemies[0].hp=s.enemies[0].maxHp=1000;
    s.world.proceduralTerrain=false;s.world.editGeometry().terrain.clear();s.world.indexTerrain();s.world.interiorRegions.clear();s.world.waterRegions.clear();
    for(auto &t:s.world.targets)t.active=false;
    s.member(1).body.position={};s.member(2).body.position={20,0,0};
    for(auto &m:s.members){m.sheet.equipment.fill(Gear::Empty);m.sheet.qualities.fill(Quality::None);m.body.cooldown.fill(0);m.held.fill(false);}
    s.mission.recipe.countdownSeconds=0;s.mission.recipe.planetoidHazard=-1;s.mission.recipe.complication=-1;
    s.mission.recipe.opener=5;s.mission.openerTime=100;
    input(s,Input{});return s;
  }();return original;
}
void fit(Session &s,Gear gear){
  auto &m=s.member(1);m.sheet.equipment.fill(Gear::Empty);m.sheet.equipment[LeftHip]=gear;
  m.sheet.level=3;m.sheet.modules={Module::GeistSystem};
  if(gearInfo(gear).module!=Module::Count)m.sheet.modules.push_back(gearInfo(gear).module);
  m.sheet.attributes[3]=32;m.sheet.attributes[2]=32;m.sheet.overheated=false;m.sheet.hp=m.sheet.maxHp;
  m.sheet.owned[size_t(gear)]=1;m.sheet.inventory[size_t(gear)]=3;m.body.cooldown.fill(0);m.held.fill(false);m.droneControl=false;
}
void press(Session &s,Gear gear){fit(s,gear);Input in;in.fire[LeftHip]=true;input(s,in);}
void target(Session &s,Vec3 position){auto &t=s.world.targets[0];t.active=true;t.origin=position;t.health=1000;s.world.updateEnemyBounds(t);}
}
int main(){
  setOriginalAssetRoot(MECHA_ASSETS);
  // Every mounted manual module performs its own authoritative action.
  {
    auto s=fixture();target(s,{0,0,-20});press(s,Gear::FunnelDrones);assert(s.member(1).funnels);
    int hp=s.enemies[0].hp;for(int i=0;i<30;++i)input(s,Input{});
    assert(s.enemies[0].hp<hp);assert(s.member(1).funnels);
    s.member(1).body.cooldown[LeftHip]=0;Input held;held.fire[LeftHip]=true;input(s,held);assert(!s.member(1).funnels);
  }
  {
    auto s=fixture();press(s,Gear::LongRangeScanners);assert(s.member(1).rangeExtension>5&&s.member(1).scanRemaining>4&&s.member(1).precog==0);
    target(s,{0,0,-60});fit(s,Gear::MicroMissiles);Input in;in.fire[LeftHip]=true;input(s,in);assert(!s.ordnance.empty());
    auto &missile=s.ordnance.back();assert(missile.body.weapon==Weapon::Homing&&missile.body.homingTarget==0&&missile.body.damage>=3&&missile.body.damage<=12&&missile.body.life>4.f);
  }
  {
    auto s=fixture();press(s,Gear::MicroMissiles);assert(s.ordnance.size()==1);auto shot=s.ordnance.front();
    assert(shot.body.homingTarget<0&&shot.body.damage>=3&&shot.body.damage<=12&&length(shot.body.velocity)>80&&shot.body.life>4.f);
    for(int i=0;i<30;++i)input(s,Input{});
    assert(!s.ordnance.empty());
  }
  {
    auto s=fixture();target(s,{0,0,-20});press(s,Gear::PrecogSystem);
    assert(s.member(1).precogTarget==0&&s.member(1).precog>5&&!s.member(1).sheet.targetLock);
    auto miss=fixture();press(miss,Gear::PrecogSystem);assert(miss.member(1).precog==0&&miss.member(1).precogTarget==-1);
  }
  {
    auto s=fixture();press(s,Gear::ShieldNodes);input(s,Input{});
    assert(s.member(1).shieldNodes&&s.member(1).shield==3&&s.member(2).shield==3);
    Session replica;replica.readSnapshot(s.snapshot());assert(replica.member(1).shieldNodes&&replica.member(2).shield==s.member(2).shield);
  }
  {
    auto scrap=fixture();fit(scrap,Gear::Scrap);scrap.member(1).sheet.ap=0;
    Input use;use.fire[LeftHip]=true;input(scrap,use);
    assert(!mountAllowed(Gear::Scrap,LeftHip)&&scrap.member(1).sheet.ap==0&&
           scrap.member(1).sheet.inventory[size_t(Gear::Scrap)]==3);
    auto battery=fixture();press(battery,Gear::VerilianBattery);assert(battery.member(1).sheet.batteryDie==6&&battery.member(1).sheet.inventory[size_t(Gear::VerilianBattery)]==2);
    battery.member(1).body.cooldown[LeftHip]=0;input(battery,use);assert(battery.member(1).sheet.inventory[size_t(Gear::VerilianBattery)]==2);
  }
  // Retired fittings cannot be fitted, traded or used through the old mount.
  for(auto gear:{Gear::Holoprojector,Gear::WristDrill,Gear::CableWinch,Gear::CargoSpider,Gear::GlideThrusters,Gear::VeriformFrame,Gear::RearThrusters,Gear::FusionGrenade}){
    assert(!mountAllowed(gear,LeftHip)&&retiredGear(gear));
    auto session=fixture();press(session,gear);
    assert(session.world.pickups.empty()&&session.member(1).sheet.reactorStress==0);
  }
  // Old catalog arrays expand without losing inventory or the saved callsign.
  {
    Dice dice{9};auto sheet=createSheet(Chassis::Striker,PilotKind::Engineer,dice);finishCreation(sheet);
    Json legacy=sheet;legacy["inventory"]=std::vector<int>(31,0);legacy["inventory"][size_t(Gear::Repair)]=2;
    legacy["owned"]=std::vector<int>(31,0);legacy["owned"][size_t(Gear::Blade)]=1;legacy["owned"][size_t(Gear::Rifle)]=1;
    legacy["oneShotStock"]=std::vector<int>(21,0);legacy.erase("batteryDie");
    auto path=std::filesystem::temp_directory_path()/("mecha-manual-profile-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    {std::ofstream out(path);out<<Json{{"schema",4},{"callsign","Offline Atlas"},{"sheet",legacy}};}
    std::string name;auto restored=loadProfile(path,&name);assert(name=="Offline Atlas"&&restored.inventory[size_t(Gear::Repair)]==2&&restored.inventory[size_t(Gear::FusionGrenade)]==0);
    restored.credits=1000;restored.owned[size_t(Gear::CargoSpider)]=1;
    restored.equipment[RightHip]=Gear::CargoSpider;
    restored.equipment[LeftHip]=Gear::Scrap;
    restored.inventory[size_t(Gear::Scrap)]=9;
    restored.inventory[size_t(Gear::OneShotModule)]=2;
    restored.oneShotStock[size_t(Module::NanoShroud)]=2;
    {std::ofstream out(path);out<<Json{{"schema",8},{"callsign",name},{"sheet",restored}};}
    auto again=loadProfile(path,&name);assert(again.owned[size_t(Gear::CargoSpider)]==0&&again.equipment[RightHip]==Gear::Empty&&again.credits==1325&&again.equipment[LeftHip]==Gear::Empty&&again.inventory[size_t(Gear::Scrap)]==9&&again.inventory[size_t(Gear::OneShotModule)]==0&&again.oneShotStock[size_t(Module::NanoShroud)]==0);std::filesystem::remove(path);
  }
  std::cout<<"Manual authoritative modules, physical supplies, gadget interactions and offline profile migration passed\n";
}
