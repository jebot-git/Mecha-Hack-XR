#include "rpg/enemy_tactics.hpp"
#include "rpg/weapon_audio.hpp"
#include "rpg/kaiju_audio.hpp"
#include "net/snapshot_packet.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace iron;using namespace iron::rpg;
namespace {
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int index(std::string_view id){for(size_t i=0;i<enemyCatalog().size();++i)if(id==enemySpec(i).id)return int(i);throw std::runtime_error("Missing enemy fixture");}
Session fixture(int count=1,const char *id="anti_mecha_hovertank"){
 Session s(17);s.add(11,"Left");s.add(22,"Right");s.world.rpgPrototype=false;
 for(auto &m:s.members){m.sheet.hp=m.sheet.maxHp=100;m.body.lifeSpent=false;m.hiddenRemaining=0;}
 s.members[0].body.position={-10,0,45};s.members[1].body.position={10,0,45};
 s.enemies.resize(size_t(count));s.world.targets.resize(size_t(count));
 for(int i=0;i<count;++i){s.enemies[size_t(i)].spec=index(id);auto &b=s.world.targets[size_t(i)];b.origin={float(i)*8-float(count-1)*4,0,0};b.center=b.origin+Vec3{0,6,0};b.half={2,4,2};b.spawnType=3;b.active=true;b.health=100;}
 return s;
}
Target wall(bool structural){Target t;t.center={0,8,20};t.half={40,10,2};t.origin=t.center;t.invulnerable=structural;return t;}
Target panel(Vec3 centre,Vec3 a,Vec3 b){
 Target t;t.origin=t.center=centre;
 t.half={std::abs(a.x)+std::abs(b.x),std::abs(a.y)+std::abs(b.y),std::abs(a.z)+std::abs(b.z)};
 t.mesh=std::make_shared<WorldMesh>();
 for(auto v:{a+b,b-a,(a+b)*(-1),a+b,(a+b)*(-1),a-b})t.mesh->push_back({v,{}});
 return t;
}
bool hasMovementCue(const World &world,CombatCue cue){
 return std::any_of(world.sounds.begin(),world.sounds.end(),[&](const SoundEvent &event){
   return event.kind==SoundEvent::Special && event.weapon==int(cue);
 });
}
void checkCreatureMotors(){
 using Motor=EnemyMotor;
 const std::map<std::string_view,Motor> roster={
   {"kaiju_spawnling",Motor::CreatureCrawl},{"kaiju_flea",Motor::CreatureCrawl},
   {"kaiju_baron",Motor::CreatureCrawl},{"kaiju_drone",Motor::CreatureCrawl},
   {"kaiju_broodqueen",Motor::CreatureCrawl},{"kaiju_dismantler",Motor::CreatureCrawl},
   {"kaiju_stalker",Motor::CreatureCrawl},{"kaiju_tunnel_wurm",Motor::CreatureCrawl},
   {"abyssal_kaiju",Motor::CreatureCrawl},{"kaiju_tyrant",Motor::CreatureWing},
   {"abomination",Motor::Creature},{"kaiju_obliterator",Motor::Creature}};
 size_t classified=0;
 for(const auto &spec:enemyCatalog()){
   if(spec.kind!=EnemyKind::SmallKaiju&&spec.kind!=EnemyKind::LargeKaiju)continue;
   auto host=fixture(1,spec.id);host.world.rpgPrototype=true;
   const int actor=host.enemies[0].spec;
   host.mission.recipe.enemies={actor};
   Session guest;
   guest.readSnapshot(coop::decodeStatePacket(coop::encodeSnapshotPacket(host.snapshot())));
   const auto expected=roster.at(spec.id);
   require(host.view(11).targets[0].ai.rpgMotor==expected &&
           guest.view(22).targets[0].ai.rpgMotor==expected,"Kaiju roster or replicated locomotion changed");
   ++classified;
 }
 require(classified==roster.size(),"Obsolete kaiju in locomotion coverage");
 auto pursuit=fixture(1,"kaiju_flea");pursuit.world.rpgPrototype=true;pursuit.phase=Phase::Combat;
 auto &runner=pursuit.enemies[0];runner.tactic=1;runner.burrowReady=false;
 pursuit.mission.recipe.enemies={runner.spec};pursuit.mission.openerTime=100;
 auto &body=pursuit.world.targets[0];body.origin={0,0,-22};applyOriginalEnemy(body,runner.spec);
 pursuit.members[0].body.position={};pursuit.members[1].sheet.hidden=true;
 bool heard=false;
 for(int tick=0;tick<240&&!heard;++tick){
   const auto before=body.origin;
   pursuit.step(1.f/60);
   require(!hasMovementCue(pursuit.world,CombatCue::EnemyStep),"Kaiju emitted a mechanical footfall");
   if(hasMovementCue(pursuit.world,CombatCue::KaijuSkitter)){
     require(length(body.origin-before)>.001f,"Stationary creature emitted a ground contact");
     heard=true;
   }
 }
 require(heard,"Moving flea did not make an audible foot contact");
 auto abyss=fixture(1,"abyssal_kaiju");
 abyss.world.waterRegions.push_back({{-12,-12,12,12},9,9,0,{}});
 abyss.biome=2;
 require(abyss.view(11).targets[0].ai.rpgMotor==Motor::CreatureSwim,"Swimming Abyssal lost water strokes");
 abyss.biome=0;
 require(abyss.view(11).targets[0].ai.rpgMotor==Motor::CreatureCrawl,"Wading Abyssal sounds like open-water swimming");
 auto finale=fixture(1,"kaiju_broodqueen");finale.world.rpgPrototype=true;
 auto &boss=finale.enemies[0];
 const auto bosses=campaignBosses();
 for(size_t i=0;i<bosses.size();++i)if(std::string_view(bosses[i].spec.id)=="gravesong_sovereign")boss.boss=int(i);
 require(boss.boss>=0,"Missing Sovereign fixture");
 auto &recipe=finale.mission.recipe;
 recipe.finale=true;recipe.finaleBattle=false;recipe.boss=boss.boss;recipe.enemies={boss.spec};
 recipe.maps=1;recipe.objective=0;recipe.siteCount=0;
 Session guest;
 guest.readSnapshot(coop::decodeStatePacket(coop::encodeSnapshotPacket(finale.snapshot())));
 require(finale.view(11).targets[0].ai.rpgMotor==Motor::CreatureCrawl&&
         guest.view(22).targets[0].ai.rpgMotor==Motor::CreatureCrawl,"Sovereign lost its brood gait");
}
}
int main(){try{
 checkCreatureMotors();
 require(enemyUnitsCompatible(enemySpec(index("aegis_mecha")),enemySpec(index("gladius_mecha"))),"Source pair cannot cooperate");
 require(enemyUnitsCompatible(enemySpec(index("kaiju_spawnling")),enemySpec(index("kaiju_broodqueen"))),"Kaiju brood cannot cooperate");
 require(!enemyUnitsCompatible(enemySpec(index("kaiju_spawnling")),enemySpec(index("mecha_recruit"))),"Biological and mechanical enemies share squad intent");
 require(!enemyUnitsCompatible(enemySpec(index("labor_bot")),enemySpec(index("mecha_recruit"))),"Different mechanical factions share squad intent");
 {
  auto s=fixture();s.members[1].sheet.hp=10;
  auto p=planEnemyTactics(s);require(p[0].targetId==22,"Equal-distance exposed wounded target not prioritized");
  s.members[1].sheet.hidden=true;p=planEnemyTactics(s);require(p[0].targetId==11,"Hidden target selected");
  s.members[0].ejected=true;p=planEnemyTactics(s);require(p[0].memberIndex<0,"No visible targets still produces target");
  s.members[0].ejected=false;s.members[0].sheet.hp=0;require(planEnemyTactics(s)[0].memberIndex<0,"Dead target selected");
 }
 {
  auto s=fixture();std::array<uint32_t,1> old{22};
  s.members[0].body.position.x=-9.8f;
  require(planEnemyTactics(s,old)[0].targetId==22,"Tiny distance changes cause target oscillation");
  s.members[1].body.position={100,0,180};
  require(planEnemyTactics(s,old)[0].targetId==11,"Hysteresis retained a clearly inferior distant target");
  s.members[1].body.position={10,0,45};s.members[0].body.wasFire[0]=true;
  require(planEnemyTactics(s)[0].targetId==11,"Observed active firing is not prioritized");
 }
 {
  auto s=fixture();s.world.targets.push_back(wall(true));
  require(!enemyCanSeeMember(s,0,s.members[0]),"Structural wall does not block acquisition");
  require(planEnemyTactics(s)[0].memberIndex<0,"Enemy acquires target through structural wall");
  require(!enemyFiringLaneClear(s,0,s.members[0].body.position+Vec3{0,6,0}),"Structural wall does not block firing lane");
  s.world.targets.back().invulnerable=false;
  require(!enemyCanSeeMember(s,0,s.members[0]),"Ranged-only tank sees through destructible wall");
  s.enemies[0].spec=index("mecha_recruit");
  require(enemyCanSeeMember(s,0,s.members[0]),"Breacher lost destructible-cover contact");
  require(!enemyFiringLaneClear(s,0,s.members[0].body.position+Vec3{0,6,0}),"Breacher fires through destructible cover");
  require(enemyFiringLaneClear(s,0,s.world.targets.back().center,1),"Selected breach obstruction blocks its own shot");
 }
 {
  auto s=fixture(2);s.world.targets[1].origin={-7,0,22};s.world.targets[1].center={-7,6,22};s.world.targets[1].half={3,5,3};
  require(enemyCanSeeMember(s,0,s.members[0]),"Ally incorrectly hides visual target");
  require(!enemyFiringLaneClear(s,0,s.members[0].body.position+Vec3{0,6,0}),"Ally does not block firing lane");
  s.enemies[1].hp=0;s.world.targets[1].health=0;
  require(enemyFiringLaneClear(s,0,s.members[0].body.position+Vec3{0,6,0}),"Destroyed ally still blocks fire");
 }
 {
  auto s=fixture(2,"mecha_sniper");s.members[1].sheet.hidden=true;
  s.world.targets[0].origin={0,0,0};s.world.targets[0].center={0,6,0};
  s.world.targets[1].origin={0,0,22};s.world.targets[1].center={0,6,22};
  s.world.targets[1].half={3,5,3};s.members[0].body.position={0,0,60};
  s.enemies[0].tactic=8;s.enemies[1].disabled=100;
  auto intent=planEnemyTactics(s)[0];
  require(!intent.fireLaneClear&&intent.positioning&&!intent.coordinated&&intent.partner<0,"Lone precision shooter cannot clear a disabled ally's firing obstruction");
  require(std::abs(intent.destination.x)>7&&std::abs(intent.destination.z)<.001f,"Independent blocked-lane intent is not a lateral sidestep");
  s.enemies[1].disabled=0;s.enemies[1].spec=index("kaiju_spawnling");
  intent=planEnemyTactics(s)[0];
  require(intent.positioning&&!intent.coordinated,"Incompatible ally incorrectly prevents independent lane recovery");
  for(int tactic:{2,9}){s.enemies[0].tactic=tactic;require(!planEnemyTactics(s)[0].positioning,"Independent recovery overrides guard or untriggered ambush doctrine");}
  s.enemies[0].tactic=8;s.world.targets[1].active=false;s.enemies[1].hp=0;
  intent=planEnemyTactics(s)[0];
  require(intent.fireLaneClear&&!intent.positioning,"Clear lane overrides stationary precision hold");
 }
 {
  auto s=fixture(6);for(size_t i=0;i<s.enemies.size();++i){auto &b=s.world.targets[i];b.origin={float(i)*4-10,0,-float(i%2)*5};b.center=b.origin+Vec3{0,6,0};}
  auto p=planEnemyTactics(s);int left=0,right=0;
  for(auto &d:p){left+=d.targetId==11;right+=d.targetId==22;}
  require(std::max(left,right)>=4,"Local attackers never concentrate fire");
  require(std::min(left,right)>=1,"Focus saturation piles entire squad onto one target");
  for(auto &d:p){
   int sharing=d.targetId==11?left:right;
   require(d.coordinated==(sharing>1),"Coordination does not match shared local contact");
   require((d.partner>=0)==(sharing>1),"Partner assigned without another same-contact attacker");
  }
  s.members[1].sheet.hidden=true;p=planEnemyTactics(s);
  for(auto &d:p)require(d.targetId==11,"Focus capacity prevents engaging only visible target");
  require(length(p.front().destination-p.back().destination)>5,"Ranged squad collapses onto same firing position");
  auto again=planEnemyTactics(s);for(size_t i=0;i<p.size();++i)require(p[i].targetId==again[i].targetId&&length(p[i].destination-again[i].destination)<.0001f,"Planner is nondeterministic");
 }
 {
  auto s=fixture(2,"kaiju_spawnling");s.members[1].sheet.hidden=true;
  auto p=planEnemyTactics(s);
  require(p[0].positioning&&p[1].positioning,"Melee cooperative approach missing");
  require(length(p[0].destination-p[1].destination)>3,"Melee units share same approach slot");
  require(length(p[0].destination-s.members[0].body.position)<=10,"Melee formation cannot reach target");
  s.enemies[1].spec=index("mecha_recruit");p=planEnemyTactics(s);
  require(!p[0].coordinated&&!p[1].coordinated,"Incompatible actors synchronize positioning");
  s.enemies[1].spec=index("kaiju_spawnling");s.world.targets[1].origin.x=400;s.world.targets[1].center.x=400;p=planEnemyTactics(s);
  require(!p[0].coordinated,"Squad shares target intent over unlimited distance");
 }
 {
  auto s=fixture(2,"ion_emplacement");s.members[1].sheet.hidden=true;
  auto p=planEnemyTactics(s);require(!p[0].positioning&&!p[1].positioning,"Static installation assigned moving formation");
 }
 {
  auto s=fixture(2,"kaiju_broodqueen");s.members[1].sheet.hidden=true;
  s.enemies[1].spec=index("kaiju_spawnling");auto p=planEnemyTactics(s);
  require(p[0].positioning&&p[0].coordinated,"Brood carrier does not cooperate with offspring");
  require(length(p[0].destination-s.members[0].body.position)>15,"Brood carrier charges into offspring melee slot");
  require(length(p[1].destination-s.members[0].body.position)<=10,"Brood offspring inherited support standoff");
 }
 {
  auto s=fixture(1,"kaiju_flea");s.world.rpgPrototype=true;s.phase=Phase::Combat;
  auto &pilot=s.members[0];auto &enemy=s.enemies[0];auto &body=s.world.targets[0];
  s.members[1].sheet.hidden=true;pilot.body.position={};pilot.body.chassisYaw=pilot.body.torsoYaw=0;
  enemy.hp=enemy.maxHp=40;enemy.tactic=1;
  s.mission.recipe.enemies={enemy.spec};s.mission.openerTime=100;
  body.origin={0,0,-18};applyOriginalEnemy(body,enemy.spec);
  for(int tick=0;tick<240&&!enemy.grapplePilot;++tick)s.step(1.f/60);
  require(enemy.grapplePilot==pilot.id,"Melee Voidmaw failed to pursue and latch onto the pilot");
  int canopyImpacts=0;
  for(int tick=0;tick<24;++tick){
    s.step(1.f/60);
    for(const auto &sound:s.world.sounds)
      if(sound.kind==SoundEvent::Special&&sound.weapon==int(CombatCue::KaijuImpact))++canopyImpacts;
  }
  require(canopyImpacts==1,"Grapple failed to strike the canopy exactly once on attachment");
  const auto held=body.origin-pilot.body.position;
  pilot.body.position.x+=6;
  s.step(1.f/60);
  require(length(body.origin-pilot.body.position-held)<.001f,"Grappler failed to follow the moving canopy");
  auto view=s.view(pilot.id,{});
  require(view.traceAim(view.eye(),{0,0,-1}).target==0,"Canopy grappler cannot be targeted from the cockpit");
  Session guest;
  guest.readSnapshot(coop::decodeStatePacket(coop::encodeSnapshotPacket(s.snapshot())));
  auto joined=guest.view(pilot.id);
  require(joined.rpgCanopyAttacker==0&&guest.view(s.members[1].id).rpgCanopyAttacker==-1&&
          joined.targets[0].ai.rpgGrapplePilot==pilot.id&&joined.targets[0].ai.voidmawPose.phase==VoidmawPose::Cling&&
          guest.enemies[0].grappleSide==enemy.grappleSide&&guest.enemies[0].grappleAt==enemy.grappleAt,
          "First combat snapshot lost the canopy owner, side or attachment time");
  const auto baseline=coop::captureScenery(s.world,s.enemies.size());
  const int durability=pilot.sheet.hp+pilot.sheet.ap;
  for(int tick=0;tick<120&&pilot.sheet.hp+pilot.sheet.ap==durability;++tick)s.step(1.f/60);
  require(pilot.sheet.hp+pilot.sheet.ap<durability,"Attached creature never landed a bite");
  const auto jawOrientation=compose(body.ai.gravity.frame,angles(body.yaw,body.ai.pitch,body.ai.bank));
  const auto jaw=body.origin+rotate(jawOrientation,enemy.grappleMuzzle);
  require(std::count_if(s.world.sounds.begin(),s.world.sounds.end(),[&](const SoundEvent &event){
    return event.kind==SoundEvent::Special&&event.weapon==int(CombatCue::KaijuBite)&&length(event.position-jaw)<.01f;
  })==1,"Canopy damage did not emit exactly one bite at the jaw");
  guest.readSnapshot(coop::decodeStatePacket(coop::encodeSnapshotPacket(s.snapshot(&baseline))),&baseline.rows);
  joined=guest.view(pilot.id);
  const auto &bite=joined.targets[0].ai.combatMotion;
  require(bite.action==EnemyAction::Strike&&bite.started==body.ai.combatMotion.started&&
          bite.active(joined.elapsed+.05f)&&guest.member(pilot.id).sheet.hp==pilot.sheet.hp,
          "Sparse update or render lead lost authoritative bite timing");
  auto shoot=[&](uint32_t shooter,int damage,Vec3 from){
    Shot shot;shot.body.position=from;shot.body.velocity=unit(body.center-from)*1100;
    shot.body.damage=float(damage);shot.body.shooter=int(shooter);shot.body.owner=-1;shot.body.life=.5f;
    shot.quality=Quality::Plasma;s.ordnance.push_back(shot);
    s.step(1.f/60);
  };
  shoot(pilot.id,2,view.eye());
  require(enemy.grapplePilot==pilot.id,"A glancing hit broke a full-strength grapple");
  shoot(s.members[1].id,5,body.center+Vec3{5,0,0});
  require(!enemy.grapplePilot&&!body.ai.rpgGrapplePilot&&enemy.hp==33&&enemy.grappleCooldown>0,
          "Cumulative ally damage did not dislodge the surviving grappler");
  guest.readSnapshot(s.snapshot(&baseline),&baseline.rows);
  require(guest.view(pilot.id).rpgCanopyAttacker==-1&&guest.world.targets[0].ai.voidmawPose.phase==VoidmawPose::Recover,
          "Sparse release retained a cockpit attachment");
 }
 {
  auto s=fixture(1,"kaiju_spawnling");s.mission.recipe.enemies={s.enemies[0].spec};
  s.world.elapsed=10;s.world.rpgPrototype=true;
  auto &cue=s.world.targets[0].ai.voidmawPose;
  const auto baseline=coop::captureScenery(s.world,s.enemies.size());
  Session remote;
  for(auto phase:{VoidmawPose::Pursuit,VoidmawPose::Dodge,VoidmawPose::Coil,VoidmawPose::Tackle,
                  VoidmawPose::Recover,VoidmawPose::Lumber,VoidmawPose::Brace,VoidmawPose::Discharge,VoidmawPose::SiegeRecover}){
    cue={phase,9.75f,.6f,-1.f};remote.readSnapshot(s.snapshot(&baseline),&baseline.rows);
    const auto received=remote.world.targets[0].ai.voidmawPose;
    require(received.phase==phase&&received.started==cue.started&&received.duration==cue.duration&&received.side==cue.side,
            "Guest lost a Voidmaw combat phase");
  }
  auto invalid=s.snapshot();invalid["enemies"][0]["voidmaw_pose"][3]=0;
  bool rejected=false;try{remote.readSnapshot(invalid);}catch(const std::runtime_error &){rejected=true;}
  require(rejected,"Guest accepted an invalid Voidmaw direction");
  s.enemies[0].spec=index("anti_mecha_hovertank");s.mission.recipe.enemies={s.enemies[0].spec};cue={};
  auto idle=s.snapshot(&baseline);
  require(!idle.at("enemies")[0].contains("voidmaw_pose"),"Ordinary enemy was charged for a Voidmaw wire record");
  remote.readSnapshot(idle,&baseline.rows);
  require(remote.world.targets[0].ai.voidmawPose.phase==VoidmawPose::Default,
          "Absent pose left a stale attack on a normal enemy");
 }
 {
  auto ground=fixture(1,"kaiju_spawnling");
  ground.world.rpgPrototype=true;ground.phase=Phase::Combat;ground.biome=5;
  auto &enemy=ground.enemies[0];auto &body=ground.world.targets[0];
  ground.members[0].body.position={};ground.members[1].sheet.hidden=true;
  enemy.burrowReady=canBurrow(enemySpec(enemy.spec));enemy.tactic=1;
  ground.mission.recipe.enemies={enemy.spec};ground.mission.openerTime=100;
  body.origin={0,0,-22};applyOriginalEnemy(body,enemy.spec);
  auto inside=ground;
  inside.world.interiorRegions.push_back({{-35,-35,35,35},0,30});
  ground.step(.02f);
  inside.step(.02f);
  require(enemy.burrowImpact&&!body.active&&std::abs(body.origin.x)>3,
          "Small melee Voidmaw did not burrow toward a flank outdoors");
  require(!inside.enemies[0].burrowImpact&&inside.world.targets[0].active,
          "Small melee Voidmaw burrowed through a solid interior floor");
 }
 {
  auto s=fixture(1,"kaiju_tunnel_wurm");s.world.rpgPrototype=true;s.phase=Phase::Combat;
  auto &pilot=s.members[0];pilot.body.position={};pilot.body.chassisYaw=pilot.body.torsoYaw=0;
  auto &dodging=s.members[1];dodging.body.position={20,0,0};
  auto &enemy=s.enemies[0];auto &body=s.world.targets[0];
  body.origin={0,0,-14};applyOriginalEnemy(body,enemy.spec);
  enemy.tactic=1;enemy.burrowReady=true;enemy.hp=enemy.maxHp=72;
  s.mission.recipe.enemies={enemy.spec};s.mission.openerTime=100;
  s.world.elapsed=1;
  s.step(1.f/60);
  require(enemy.burrowImpact&&!body.active,"Tunnel Wurm did not start a submerged ambush");
  require(hasMovementCue(s.world,CombatCue::KaijuBurrow)&&hasMovementCue(s.world,CombatCue::ArrivalBurrow),
          "Wurm dive lost its body movement or destination warning");
  unsigned eruptions=0;
  for(int tick=0;tick<42;++tick){
    s.step(1.f/60);
    if(hasMovementCue(s.world,CombatCue::KaijuBurrow)){
      require(body.active,"Wurm played emergence before surfacing");
      ++eruptions;
    }
  }
  require(eruptions==1,"Wurm emergence must sound once per resurfacing");
  require(body.active&&pilot.body.rpgKnockdown.active(s.world.elapsed)&&
          !dodging.body.rpgKnockdown.active(s.world.elapsed),"Burrow emergence failed its hit and dodge conditions");
  s.world.elapsed+=.6f;
  const auto standing=pilot.body.position;
  pilot.inputAge=0;
  pilot.input.forward=1;pilot.input.steer=.8f;pilot.input.turn=.6f;
  s.step(1.f/60);
  require(length(pilot.body.position-standing)<.001f&&std::abs(pilot.body.facing())<.001f,
          "Knocked-down pilot can walk or steer during recovery");
  const auto view=s.view(pilot.id,{});
  require(view.eye().y<6&&std::abs(view.knockdownPose().roll)>1.f,
          "Burrow hit lacks the full cockpit drop and roll");
  const auto expected=rotate(view.knockdownPose().orientation,{-2.5f,-1.2f,-.1f});
  require(pilot.body.arms[0].tracked&&length(pilot.body.arms[0].shoulder-view.eye()-expected)<.001f,
          "Controller arm remains upright in the fallen cockpit");
  auto guest=s;guest.readSnapshot(s.snapshot());
  const auto remote=guest.view(pilot.id,{});
  require(length(remote.eye()-view.eye())<.001f&&std::abs(remote.knockdownPose().roll-view.knockdownPose().roll)<.001f,
          "Co-op knockdown camera differs from the authority");
  s.world.elapsed+=3;
  require(std::abs(s.view(pilot.id,{}).eye().y-12)<.001f,"Cockpit failed to recover after the knockdown");
 }
 {
  World world;
  world.targets.clear();
  world.targets.push_back(panel({0,14,-12},{15,0,0},{0,14,5}));
  SurfaceGravity gravity;
  Vec3 feet{};
  const auto hit=gravityRay(world,{0,12,0},{0,12,-30});
  require(hit&&hit->normal.y<-.2f&&hit->normal.z>.9f,"Gravity contact ignored its angled collision mesh");
  require(beginGravityPull(world,gravity,feet,*hit,13,3.5f),"Angled surface refused a clear transfer");
  for(int tick=0;tick<90;++tick)advanceGravity(world,gravity,feet,13,3.5f,1.f/60);
  require(gravity.mode==GravityMode::Attached&&dot(gravity.up(),hit->normal)>.999f,"Gravity did not align to the angled face");
  require(moveOnGravity(world,gravity,feet,{2,0,0},13,3.5f)&&feet.x>1.9f,"Attached pilot cannot traverse the surface");
  world.targets.push_back(panel({8,14,0},{0,0,24},{0,14,0}));
  world.targets[0].health=0;
  advanceGravity(world,gravity,feet,13,3.5f,1.f/60);
  require(gravity.mode==GravityMode::Floating,"Destroyed support left the pilot attached");
  for(int tick=0;tick<100;++tick)advanceGravity(world,gravity,feet,13,3.5f,1.f/60);
  require(gravity.mode==GravityMode::Attached&&gravity.up().x<-.99f&&std::abs(feet.x-7.85f)<.03f,
          "Floating pilot did not acquire the nearest surviving face");
 }
 {
  auto s=fixture(1,"mecha_recruit");s.world.rpgPrototype=true;s.phase=Phase::Combat;s.biome=1;
  auto &pilot=s.members[0];
  pilot.body.position={};pilot.body.chassisYaw=pilot.body.torsoYaw=0;
  s.members[1].body.position={50,0,50};
  s.enemies[0].disabled=100;s.world.targets[0].origin={35,0,-30};
  s.mission.recipe.enemies={s.enemies[0].spec};s.mission.openerTime=100;
  s.world.targets.push_back(panel({0,20,-7},{20,0,0},{0,20,0}));
  pilot.thrusters.charge=4;pilot.inputAge=0;s.step(1.f/60);
  pilot.input.forward=1;pilot.input.thrust=true;
  for(int tick=0;tick<90;++tick){pilot.inputAge=0;s.step(1.f/60);}
  require(pilot.body.gravity.mode==GravityMode::Attached&&pilot.body.gravity.up().z>.99f,
          "Space boost did not switch the pilot to the wall");
  const auto view=s.view(pilot.id,{});
  require(length(view.eye()-pilot.body.position-pilot.body.gravity.up()*12)<.001f&&
          dot(rotate(view.cockpitOrientation(),{0,1,0}),pilot.body.gravity.up())>.999f,
          "Cockpit and wall gravity disagree");
  auto guest=s;guest.readSnapshot(s.snapshot());
  require(length(guest.view(pilot.id,{}).eye()-view.eye())<.001f,"Co-op lost the new cockpit gravity frame");
 }
 {
  auto s=fixture(1,"mecha_recruit");s.world.rpgPrototype=true;s.phase=Phase::Combat;s.biome=3;
  auto &pilot=s.members[0];
  pilot.body.position={};pilot.body.chassisYaw=pilot.body.torsoYaw=0;
  s.members[1].body.position={50,0,50};
  s.enemies[0].disabled=100;s.world.targets[0].origin={35,0,-30};
  s.mission.recipe.enemies={s.enemies[0].spec};s.mission.openerTime=100;
  s.world.interiorRegions.push_back({{-22,-22,22,22},0,27});
  s.world.targets.push_back(panel({0,27,0},{22,0,0},{0,0,22}));
  pilot.sheet.equipment[LeftShoulder]=Gear::Grappler;pilot.sheet.attributes[0]=20;
  pilot.input.headPitch=1.45f;pilot.input.fire[LeftShoulder]=true;
  for(int tick=0;tick<120;++tick){pilot.inputAge=0;s.step(1.f/60);}
  require(pilot.body.gravity.mode==GravityMode::Attached&&pilot.body.gravity.up().y<-.99f,
          "Dungeon grapple could not attach upside down to a ceiling");
 }
 {
  auto s=fixture(1,"kaiju_flea");s.world.rpgPrototype=true;s.phase=Phase::Combat;s.biome=3;
  auto &enemy=s.enemies[0];auto &actor=s.world.targets[0];
  s.members[0].body.position={0,0,-19};s.members[1].sheet.hidden=true;
  actor.origin={0,0,-6};applyOriginalEnemy(actor,enemy.spec);
  enemy.tactic=1;enemy.hp=enemy.maxHp=40;
  s.mission.recipe.enemies={enemy.spec};s.mission.openerTime=100;
  s.world.targets.push_back(panel({0,20,-9},{20,0,0},{0,20,0}));
  for(int tick=0;tick<120&&actor.ai.gravity.mode!=GravityMode::Attached;++tick)s.step(1.f/60);
  require(actor.ai.gravity.mode==GravityMode::Attached&&actor.ai.gravity.up().z>.99f,
          "Small Voidmaw cannot scale a wall in ordinary gravity");
  auto guest=s;guest.readSnapshot(s.snapshot());
  require(guest.world.targets[0].ai.gravity.up().z>.99f,"Co-op lost the NPC attachment orientation");
  s.world.interiorRegions.push_back({{-20,-25,20,16},0,23});
  s.world.targets.push_back(panel({0,23,-5},{20,0,0},{0,0,20}));
  for(int tick=0;tick<540&&s.world.targets[0].ai.gravity.up().y>-.98f;++tick)s.step(1.f/60);
  require(s.world.targets[0].ai.gravity.up().y<-.98f,"Wall-climbing Voidmaw did not transfer to the dungeon ceiling");
 }
 {
  auto s=fixture(1,"mecha_recruit");s.world.rpgPrototype=true;s.phase=Phase::Combat;s.biome=2;
  auto &enemy=s.enemies[0];auto &actor=s.world.targets[0];
  s.members[0].body.position={12,0,-1};s.members[1].sheet.hidden=true;
  actor.origin={0,0,-7.5f};applyOriginalEnemy(actor,enemy.spec);
  enemy.hp=enemy.maxHp=40;enemy.thrusters.remaining=.32f;enemy.thrusters.velocity={0,0,-1};
  s.mission.recipe.enemies={enemy.spec};s.mission.openerTime=100;
  s.world.targets.push_back(panel({0,24,-11},{22,0,0},{0,24,0}));
  for(int tick=0;tick<120&&s.world.targets[0].ai.gravity.mode!=GravityMode::Attached;++tick)s.step(1.f/60);
  require(s.world.targets[0].ai.gravity.mode==GravityMode::Attached&&s.world.targets[0].ai.gravity.up().z>.99f,
          "NPC underwater boost did not attach to the bulkhead");
 }
 {
  auto s=fixture(1,"kaiju_stalker");s.world.rpgPrototype=true;s.phase=Phase::Combat;s.biome=1;
  auto &pilot=s.members[0];auto &enemy=s.enemies[0];auto &actor=s.world.targets[0];
  pilot.body.position={0,14,-21.85f};
  pilot.body.gravity.mode=GravityMode::Attached;
  pilot.body.gravity.frame=alignGravity({}, {0,0,1});
  s.members[1].body.position={80,0,65};
  actor.origin={};applyOriginalEnemy(actor,enemy.spec);
  enemy.hp=enemy.maxHp=64;
  s.mission.recipe.enemies={enemy.spec};s.mission.openerTime=100;
  s.world.targets.push_back(panel({0,21,-22},{25,0,0},{0,21,0}));
  for(int tick=0;tick<120&&s.world.targets[0].ai.gravity.mode!=GravityMode::Attached;++tick)s.step(1.f/60);
  require(s.world.targets[0].ai.gravity.mode==GravityMode::Attached&&s.world.targets[0].ai.gravity.up().z>.99f,
          "Grapple-equipped NPC did not reel itself onto the pilot's wall");
 }
 // Real combat steps exercise warning, release, physical projectiles and egress.
 auto attackFixture=[](const char *id,float elevation){
   auto s=fixture(1,id);
   s.world.rpgPrototype=true;s.phase=Phase::Combat;
   s.mission.recipe.opener=5;s.mission.openerTime=100;
   s.mission.recipe.enemies={s.enemies.front().spec};
   s.members[0].body.position={0,0,37};s.members[1].body.position={80,0,50};
   s.members[1].hiddenRemaining=1000;
   auto &actor=s.world.targets.front();actor.origin={0,elevation,0};actor.ai.airborne=elevation>0;
   applyOriginalEnemy(actor,s.enemies.front().spec);s.world.updateEnemyBounds(actor);
   s.enemies.front().fireClock=0;
   return s;
 };
 auto countEffects=[](const Session &s,unsigned id){
   return std::count_if(s.world.effects.begin(),s.world.effects.end(),[&](const auto &fx){return fx.model==id;});
 };
 {
   auto s=attackFixture("anti_mecha_hovertank",3.f);
   for(int tick=0;tick<60&&s.enemies[0].vehiclePass.phase!=VehiclePassPhase::Warning;++tick)s.step(.025f);
   require(countEffects(s,rpgVehicleVolleyWarning)==1&&s.ordnance.empty(),"Ground gunner shoots before its attack warning");
   const auto marked=s.enemies[0].vehiclePass.aim;
   s.members[0].body.position.x+=23;
   int fired=0;
   for(int tick=0;tick<90&&s.enemies[0].vehiclePass.phase!=VehiclePassPhase::Egress;++tick){
      s.step(.025f);
      fired+=std::count_if(s.ordnance.begin(),s.ordnance.end(),[](const auto &shot){
        return shot.body.kind==ProjectileKind::Rocket&&shot.body.life>2.77f&&shot.body.hull>0;
      });
   }
   require(fired==3&&length(s.enemies[0].vehiclePass.aim-marked)<.001f,
           "Ground gunner lacks three fixed-aim lanes or followed a dodging pilot");
   const auto before=s.world.targets[0].origin;
   const auto preGap=length(before-marked);
   for(int tick=0;tick<50;++tick)s.step(.025f);
   require(length(s.world.targets[0].origin-before)>8&&length(s.world.targets[0].origin-marked)>preGap+5,
           "Ground gunner did not disengage after firing");
   require(s.members[0].sheet.hp==100,"Successful lane dodge took damage");
 }
 {
   auto s=attackFixture("interceptor",19.f);
   std::vector<int> waves;
   for(int tick=0;tick<120&&s.enemies[0].vehiclePass.phase!=VehiclePassPhase::Egress;++tick){
      s.step(.02f);
      const int spawned=std::count_if(s.ordnance.begin(),s.ordnance.end(),[](const auto &shot){
        return shot.body.weapon==Weapon::Rail&&shot.body.life>2.77f;
      });
      if(spawned)waves.push_back(spawned);
   }
   require(waves==std::vector{3,2},"Interceptor did not emit staggered three/two pulse waves");
 }
 {
   auto s=attackFixture("bomber",32.f);
   for(int tick=0;tick<60&&s.enemies[0].vehiclePass.phase!=VehiclePassPhase::Warning;++tick)s.step(.025f);
   require(countEffects(s,rpgVehicleBombWarning)==3&&s.ordnance.empty(),
           "Bomber does not warn with three separated ground targets");
   auto dodge=s;dodge.members[0].body.position.x+=20;
   int drops=0;
   const auto begin=s.world.targets[0].origin;
   const auto health=s.members[0].sheet.hp+s.members[0].sheet.ap;
   for(int tick=0;tick<140;++tick){
      s.step(.025f);dodge.step(.025f);
      drops+=std::count_if(s.ordnance.begin(),s.ordnance.end(),[](const auto &shot){
        return shot.body.kind==ProjectileKind::Bomb&&shot.body.life>1.395f;
      });
   }
   require(drops==3&&s.world.targets[0].origin.z>begin.z+40,"Bomber did not release physical ordnance on a fly-through");
   require(s.members[0].sheet.hp+s.members[0].sheet.ap<health&&
           dodge.members[0].sheet.hp+dodge.members[0].sheet.ap==health,
           "Bomb lane does not distinguish standing in a marker from dodging out");
 }
 // Replay varied staging with the same intent state. This is regression-driven
 // tuning, not a learned model: deterministic host choices must survive replay.
 int scenarios=0;
 for(int seed=1;seed<=128;++seed){
  auto s=fixture(4);for(size_t i=0;i<s.enemies.size();++i){float shift=float((seed*17+int(i)*11)%21)-10;s.world.targets[i].origin.z=shift;s.world.targets[i].center.z=shift;}
  s.members[0].sheet.hp=20+seed%80;s.members[1].body.wasFire[0]=seed%2;
  std::array<uint32_t,4> old{11,11,22,22};auto a=planEnemyTactics(s,old);auto b=planEnemyTactics(s,old);
  for(size_t i=0;i<a.size();++i){require(a[i].targetId==b[i].targetId,"Replay changed target");require(std::isfinite(a[i].destination.x)&&std::isfinite(a[i].destination.z),"Formation produced nonfinite position");}
  s.members[0].sheet.hidden=true;auto hidden=planEnemyTactics(s,old);for(auto &d:hidden)require(d.targetId!=11,"Cached focus leaks hidden target in replay");++scenarios;
 }
 std::cout<<"Target scoring, geometry perception, faction compatibility, saturation, formation lanes and "<<scenarios<<" deterministic tuning scenarios verified\n";
 std::cout<<"Grapple and boost gravity transfers, small Voidmaw climbing, canopy orientation, destroyed support and co-op snapshots verified\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
