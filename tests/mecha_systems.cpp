#include "mecha_rig_fixture.hpp"
#include "rpg/session.hpp"
#include "rpg/preparation.hpp"
#include "animation.hpp"
#include "searchlight.hpp"
#include "rpg/weak_points.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
using namespace iron;using namespace iron::rpg;
template<class F>static bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
static void input(Session &s,uint32_t id,const Input &in,float dt=1.f/60){s.command(id,{{"op","input"},{"epoch",s.epoch},{"input",coop::inputWire(in)},{"systems",in.ping}});s.step(dt);}
static Session fixture(){
 Session s(713);s.add(1,"Atlas");s.add(2,"Beacon");for(auto &m:s.members)finishCreation(m.sheet);
 GameSettings settings;settings.maps=1;settings.enemy=3;settings.enemyCount=1;settings.objective=0;settings.opener=5;settings.environment=3;settings.location=0;
 s.command(1,{{"op","settings"},{"settings",settings}});s.command(1,{{"op","ready"},{"value",true}});s.command(2,{{"op","ready"},{"value",true}});s.command(1,{{"op","deploy"}});
 s.enemies[0].disabled=10000;s.world.proceduralTerrain=false;s.world.editGeometry().terrain.clear();s.world.indexTerrain();s.world.interiorRegions.clear();s.world.waterRegions.clear();for(auto &t:s.world.targets)t.active=false;
 s.member(1).body.position={};s.member(2).body.position={20,0,0};input(s,1,Input{});input(s,2,Input{});return s;
}
static Json read(const std::filesystem::path &path){std::ifstream f(path);Json j;f>>j;return j;}
static Vec3 vector(const Json &j){return {j[0],j[1],j[2]};}
static void manualRoster(Dice &dice){
 // Pilots retain the core and Mission Manual passive attribute bonuses. Secondary pilot
 // powers are excluded, including the old command forms and allied targets.
 constexpr int bonuses[][4]={{2,0,0,1},{0,1,2,0},{1,2,0,0},{0,0,1,2},{0,1,2,0},{2,0,0,1},{1,1,0,1},{1,0,0,2}};
 Dice baselineDice{119};auto baseline=createSheet(Chassis::Striker,PilotKind::Commander,baselineDice);
 for(int kind=0;kind<int(PilotKind::Count);++kind){
  Dice creation{119};auto sheet=createSheet(Chassis::Striker,PilotKind(kind),creation);
  for(int attribute=0;attribute<4;++attribute)assert(sheet.attributes[attribute]-bonuses[kind][attribute]==baseline.attributes[attribute]-bonuses[0][attribute]);
  auto s=fixture();auto &m=s.member(1);m.sheet.pilot=PilotKind(kind);m.sheet.hp=std::max(1,m.sheet.maxHp-3);
  auto before=Json(m.sheet);auto allyBefore=Json(s.member(2).sheet);s.member(2).body.cooldown[RightHand]=3;
  assert(rejects([&]{s.command(1,{{"op","pilot"}});}));
  assert(rejects([&]{s.command(1,{{"op","pilot"},{"target",2},{"move",true}});}));
  assert(Json(m.sheet)==before&&Json(s.member(2).sheet)==allyBefore&&s.member(2).body.cooldown[RightHand]==3);
  assert(std::string_view(pilotRoleDescription(PilotKind(kind))).size()>10);
  const auto snapshot=s.snapshot();
  assert(!snapshot.at("players").at(0).contains("pilot_cooldown"));
  Session restored;restored.readSnapshot(snapshot);assert(restored.snapshot()==snapshot);
 }
 {
  auto s=fixture();
  auto &pilot=s.member(1);auto &ally=s.member(2);
  pilot.sheet=createSheet(Chassis::Auxiliary,PilotKind::Vet,dice);
  pilot.body.chassisYaw=pilot.body.torsoYaw=0;ally.body.position={0,0,-12};ally.sheet.stress(dice,.5f);
  auto activate=[&]{Input in;input(s,1,in);pilot.chassisCooldown=0;in.ping[1]=true;input(s,1,in);};
  float heat=ally.sheet.reactorStress;activate();
  assert(ally.reinforced&&ally.sheet.reactorStress<heat&&pilot.sheet.reactorStress>0);
  Session guest;guest.readSnapshot(s.snapshot());assert(guest.member(2).reinforced);
  pilot.sheet=createSheet(Chassis::Colossus,PilotKind::Merc,dice);
  activate();assert(pilot.garrison);
  Session replica;replica.readSnapshot(s.snapshot());assert(replica.member(1).garrison);
  pilot.sheet=createSheet(Chassis::Hybrid,PilotKind::Pariah,dice);
  pilot.garrison=false;activate();
  assert(!s.ordnance.empty()&&s.ordnance.back().quality==Quality::Beam);
  pilot.sheet.stress(dice,2.f);assert(pilot.sheet.overheated);
  s.step(1.f/60);assert(!pilot.sheet.overheated);
  pilot.sheet=createSheet(Chassis::Vanguard,PilotKind::Bionic,dice);
  pilot.sheet.owned[size_t(Gear::Cannon)]=1;equipGear(pilot.sheet,LeftHand,Gear::Cannon);
  assert(pilot.sheet.equipment[RightHand]==Gear::Rifle&&equipmentOwner(pilot.sheet,RightHand)==RightHand);
  pilot.sheet.owned[size_t(Gear::HeavyBlade)]=1;
  assert(rejects([&]{equipGear(pilot.sheet,RightHand,Gear::HeavyBlade);}));
  auto legacy=pilot.sheet;
  customize(legacy,{{"modules",{"shoulder_hardpoint"}},{"equipment",{{"left_hand","cannon"},{"right_hand","cannon"},
      {"left_shoulder","empty"},{"right_shoulder","empty"},{"left_hip","repair"},{"right_hip","scan"}}}});
  assert(legacy.equipment[LeftHand]==Gear::Cannon&&legacy.equipment[RightHand]==Gear::Empty);
  int die=damageDie(pilot.sheet);activate();
  assert(damageDie(pilot.sheet)==die+2&&pilot.sheet.powerRouting==1);
  activate();
  assert(damageDie(pilot.sheet)==die-2&&pilot.sheet.powerRouting==-1);
  s.phase=Phase::Defeat;
  pilot.reinforced=true;ally.garrison=true;ally.berserkRetreat=.5f;
  s.command(1,{{"op","hangar"}});
  assert(damageDie(pilot.sheet)==die&&pilot.sheet.powerRouting==0&&!pilot.reinforced&&pilot.chassisCooldown==0);
  assert(!ally.garrison&&ally.berserkRetreat==0);
 }
}
int main(int argc,char **argv){
 Dice dice{17};
 if(argc==2&&std::string_view(argv[1])=="--manual-roster"){manualRoster(dice);return 0;}
 for(int item=1;item<int(Gear::Count);++item){auto gear=Gear(item);if(gear==Gear::Comlink)continue;
  for(int slot=0;slot<MountCount;++slot){auto s=createSheet(Chassis::Striker,PilotKind::Engineer,dice);s.equipment.fill(Gear::Empty);s.owned[size_t(gear)]=1;if(gearInfo(gear).module!=Module::Count)s.modules={gearInfo(gear).module};
   bool allowed=mountAllowed(gear,slot);bool failed=rejects([&]{equipGear(s,slot,gear);});assert(allowed!=failed);
  }
 }
 {
  auto match=fixture();
  auto &pilot=match.member(1);
  pilot.sheet.ap=pilot.sheet.maxAp=4;
  pilot.shield=0;
  const auto oldHp=pilot.sheet.hp;
  const auto oldIntegrity=match.view(1).rpgArmor;
  Shot shot;
  shot.body.position={5,6,0};shot.body.velocity={-220,0,0};
  shot.body.damage=2;shot.body.life=2;shot.body.owner=-1;shot.body.shooter=-1;
  shot.body.kind=ProjectileKind::Bullet;shot.body.weapon=Weapon::Rifle;
  match.ordnance.push_back(shot);
  match.step(.05f);
  const auto view=match.view(1);
  if(pilot.sheet.hp!=oldHp||pilot.sheet.ap!=2||view.rpgArmor>=oldIntegrity||
     view.damageUntil[2]<=view.elapsed)
    throw std::runtime_error("Armour-only projectile did not update integrity and bearing");
  Session guest;guest.readSnapshot(match.snapshot());
  if(guest.view(1).damageUntil!=view.damageUntil||guest.view(2).damageUntil[2]>0)
    throw std::runtime_error("Co-op snapshot lost the directional hit or sent it to another pilot");
 }
 auto sights=fixture();auto &pilot=sights.member(1);pilot.sheet.equipment.fill(Gear::Empty);assert(sights.view(1).rpgAimMask==0);for(auto weapon:sights.view(1).weapons)assert(weapon==Weapon::Empty);
 pilot.sheet.equipment[0]=Gear::Rifle;assert(sights.view(1).rpgAimMask==1);
 pilot.sheet.equipment[1]=Gear::Shield;assert(sights.view(1).rpgAimMask==1);
 pilot.sheet.equipment[2]=Gear::NanoShroud;assert(sights.view(1).rpgAimMask==1);
 pilot.sheet.equipment[4]=Gear::ChestBeam;assert(sights.view(1).rpgAimMask==5);
 pilot.injuries=uint32_t(MechaInjury::Targeting);assert(sights.view(1).rpgAimMask==0);pilot.injuries=0;
 pilot.sheet.equipment.fill(Gear::Empty);pilot.sheet.equipment[1]=Gear::Cannon;normalizeHeavyEquipment(pilot.sheet);assert(sights.view(1).rpgAimMask==2);
 pilot.vehicleBlend=.5f;assert(sights.view(1).rpgAimMask==0);
 // Refitting retains all owned stock and moves a hand module onto a body mount.
 auto old=createSheet(Chassis::Striker,PilotKind::Engineer,dice);old.modules={Module::ChestBeam};old.equipment.fill(Gear::Empty);old.equipment[LeftHand]=Gear::ChestBeam;old.equipment[RightShoulder]=Gear::Rifle;auto stock=old.owned;
 refitMounts(old);validate(old);assert(old.owned==stock&&old.equipment[0]==Gear::Rifle&&old.equipment[2]==Gear::ChestBeam);
 manualRoster(dice);
 Input fire;
 // Buying/selling is transactional and only allowed between missions.
 Session shop;shop.add(1,"Trader");auto &buyer=shop.member(1);finishCreation(buyer.sheet);buyer.sheet.credits=200;
 shop.command(1,{{"op","trade"},{"action","buy"},{"gear","rifle"}});assert(buyer.sheet.credits==160&&buyer.sheet.owned[size_t(Gear::Rifle)]==2);
 shop.command(1,{{"op","trade"},{"action","sell"},{"gear","rifle"}});assert(buyer.sheet.credits==180);
 auto before=Json(buyer.sheet);assert(rejects([&]{shop.command(1,{{"op","trade"},{"action","sell"},{"gear","rifle"}});}));assert(Json(buyer.sheet)==before);
 shop.command(1,{{"op","trade"},{"action","buy"},{"gear","repair"}});assert(buyer.sheet.inventory[size_t(Gear::Repair)]==1);
 shop.command(1,{{"op","trade"},{"action","sell"},{"gear","repair"}});assert(buyer.sheet.inventory[size_t(Gear::Repair)]==0&&buyer.sheet.credits==180-gearInfo(Gear::Repair).cost+gearInfo(Gear::Repair).cost/2);
 shop.phase=Phase::Combat;assert(rejects([&]{shop.command(1,{{"op","trade"},{"action","buy"},{"gear","repair"}});}));
 // Contract reward depends on the mission and is paid once, on final success.
 std::set<int> rewards;GameSettings settings;settings.maps=1;settings.enemy=3;settings.enemyCount=1;
 for(int objective=0;objective<10;++objective){settings.objective=objective;auto recipe=generateMission(19,0,settings,1);assert(recipe.rewardCredits>0&&Json(recipe)==Json(generateMission(19,0,settings,1)));rewards.insert(recipe.rewardCredits);}assert(rewards.size()>2);
 auto reward=fixture();
 const size_t optional=reward.world.pickups.size();
 reward.world.pickups.push_back({{10000,0,10000},rpgSafeBoxPickup});
 reward.supplies.push_back({optional,SafeBoxKind::ModuleCache,123,0});
 int credit=reward.member(1).sheet.credits,payout=reward.mission.contractReward;reward.enemies[0].hp=0;reward.step(.05f);assert(reward.phase==Phase::Complete&&reward.member(1).sheet.credits==credit+payout);reward.step(.05f);assert(reward.member(1).sheet.credits==credit+payout);
 // Reversible vehicle sequence, joystick drive/steering, attack and focus gates.
 auto vehicle=fixture();auto &driver=vehicle.member(1);driver.sheet.modules={Module::VehicleMode};driver.sheet.equipment[LeftHip]=Gear::VehicleMode;fire={};fire.fire[LeftHip]=true;input(vehicle,1,fire);assert(driver.vehicle&&driver.vehicleBlend<.1f);
 fire={};for(int n=0;n<100;++n)input(vehicle,1,fire);assert(driver.vehicleBlend==1);
 Vec3 start=driver.body.position;fire.forward=1;for(int n=0;n<120;++n)input(vehicle,1,fire);assert(length(driver.body.position-start)>20&&driver.body.walkingSpeed==0);
 driver.stun=1;start=driver.body.position;for(int n=0;n<20;++n)input(vehicle,1,fire);assert(length(driver.body.position-start)<.001f&&driver.vehicleSpeed==0);driver.stun=0;
 float yaw=driver.body.chassisYaw;fire.steer=.7f;for(int n=0;n<60;++n)input(vehicle,1,fire);assert(std::abs(wrap(driver.body.chassisYaw-yaw))>.2f);
 auto shots=driver.body.shots[RightHand];fire.fire[RightHand]=true;input(vehicle,1,fire);assert(driver.body.shots[RightHand]==shots&&vehicle.view(1).rpgAimMask==0);
 fire={};fire.focused=false;start=driver.body.position;for(int n=0;n<20;++n)input(vehicle,1,fire);assert(length(driver.body.position-start)<.001f&&driver.vehicleSpeed==0);
 fire={};input(vehicle,1,fire);fire.fire[LeftHip]=true;input(vehicle,1,fire);assert(!driver.vehicle);fire={};for(int n=0;n<100;++n)input(vehicle,1,fire);assert(driver.vehicleBlend==0);
 auto collision=fixture();auto &car=collision.member(1);car.vehicle=true;car.vehicleBlend=1;auto &wall=collision.world.targets.back();wall.active=true;wall.health=100;wall.center={0,8,-14};wall.half={20,8,1};
 fire={};fire.forward=1;for(int n=0;n<180;++n)input(collision,1,fire);assert(car.body.position.z>-13&&car.body.position.z<-1);
 // Mobility changes stride speed, consecutive boosts and recharge.
 auto low=fixture(),high=low;auto &slow=low.member(1);auto &fast=high.member(1);
 slow.sheet.attributes[1]=6;fast.sheet.attributes[1]=18;
 slow.thrusters={};fast.thrusters={};
 Input walk;walk.forward=1;
 for(int n=0;n<60;++n){input(low,1,walk);input(high,1,walk);}
 assert(length(fast.body.position)>length(slow.body.position)*2.5f);
 assert(slow.body.rpgThrustCapacity==2&&fast.body.rpgThrustCapacity==4);
 {
  auto standard=fixture();
  auto &sheet=standard.member(1).sheet;
  sheet.pilot=PilotKind::Maverick;
  normalizeCombatProfile(sheet);prepare(sheet);
  assert(sheet.attributes[1]==14);
  auto refined=standard;
  auto &upgraded=refined.member(1).sheet;
  upgraded.campaignAwards=1;
  refinePilot(upgraded,PilotRefinement::Mobility,true);
  for(int n=0;n<24;++n){input(standard,1,walk);input(refined,1,walk);}
  assert(length(refined.member(1).body.position)>length(standard.member(1).body.position));
  assert(standard.member(1).body.rpgThrustCapacity==3&&refined.member(1).body.rpgThrustCapacity==3);
 }
 Input thrust;thrust.thrust=true;input(high,1,thrust);
 assert(fast.body.rpgThrustCharge==4); // a centered stick can be tilted after clicking
 const auto dashStart=fast.body.position;thrust.steer=1;
 for(int n=0;n<20;++n)input(high,1,thrust);
 assert(fast.body.position.x-dashStart.x>7&&fast.body.rpgThrustCharge<3.1f);
 for(int n=0;n<20;++n)input(high,1,thrust);
 assert(fast.body.rpgThrustCharge>3.f); // holding does not repeat
 input(high,1,Input{});
 auto &blocker=high.world.targets.back();
 blocker.active=true;blocker.health=20;blocker.rpgDecoration=false;
 blocker.center=fast.body.position+Vec3{5,5,0};blocker.half={1,5,8};
 const auto wallStart=fast.body.position;fast.body.chassisYaw=0;
 for(int n=0;n<24;++n)input(high,1,thrust);
 assert(fast.body.position.x<wallStart.x+4.f&&blocker.health==20);
 blocker.active=false;input(high,1,Input{});
 Input rise;rise.ascend=true;input(high,1,rise);
 assert(fast.body.position.y>0); // fixed lift also works on dry land
 input(high,1,Input{});
 high.biome=1;high.world.proceduralTerrain=true;
 assert(!searchlightActive(high.view(1)));
 Input lamp;lamp.searchlight=true;
 input(high,1,lamp);assert(searchlightActive(high.view(1)));
 input(high,1,lamp);assert(searchlightActive(high.view(1)));
 input(high,1,Input{});input(high,1,lamp);assert(!searchlightActive(high.view(1)));
 input(high,1,Input{});
 for(int n=0;n<18;++n)input(high,1,rise);
 assert(fast.body.position.y>4.f);
 for(int n=0;n<360;++n)input(high,1,Input{});
 assert(fast.body.position.y>4.f&&fast.body.rpgThrustCharge==4); // A spent pulse leaves the hull aloft in zero G.
 fast.body.position.y=0;
 high.biome=2;
 assert(!searchlightActive(high.view(1)));
 input(high,1,lamp);assert(searchlightActive(high.view(1)));
 input(high,1,Input{});input(high,1,lamp);assert(!searchlightActive(high.view(1)));
 high.world.waterRegions.push_back({{-150,-150,150,150},16,16});
 high.world.submergedRoutes.push_back({-150,-150,150,150});
 for(int n=0;n<18;++n)input(high,1,rise);
 assert(fast.body.position.y>2.f&&fast.body.position.y<16.f);
 high.biome=3;assert(!searchlightActive(high.view(1)));
 // Dry lift has the same ceiling with or without horizontal thrust.
 {
  auto solo=fixture(),horizontal=fixture(),combo=fixture();
  for(auto *s:{&solo,&horizontal,&combo}){
   s->biome=3;
   s->mission.recipe.location=0;
   s->member(1).sheet.attributes[1]=16;
  }
  Input dash;dash.thrust=true;dash.steer=1;
  Input both=dash;both.ascend=true;
  float peak=0,combinedPeak=0,combinedHeat=0,soloHeat=0;
  for(int i=0;i<38;++i){
   input(solo,1,i==0?rise:Input{});
   input(horizontal,1,i==0?dash:Input{});
   input(combo,1,i==0?both:Input{});
   peak=std::max(peak,solo.member(1).body.position.y);
   combinedPeak=std::max(combinedPeak,combo.member(1).body.position.y);
   combinedHeat=std::max(combinedHeat,combo.member(1).sheet.reactorStress);
   soloHeat=std::max(soloHeat,solo.member(1).sheet.reactorStress);
  }
  assert(peak>6.3f&&peak<=6.51f&&std::abs(combinedPeak-peak)<.1f);
  assert(combo.member(1).body.position.x>horizontal.member(1).body.position.x*2&&combinedHeat>soloHeat*2);
 }
 // Energy and kinetic impacts use separate shield paths.
 {
  auto shielded=fixture();
  auto &pilot=shielded.member(1);
  pilot.shield=3;pilot.shieldRemaining=15;
  pilot.sheet.ap=pilot.sheet.maxAp=7;
  auto hit=[&](Weapon weapon,Quality quality){
   Shot shot;
   shot.body.position={5,6,0};shot.body.velocity={-220,0,0};
   shot.body.damage=2;shot.body.life=2;shot.body.owner=0;shot.body.shooter=-1;
   shot.body.kind=ProjectileKind::Bullet;shot.body.weapon=weapon;
   shot.quality=quality;
   shielded.ordnance.push_back(shot);
   shielded.step(.05f);
  };
  hit(Weapon::Rifle,Quality::None);
  assert(pilot.shield==3&&pilot.sheet.ap==5);
  hit(Weapon::Rail,Quality::Thermal);
  assert(pilot.shield==1&&pilot.sheet.ap==5);
 }
 // Radar detection survives visual camouflage and contains only a bearing at long range.
 auto sensed=fixture();
 sensed.enemies[0].spec=4; // Camo sniper
 auto &contact=sensed.world.targets[0];contact.active=true;contact.escaped=false;
 contact.center=contact.origin={0,3,-PassiveRadar::range*.85f};contact.health=8;
 auto &scanner=sensed.member(1);
 auto night=[&]{sensed.world.environment={Lighting::Night,Weather::Clear,0};sensed.world.night=true;};
 auto seesScanner=[&]{
  const auto disabled=sensed.enemies[0].disabled;
  sensed.enemies[0].disabled=0;
  night();
  const bool visible=enemyCanSeeMember(sensed,0,scanner);
  sensed.enemies[0].disabled=disabled;
  return visible;
 };
 assert(sensed.view(1).rpgRadar.contacts.empty()&&!seesScanner());
 Input scan;scan.fire[RightShoulder]=true;
 scanner.sheet.equipment[RightShoulder]=Gear::Empty;
 input(sensed,1,scan);
 assert(sensed.view(1).rpgRadar.contacts.empty()&&!pilotRevealed(scanner));
 input(sensed,1,Input{});
 scanner.sheet.equipment[RightShoulder]=Gear::Scan;
 input(sensed,1,scan);
 night();
 const auto near=sensed.view(1);
 assert(!near.targets[0].active&&near.rpgRadar.contacts.size()==1&&near.rpgRadar.distantBearings==0);
 assert(!searchlightActive(near)&&near.rpgRadar.exposesPilot&&pilotRevealed(scanner)&&seesScanner());
 const auto firstSweep=scanner.scanSweep;
 contact.center=contact.origin={PassiveRadar::range*2,3,0};
 for(int tick=0;tick<110;++tick)input(sensed,1,scan);
 assert(scanner.scanSweep==firstSweep&&!pilotRevealed(scanner)&&sensed.view(1).rpgRadar.strength<1.f);
 for(int tick=0;tick<260;++tick)input(sensed,1,Input{});
 const auto far=sensed.view(1);
 assert(scanner.scanSweep==firstSweep+1&&far.rpgRadar.contacts.empty()&&far.rpgRadar.distantBearings==(1u<<3));
 scanner.injuries|=uint32_t(MechaInjury::Sensors);
 assert(sensed.view(1).rpgRadar.distantBearings==0);
 scanner.injuries=0;
 input(sensed,1,scan);
 assert(!(scanner.activeScanners&(1u<<RightShoulder)));
 for(int tick=0;tick<375;++tick)input(sensed,1,Input{});
 assert(scanner.scanSweep==firstSweep+1&&sensed.view(1).rpgRadar.distantBearings==0&&!pilotRevealed(scanner));

 contact.center=contact.origin={0,3,-PassiveRadar::range*.85f};
 scanner.sheet.modules.push_back(Module::LongRangeScanners);
 scanner.sheet.equipment[LeftShoulder]=Gear::LongRangeScanners;
 Input covert;covert.fire[LeftShoulder]=true;
 input(sensed,1,covert);
 assert(scanner.scanSweep==firstSweep+2&&scanner.rangeExtension>5.f);
 assert(!pilotRevealed(scanner)&&!seesScanner());
 for(int tick=0;tick<365;++tick)input(sensed,1,Input{});
 const auto covertView=sensed.view(1);
 assert(scanner.scanSweep==firstSweep+3&&covertView.rpgRadar.contacts.size()==1&&covertView.rpgRadar.strength>0);
 assert(!covertView.rpgRadar.exposesPilot&&!pilotRevealed(scanner));
 Session covertReplica;covertReplica.readSnapshot(sensed.snapshot());
 assert(covertReplica.member(1).activeScanners==(1u<<LeftShoulder)&&covertReplica.view(1).rpgRadar.strength>0);
 input(sensed,1,lamp);
 assert(searchlightActive(sensed.view(1))&&pilotRevealed(scanner)&&seesScanner());
 input(sensed,1,covert);
 assert(!scanner.activeScanners);

 // Emergency repair spends an installed charge even while venting.
 {
  auto game=fixture();
  auto &pilot=game.member(1);
  pilot.sheet.hp=7;pilot.sheet.ap=0;
  pilot.sheet.inventory[size_t(Gear::Repair)]=2;
  pilot.sheet.repairKits=2;pilot.sheet.overheated=true;pilot.sheet.reactorStress=.95f;pilot.sheet.ventRemaining=1.1f;
  Shot hit;
  hit.body.position={5,6,0};hit.body.velocity={-220,0,0};
  hit.body.damage=4;hit.body.life=2;hit.body.owner=-1;hit.body.shooter=-1;
  hit.body.kind=ProjectileKind::Bullet;hit.body.weapon=Weapon::Rifle;
  game.ordnance.push_back(hit);
  game.step(.05f);
  assert(pilot.sheet.hp>3&&pilot.sheet.inventory[size_t(Gear::Repair)]==1&&pilot.sheet.repairKits==1);
  assert(pilot.body.cooldown[LeftHip]>4&&pilot.sheet.overheated);
  pilot.sheet.hp=5;hit.body.damage=2;game.ordnance.push_back(hit);game.step(.05f);
  assert(pilot.sheet.hp==3&&pilot.sheet.inventory[size_t(Gear::Repair)]==1);
 }
 // A grounded footfall hits the prop once and goes through RPG destruction.
 auto stomp=fixture();auto &walker=stomp.member(1);walker.sheet.attributes[1]=12;
 walker.body.chassisYaw=0;walker.body.gaitPhase=2.95f;
 auto &prop=stomp.world.targets.back();prop.active=true;prop.invulnerable=false;
 prop.escaped=false;prop.spawnType=-1;prop.rpgDecoration=true;
 prop.ai.boundsReady=false;prop.center={1.7f,.5f,-.15f};prop.half={.45f,.5f,.45f};prop.health=10;
 input(stomp,1,walk);
 assert(prop.health==0);
 auto &prey=stomp.world.targets[0];prey.active=true;prey.invulnerable=false;
 prey.ai.boundsReady=false;prey.center={1.7f,1.f,-.3f};prey.half={.5f,1.f,.5f};
 prey.health=float(stomp.enemies[0].hp);walker.body.gaitPhase=2.95f;
 input(stomp,1,walk);
 assert(stomp.enemies[0].hp==0);
 // A visible, close approach sends the lightweight mecha sideways on its own thrusters.
 auto evade=fixture();evade.member(1).body.chassisYaw=0;
 evade.member(2).body.position={80,0,80};
 auto &opponent=evade.enemies[0];opponent.disabled=0;opponent.fireClock=20;
 auto &npc=evade.world.targets[0];npc.active=true;npc.health=float(opponent.hp);
 npc.origin={0,0,-9};npc.ai.localCenter={0,2,0};npc.ai.localHalf={1.5f,2,1.5f};
 npc.ai.boundsReady=true;npc.invulnerable=false;evade.world.updateEnemyBounds(npc);
 for(int tick=0;tick<30&&opponent.thrusters.remaining==0;++tick)input(evade,1,walk);
 assert(opponent.thrusters.remaining>0&&std::abs(npc.origin.x)>.05f);
 auto &npcProp=evade.world.targets.back();npcProp.active=true;npcProp.invulnerable=false;
 npcProp.escaped=false;npcProp.spawnType=-1;npcProp.rpgDecoration=true;
 npcProp.ai.boundsReady=false;npcProp.half={1.f,.5f,1.f};npcProp.health=10;
 npcProp.center=npc.origin+rotate(angles(npc.yaw),{1.7f,.5f,0});
 npc.ai.gaitPhase=dot(npc.ai.velocity,direction(npc.yaw,0))>=0?2.98f:4.02f;
 for(int tick=0;tick<5&&npcProp.health>0;++tick)input(evade,1,walk);
 assert(npcProp.health==0);
 // The same controller press toggles a rifle once, and semi fires only on a new trigger edge.
 auto modes=fixture();auto &gunner=modes.member(1);gunner.sheet.equipment[RightHand]=Gear::Rifle;
 Input tracked;tracked.handValid={true,true};
 tracked.handPoses[LeftHand]={{-.18f,-.09f,-.80f},{},{},true};
 tracked.handPoses[RightHand]={{.18f,-.09f,-.80f},{},{},true};
 input(modes,1,tracked);tracked.fireMode=true;input(modes,1,tracked);input(modes,1,tracked);
 assert(gunner.rifleSemi);
 tracked.fireMode=false;tracked.fire[RightHand]=true;
 auto rounds=modes.ordnance.size();input(modes,1,tracked);assert(modes.ordnance.size()==rounds+1);
 for(int tick=0;tick<48;++tick)input(modes,1,tracked);
 assert(modes.ordnance.size()==rounds+1);
 tracked.fire[RightHand]=false;input(modes,1,tracked);
 tracked.fire[RightHand]=true;input(modes,1,tracked);
 assert(modes.ordnance.size()==rounds+2);
 // A timed blade guard rejects a close strike on its actual path.
 auto parry=fixture();auto &duelist=parry.member(1);duelist.sheet.equipment[LeftHand]=Gear::Blade;
 duelist.body.chassisYaw=duelist.body.torsoYaw=0;
 tracked.fire.fill(false);tracked.handPoses[LeftHand].position={-.05f,-.13f,-.70f};
 input(parry,1,tracked);
 assert(duelist.body.arms[LeftHand].tracked);
 const auto wrist=duelist.body.arms[LeftHand].wrist;
 const float laneX=std::clamp(wrist.x-duelist.body.position.x,-1.8f,1.8f)+duelist.body.position.x;
 const float laneY=std::clamp(wrist.y-duelist.body.position.y,1.f,11.f)+duelist.body.position.y;
 Shot slash;slash.meleeStrike=true;slash.body.owner=0;
 slash.body.position={laneX,laneY,duelist.body.position.z-7.f};slash.body.velocity={0,0,110};
 slash.body.damage=5;slash.body.life=1;
 auto unguarded=parry;unguarded.ordnance.push_back(slash);input(unguarded,1,tracked,.08f);
 const auto defence=duelist.sheet.ap+duelist.sheet.hp;
 assert(unguarded.member(1).sheet.ap+unguarded.member(1).sheet.hp<defence);
 duelist.parryWindow[LeftHand]=.30f;parry.ordnance.push_back(slash);input(parry,1,tracked,.08f);
 assert(duelist.riposteRemaining>0&&duelist.sheet.ap+duelist.sheet.hp==defence);
 assert(parry.world.targets[0].ai.combatMotion.action==EnemyAction::Stagger);
 // Two linked hits from opposite hands build a combo against the same target.
 auto chain=fixture();auto &sword=chain.member(1);
 sword.sheet.equipment[LeftHand]=sword.sheet.equipment[RightHand]=Gear::Blade;
 input(chain,1,tracked);
 auto &dummy=chain.world.targets[0];dummy.active=true;dummy.health=100;
 dummy.center=(sword.body.arms[LeftHand].muzzle+sword.body.arms[RightHand].muzzle)*.5f+Vec3{0,0,-5};
 dummy.half={6,4,1.5f};dummy.origin=dummy.center-Vec3{0,4,0};
 dummy.ai.boundsReady=false;chain.enemies[0].hp=chain.enemies[0].maxHp=100;
 tracked.punch[LeftHand]=true;input(chain,1,tracked);assert(sword.comboCount==1);
 tracked.punch={false,true};input(chain,1,tracked);assert(sword.comboCount==2);
 tracked.punch.fill(false);
 // Sword-equipped pilots show a guard before intercepting a frontal attack.
 auto guarded=chain;auto &guard=guarded.enemies[0];auto &defender=guarded.world.targets[0];
 auto ace=std::ranges::find_if(enemyCatalog(),[](const auto &spec){return std::string_view(spec.id)=="mecha_ace";});
 assert(ace!=enemyCatalog().end());guard.spec=int(ace-enemyCatalog().begin());
 defender.mesh.reset(); // Keep the test target's hand-aligned bounds during AI movement.
 defender.center=defender.center-direction(sword.body.facing(),0)*3.f;
 defender.origin={defender.center.x,0,defender.center.z};
 guard.disabled=0;guard.tactic=2;guard.fireClock=10;
 guarded.member(1).body.cooldown.fill(0);
 guarded.member(2).sheet.hidden=true;guarded.member(2).hiddenRemaining=20;
 // Let the tactical planner refresh its previously disabled actor.
 for(int i=0;i<24;++i)input(guarded,1,tracked);
 guard.fireClock=.73f;
 input(guarded,1,tracked,.02f);
 // An elevated target can settle during setup. Give the attacker time to
 // finish its last rush step before checking the frontal guard.
 for(int i=0;i<6&&guard.duel.phase==DuelPhase::Rush;++i)input(guarded,1,tracked);
 assert(defender.ai.combatMotion.action==EnemyAction::Guard);
 for(int i=0;i<5;++i)input(guarded,1,tracked);
 auto &challenger=guarded.member(1);
 const auto &blade=challenger.body.arms[LeftHand];
 defender.center=blade.muzzle+blade.forward*4.f;
 defender.half={3,3,2};
 defender.origin={defender.center.x,0,defender.center.z};
 defender.yaw=wrap(challenger.body.facing()+pi);
 assert(guarded.world.traceAim(blade.muzzle,blade.forward,10).target==0);
 const int beforeGuard=guard.hp;
 tracked.punch[LeftHand]=true;input(guarded,1,tracked);
 assert(defender.ai.combatMotion.action==EnemyAction::Parry&&guard.hp==beforeGuard);
 tracked.punch.fill(false);
 // A real ranged impact disables only the addressed subsystem.
 auto critical=fixture();auto &enemy=critical.enemies[0];auto &hull=critical.world.targets[0];
 enemy.hp=enemy.maxHp=100;hull.active=true;hull.health=100;hull.yaw=.65f;hull.origin={0,0,-30};
 hull.ai.boundsReady=true;hull.ai.localCenter={0,4,0};hull.ai.localHalf={3,4,3};critical.world.updateEnemyBounds(hull);
 const auto optic=weakPointPosition(hull,WeakPoint::Optics);
 const auto ray=unit(hull.center-optic);
 Shot precision;precision.body.shooter=1;
 precision.body.position=optic-ray*3;precision.body.velocity=ray*180;precision.body.damage=14;precision.body.life=.6f;
 critical.ordnance.push_back(precision);critical.step(.05f);
 assert(hasInjury(enemy.injuries,MechaInjury::Targeting)&&enemy.missingArms==0&&enemy.hp<86);
 const auto arm=weakPointPosition(hull,WeakPoint::LeftArm);
 precision.body.position=arm-unit(hull.center-arm)*3;precision.body.velocity=unit(hull.center-arm)*180;
 critical.ordnance.push_back(precision);critical.step(.05f);
 assert(enemy.missingArms==1&&critical.view(1).targets[0].rpgEnemyMissingArms==1);
 // Native pose samples feed the Blender preview, keeping one source of transforms.
 Json poseExport=Json::object();auto rigs=mechaPlayerRigFixture(std::filesystem::path(MECHA_ASSETS)).at("rigs");
 const std::array<std::string,9> roles={"rigid","torso","left-hip","left-knee","left-foot","right-hip","right-knee","right-foot","head"};
 for(const char *name:{"chassis_brawler","chassis_scout","chassis_striker","chassis_titan","chassis_brawler_alt","chassis_scout_alt","chassis_striker_alt","chassis_titan_alt"}){
  auto data=rigs.at(name);WalkerRig rig;rig.floor=data.at("floor");for(auto &p:data.at("parts")){WalkerPart part;part.parent=p.at("parent");part.arm=p.value("arm",-1);part.joint=WalkerJoint(std::find(roles.begin(),roles.end(),p.at("role").get<std::string>())-roles.begin());part.pivot=vector(p.at("pivot"));part.low=vector(p.at("low"));part.high=vector(p.at("high"));part.empty=p.at("faces").empty();rig.parts.push_back(part);}
  float standingHigh=0;for(const auto &part:rig.parts)standingHigh=std::max(standingHigh,part.high.y);
  Json samples=Json::array();for(int frame=0;frame<=48;++frame){float blend=frame/48.f;auto pose=poseVehicle(rig,blend);float low=1e9f,high=-1e9f;Json parts=Json::array();
   for(size_t i=0;i<rig.parts.size();++i){auto &p=pose.parts[i];assert(std::isfinite(p.offset.y));for(float x:{rig.parts[i].low.x,rig.parts[i].high.x})for(float y:{rig.parts[i].low.y,rig.parts[i].high.y})for(float z:{rig.parts[i].low.z,rig.parts[i].high.z}){auto point=animatedPoint(p,{x,y,z});low=std::min(low,point.y);high=std::max(high,point.y);}parts.push_back({{"q",{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},{"offset",{p.offset.x,p.offset.y,p.offset.z}}});}
   if(frame==48)assert(high<standingHigh*.75f);
   assert(low>=rig.floor-.0001f&&std::abs(low-rig.floor)<.001f);samples.push_back({{"frame",frame+1},{"blend",blend},{"parts",parts}});
  }poseExport[name]=samples;
 }
 if(argc==3&&std::string(argv[1])=="--poses"){std::ofstream f(argv[2]);f<<poseExport.dump();assert(bool(f));}
 std::cout<<"Mount/reticle gates, profile refitting, passive pilots and rejected secondary abilities, trade, contract rewards and vehicle controls/392 grounded poses passed\n";
}
