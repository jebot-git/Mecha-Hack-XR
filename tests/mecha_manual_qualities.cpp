#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace iron;
using namespace iron::rpg;
namespace {
constexpr float tick=.01f;
void input(Session &s,Input in={},float dt=tick){s.command(1,{{"op","input"},{"epoch",s.epoch},{"input",coop::inputWire(in)}});s.step(dt);}
Session fixture(){
  static const Session initial=[] {
    Session s(731);s.add(1,"Quality audit");finishCreation(s.member(1).sheet);
    GameSettings settings;settings.maps=1;settings.enemy=3;settings.enemyCount=1;settings.objective=0;settings.opener=5;settings.environment=3;settings.location=0;
    s.command(1,{{"op","settings"},{"settings",settings}});s.command(1,{{"op","ready"},{"value",true}});s.command(1,{{"op","deploy"}});
    s.enemies[0].disabled=10000;s.enemies[0].hp=s.enemies[0].maxHp=100000;
    s.world.proceduralTerrain=false;s.world.editGeometry().terrain.clear();s.world.indexTerrain();s.world.interiorRegions.clear();s.world.waterRegions.clear();
    for(auto &t:s.world.targets)t.active=false;
    auto &m=s.member(1);m.body.position={};m.sheet.equipment.fill(Gear::Empty);m.sheet.qualities.fill(Quality::None);m.sheet.modules={Module::GeistSystem};m.sheet.attributes[3]=32;
    m.body.cooldown.fill(0);m.held.fill(false);
    s.mission.recipe.countdownSeconds=0;s.mission.recipe.planetoidHazard=s.mission.recipe.complication=-1;s.mission.recipe.opener=5;s.mission.openerTime=100;
    input(s);return s;
  }();return initial;
}
void fit(Session &s,Gear gear,Quality quality){auto &m=s.member(1);
  m.sheet.equipment.fill(Gear::Empty);m.sheet.qualities.fill(Quality::None);m.sheet.owned[size_t(gear)]=2;m.sheet.equipment[RightHand]=gear;
  normalizeHeavyEquipment(m.sheet);equipQuality(m.sheet,RightHand,quality);m.body.cooldown.fill(0);m.recoil.fill(0);m.held.fill(false);
}
void enemy(Session &s,float distance){auto &t=s.world.targets[0];t.active=true;t.health=100000;t.origin={0,0,-distance};t.center={0,10,-distance};t.half={8,10,1};t.ai.boundsReady=false;}
Shot shoot(Session &s,int hand=RightHand){s.ordnance.clear();s.member(1).body.cooldown.fill(0);s.member(1).recoil.fill(0);s.member(1).sheet.coolant(1.f);s.member(1).held.fill(false);Input fire;fire.fire[hand]=true;input(s,fire,.001f);assert(s.ordnance.size()==1);return s.ordnance.front();}
float deviation(const Session &s,const Shot &shot,int hand=RightHand){return std::acos(std::clamp(dot(unit(shot.body.velocity),unit(s.members.front().body.arms[hand].forward)),-1.f,1.f));}
void settle(Session &s,float seconds){for(int n=0;n<int(std::ceil(seconds/tick));++n)input(s);}
int impact(Quality quality,float distance){auto s=fixture();enemy(s,distance);
  Shot shot;shot.body.position={0,6,0};shot.body.velocity={0,0,-100};shot.body.life=2;shot.body.damage=4;shot.body.shooter=1;shot.body.mount=RightHand;shot.quality=quality;
  s.ordnance.push_back(shot);int before=s.enemies[0].hp;settle(s,1);assert(s.ordnance.empty());return before-s.enemies[0].hp;
}
}
int main(){setOriginalAssetRoot(MECHA_ASSETS);
  // Simple bonuses retain exactly the same physical ray and damage roll.
  {
    auto plain=fixture();fit(plain,Gear::Rifle,Quality::None);auto beam=plain;beam.member(1).sheet.qualities[RightHand]=Quality::Beam;
    auto a=shoot(plain),b=shoot(beam);assert(b.body.damage==a.body.damage+1&&length(a.body.velocity-b.body.velocity)<.001f);
    auto melee=fixture();fit(melee,Gear::Blade,Quality::None);enemy(melee,10);auto thermal=melee;thermal.member(1).sheet.qualities[RightHand]=Quality::Thermal;
    Input attack;attack.fire[RightHand]=true;input(melee,attack);input(thermal,attack);
    assert(thermal.enemies[0].hp==melee.enemies[0].hp-1&&thermal.member(1).body.punches[RightHand]==1);
  }
  {
    auto paired=fixture();fit(paired,Gear::Rifle,Quality::None);paired.member(1).sheet.equipment[LeftHand]=Gear::Rifle;
    auto dual=paired;equipQuality(dual.member(1).sheet,RightHand,Quality::Dual);Input fire;fire.fire[LeftHand]=fire.fire[RightHand]=true;fire.ping={true,true};
    input(paired,fire,.001f);input(dual,fire,.001f);assert(paired.ordnance.size()==2&&dual.ordnance.size()==2);
    float plainDamage=0,dualDamage=0;for(auto &shot:paired.ordnance)plainDamage+=shot.body.damage;for(auto &shot:dual.ordnance)dualDamage+=shot.body.damage;
    assert(dualDamage==plainDamage+2&&length(dual.ordnance[0].launchOrigin-dual.ordnance[1].launchOrigin)>1);
    assert(!dual.member(1).handSupported[0]&&!dual.member(1).handSupported[1]);
  }
  {
    auto fed=fixture();fit(fed,Gear::Rifle,Quality::ScrapFed);fed.member(1).sheet.inventory[size_t(Gear::Scrap)]=1;
    auto plain=fed;plain.member(1).sheet.qualities[RightHand]=Quality::None;int ap=fed.member(1).sheet.ap;
    assert(shoot(fed).body.damage==shoot(plain).body.damage+3);assert(fed.member(1).sheet.inventory[size_t(Gear::Scrap)]==0&&fed.member(1).sheet.ap==ap);
    plain=fed;plain.member(1).sheet.qualities[RightHand]=Quality::None;assert(shoot(fed).body.damage==shoot(plain).body.damage+3&&fed.member(1).sheet.ap==ap-1);
    fed.member(1).sheet.ap=0;plain=fed;plain.member(1).sheet.qualities[RightHand]=Quality::None;assert(shoot(fed).body.damage==shoot(plain).body.damage&&fed.member(1).sheet.ap==0);
  }
  {
    auto s=fixture();fit(s,Gear::Cannon,Quality::Buster);s.member(1).sheet.attributes[0]=3;s.member(1).sheet.attributes[2]=32;
    Input held;held.fire[RightHand]=true;input(s,held);assert(s.ordnance.empty()&&s.member(1).busterReady[RightHand]&&s.member(1).body.cooldown[RightHand]>.7f);
    for(int n=0;n<100&&s.ordnance.empty();++n)input(s,held);
    assert(s.ordnance.size()==1&&s.ordnance[0].body.damage>=7&&s.ordnance[0].body.damage<=14&&deviation(s,s.ordnance[0])<.007f);
    assert(!s.member(1).busterReady[RightHand]&&s.member(1).body.shots[RightHand]==1);
  }
  {
    assert(impact(Quality::Artillery,20)==impact(Quality::None,20));int bonus=impact(Quality::Artillery,60)-impact(Quality::None,60);assert(bonus>=1&&bonus<=4);
    assert(impact(Quality::SawedOff,20)==impact(Quality::None,20)+1&&impact(Quality::SawedOff,60)==impact(Quality::None,60));
    auto near=fixture();fit(near,Gear::Rifle,Quality::SawedOff);near.member(1).sheet.attributes[0]=3;enemy(near,20);auto far=near;enemy(far,60);
    auto a=shoot(near),b=shoot(far);assert(deviation(far,b)>1.7f*deviation(near,a));
  }
  {
    auto plain=fixture();fit(plain,Gear::Rifle,Quality::None);auto automated=plain;automated.member(1).sheet.qualities[RightHand]=Quality::Automated;
    Input held;held.fire[RightHand]=true;for(int n=0;n<250;++n){
      input(plain,held);input(automated,held);
      plain.member(1).sheet.coolant(1.f);automated.member(1).sheet.coolant(1.f);
    }
    assert(automated.member(1).body.shots[RightHand]>plain.member(1).body.shots[RightHand]&&automated.member(1).body.shots[RightHand]<12);
  }
  {
    auto s=fixture();fit(s,Gear::Cannon,Quality::BeltFed);enemy(s,25);Input held;held.fire[RightHand]=true;
    for(int n=0;n<1200&&s.member(1).beltTarget[RightHand]!=0;++n)input(s,held);
    assert(s.member(1).beltTarget[RightHand]==0);auto fired=s.member(1).body.shots[RightHand];
    auto &wall=s.world.targets.back();wall.active=true;wall.invulnerable=true;wall.health=100000;wall.center={0,10,-15};wall.half={20,10,1};
    for(int n=0;n<200&&s.member(1).beltTarget[RightHand]==0;++n)input(s,held);
    assert(s.member(1).beltTarget[RightHand]==-1&&s.member(1).body.cooldown[RightHand]>2.9f&&s.member(1).body.shots[RightHand]==fired);
    wall.active=false;for(int n=0;n<200;++n)input(s,held);assert(s.member(1).body.shots[RightHand]==fired);
    for(int n=0;n<130;++n)input(s,held);
    assert(s.member(1).body.shots[RightHand]>fired);
  }
  {
    // A one on the damage die jams the feed even when a stance adds damage.
    auto s=fixture();fit(s,Gear::Cannon,Quality::BeltFed);enemy(s,25);s.member(1).siege=true;bool rolledOne=false;
    for(int n=0;n<64&&!rolledOne;++n){auto shot=shoot(s);rolledOne=shot.body.damage==5;settle(s,.3f);}
    assert(rolledOne&&s.member(1).beltTarget[RightHand]==-1&&s.member(1).body.cooldown[RightHand]>2.5f);
  }
  {
    auto s=fixture();auto &wall=s.world.targets.back();wall.active=true;wall.invulnerable=true;wall.health=100000;wall.center={0,10,-10};wall.half={20,10,1};
    Shot shot;shot.body.position={0,6,0};shot.body.velocity={0,0,-100};shot.body.life=1;shot.body.damage=6;shot.body.shooter=1;shot.quality=Quality::Bouncing;s.ordnance.push_back(shot);
    for(int n=0;n<20&&!s.ordnance.empty()&&s.ordnance[0].ricochets==0;++n)input(s);
    assert(s.ordnance.size()==1&&s.ordnance[0].ricochets==1&&s.ordnance[0].body.velocity.z>0&&s.ordnance[0].body.damage>=1&&s.ordnance[0].body.damage<=4);
    int hp=s.enemies[0].hp;settle(s,.6f);assert(s.ordnance.empty()&&s.enemies[0].hp==hp);
  }
  {
    auto s=fixture();fit(s,Gear::Rifle,Quality::Unstable);auto &m=s.member(1);m.sheet.modules.push_back(Module::LodiumAlloy);m.sheet.equipment[LeftHand]=Gear::Shield;prepare(m.sheet);
    bool exploded=false,selfDamage=false;int die=damageDie(m.sheet);
    for(int n=0;n<96;++n){m.sheet.hp=m.sheet.maxHp;m.sheet.ap=m.sheet.maxAp;int health=m.sheet.hp+m.sheet.ap;auto shot=shoot(s);
      exploded|=shot.body.damage>die;int loss=health-m.sheet.hp-m.sheet.ap;selfDamage|=loss>0;assert(loss>=0&&loss<=die&&shot.body.damage>=1&&shot.body.damage<=2*die);
    }
    assert(exploded&&selfDamage&&!m.ejected);
  }
  {
    auto s=fixture();fit(s,Gear::Blade,Quality::Lodium);enemy(s,10);bool restored=false;int hp=s.enemies[0].hp;
    for(int n=0;n<256;++n){s.member(1).sheet.ap=0;s.member(1).sheet.coolant(1.f);s.member(1).held.fill(false);s.member(1).body.cooldown.fill(0);Input attack;attack.fire[RightHand]=true;input(s,attack,.001f);restored|=s.member(1).sheet.ap==1;assert(s.member(1).sheet.ap<=1);}
    assert(restored&&s.enemies[0].hp<hp&&s.member(1).body.punches[RightHand]==256);
  }
  {
    auto s=fixture();auto &m=s.member(1);m.funnels=m.shieldNodes=m.veriform=true;m.manualClock=1.25f;m.precog=4;m.precogTarget=0;m.nodeMobility=2;m.rangeExtension=3;m.pistonCooldown=2.5f;
    m.immobile=1;m.mobilityDisrupt=2;m.systemDisrupt=3;m.busterReady[RightHand]=true;m.beltTarget[LeftHand]=0;m.sheet.batteryDie=4;
    auto snap=s.snapshot();Session copy;copy.readSnapshot(snap);assert(copy.snapshot().at("players")[0].at("manual")==snap.at("players")[0].at("manual"));assert(copy.member(1).sheet.batteryDie==4);
  }
  std::cout<<"All12 Mission Manual qualities: physical damage, paired hands, cadence, charge, feed/reload, ricochet, bounded recoil-era effects and replicated state verified\n";
}
