// Sustained authoritative AI audit; no renderer, profiles or network access.
#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include "searchlight.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
using namespace iron;using namespace iron::rpg;
static Json findings=Json::array();
static void check(std::string name,bool ok,Json evidence=Json::object()){findings.push_back({{"name",name},{"passed",ok},{"evidence",evidence}});}
static Session fixture(int spec,int tactic=8,int players=1,int count=1){
 Session s(713);for(int i=0;i<players;++i)s.add(uint32_t(i+1),"AI target "+std::to_string(i+1));
 GameSettings cfg;cfg.enemy=spec;cfg.enemyCount=count;cfg.environment=3;cfg.location=0;cfg.objective=0;cfg.opener=5;cfg.tactic=tactic;cfg.extent=500;
 s.command(1,{{"op","settings"},{"settings",cfg}});for(auto &m:s.members)s.command(m.id,{{"op","ready"},{"value",true}});s.command(1,{{"op","deploy"}});
 s.world.proceduralTerrain=false;s.world.editGeometry().terrain.clear();s.world.indexTerrain();s.world.interiorRegions.clear();s.world.waterRegions.clear();s.world.interiorNavNodes.clear();s.world.interiorNavLinks.clear();
 for(size_t i=s.enemies.size();i<s.world.targets.size();++i)s.world.targets[i].active=false;
 s.mission.recipe.environment=1;s.environmentCycle.startHour=8;s.environmentCycle.daySeconds=s.environmentCycle.weatherSeconds=1e9;
 for(size_t i=0;i<s.members.size();++i){auto &m=s.members[i];m.body.position={float(i)*40,0,20};m.sheet.hp=m.sheet.maxHp=1000000;m.input.focused=false;}
 for(size_t i=0;i<s.enemies.size();++i){auto &t=s.world.targets[i];t.origin={float(i)*25,0,0};t.yaw=0;s.world.updateEnemyBounds(t);s.enemies[i].anchorX=t.origin.x;s.enemies[i].anchorZ=t.origin.z;s.enemies[i].decisionClock=10000;s.enemies[i].fireClock=0;}
 return s;
}
static int freshShots(const Session &s,int owner=0){int n=0;for(const auto &shot:s.ordnance)if(shot.body.owner==owner&&shot.body.life>2.98f)++n;return n;}
static void position(Session &s,int index,Vec3 p){s.world.targets[index].origin=p;s.world.updateEnemyBounds(s.world.targets[index]);}
// A ten-second geometry soak needs an actual engagement opportunity. A
// generated deployment can legitimately put every weapon outside range; choose
// a nearby stationary observer without moving enemies or clearing map cover.
static Json stageGeneratedEngagement(Session &s){
 for(size_t i=0;i<s.enemies.size();++i){
  const auto &body=s.world.targets[i];const auto &spec=enemySpec(s.enemies[i].spec);
  if(spec.traits&(Flying|Hover))continue;
  for(const auto &attack:spec.attacks){
   if(attack.range<=10)continue;
   float near=attack.minimum+5,far=std::min(45.f,attack.range-2);if(near>far)continue;
   float radius=std::clamp(attack.range*.55f,near,far);
   for(int direction=0;direction<32;++direction){
    float angle=float(direction)*2*pi/32;Vec3 p=body.origin+Vec3{std::cos(angle)*radius,0,std::sin(angle)*radius};
    p.y=s.world.surfaceHeight(p.x,p.z);
    float range=length(p-body.origin);
    if(!insideArea(p,s.settings.extent*.98f)||range<attack.minimum+2||range>attack.range-2||!clearPosition(s.world,p,3.5f))continue;
    // Defense target priority must not override the candidate with a more
    // distant asset that is outside this enemy's real weapon envelope.
    bool priority=false;for(int a:s.mission.assets)if(length(s.world.targets[a].origin-body.origin)<range+20)priority=true;
    if(priority)continue;
    Pilot observer;observer.position=body.origin;
    if(!visibilityInRange(s.world,observer,p+Vec3{0,6,0})||!s.world.enemyLineOfSight(body.center,p+Vec3{0,6,0},int(i),-1))continue;
    bool reachable=true;Vec3 previous=body.origin;int samples=int(std::ceil(radius/2));
    for(int n=1;n<=samples&&reachable;++n){
     Vec3 next=body.origin+(p-body.origin)*(float(n)/samples);next.y=s.world.surfaceHeight(next.x,next.z);
     reachable=s.world.groundPassable(next,3.5f)&&std::abs(next.y-previous.y)<=.6f+.6f*std::hypot(next.x-previous.x,next.z-previous.z)&&s.world.waterDepthAt(next,3.5f)<=s.world.maxWadeDepth;
     reachable=reachable&&clearPosition(s.world,next,3.5f,int(i));
     previous=next;
    }
    if(!reachable)continue;
    Vec3 deployment=s.member(1).body.position;s.member(1).body.position=p;
    return {{"enemy",int(i)},{"archetype",spec.id},{"range",range},{"weapon",attack.name},{"weapon_maximum",attack.range},{"walkable_samples",samples},{"clear_line_of_sight",true},{"original_deployment",{deployment.x,deployment.y,deployment.z}},{"stationary_observer",{p.x,p.y,p.z}}};
   }
  }
 }
 return nullptr;
}
int main(int argc,char **argv){try{
 Json matrix=Json::array();bool bounded=true,immobile=true;size_t shots=0;double totalMs=0;size_t ticks=0;
 for(int spec=0;spec<int(enemyCatalog().size());++spec){auto base=fixture(spec);
  for(int tactic=0;tactic<10;++tactic){auto s=base;s.enemies[0].tactic=tactic;Vec3 start=s.world.targets[0].origin;float travel=0,minRange=10000;int bursts=0,launched=0;
   for(int tick=0;tick<600;++tick){Vec3 before=s.world.targets[0].origin;float fire=s.enemies[0].fireClock;auto time=std::chrono::steady_clock::now();s.step(1.f/60);totalMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-time).count();++ticks;
    auto &dummy=s.member(1);if(dummy.sheet.hp<=0||dummy.ejected){dummy.sheet.hp=1000000;dummy.ejected=false;dummy.body.lifeSpent=false;dummy.body.ejectionTime=0;dummy.body.missionStatus=MissionStatus::Active;s.phase=Phase::Combat;}
    auto &t=s.world.targets[0];travel+=length(t.origin-before);minRange=std::min(minRange,length(t.origin-s.member(1).body.position));launched+=freshShots(s);if(s.enemies[0].fireClock>fire+.1f&&t.ai.muzzleFlash>.1f)++bursts;
    bounded&=std::isfinite(t.origin.x)&&std::isfinite(t.origin.y)&&std::isfinite(t.origin.z)&&insideArea(t.origin,s.settings.extent)&&s.ordnance.size()<=maxLiveProjectiles;
   }
   if(enemySpec(spec).kind==EnemyKind::Installation)immobile&=travel<.001f;
   shots+=launched;matrix.push_back({{"enemy",enemySpec(spec).id},{"tactic",tactic},{"travel",travel},{"lateral",s.world.targets[0].origin.x-start.x},{"min_range",minRange},{"shots",launched},{"bursts",bursts},{"player_damage",1000000-s.member(1).sheet.hp}});
  }
 }
 check(std::to_string(enemyCatalog().size()*10)+" sustained enemy/tactic cases remain finite and bounded",bounded,{{"cases",matrix.size()},{"ticks",ticks},{"shots",shots},{"mean_tick_ms",totalMs/ticks}});
 check("Installations remain stationary under every doctrine",immobile);
 // Keep ranged doctrines inside their real attack envelope, and keep ground
 // enemies out of the player's collision footprint throughout a flank.
 {auto s=fixture(3,0);s.member(1).body.position={0,0,60};int bursts=0;for(int i=0;i<1200;++i){float clock=s.enemies[0].fireClock;s.step(1.f/60);if(s.enemies[0].fireClock>clock+.1f&&s.world.targets[0].ai.muzzleFlash>.1f)++bursts;}
  check("Ranged withdrawal approaches and maintains firing range",bursts>=2,{{"bursts",bursts},{"final_range",length(s.world.targets[0].origin-s.member(1).body.position)}});}
 {auto s=fixture(3,7);s.member(1).body.position={0,0,60};int bursts=0;for(int i=0;i<1200;++i){float clock=s.enemies[0].fireClock;s.step(1.f/60);if(s.enemies[0].fireClock>clock+.1f&&s.world.targets[0].ai.muzzleFlash>.1f)++bursts;}
  check("Hit-and-run approaches instead of retreating after unfired attacks",bursts>=2,{{"bursts",bursts},{"final_range",length(s.world.targets[0].origin-s.member(1).body.position)}});}
 {auto s=fixture(3,3);float nearest=10000;for(int i=0;i<600;++i){s.step(1.f/60);nearest=std::min(nearest,std::hypot(s.world.targets[0].origin.x-s.member(1).body.position.x,s.world.targets[0].origin.z-s.member(1).body.position.z));}
  check("Ground flank does not enter player collision footprint",nearest>=2.49f,{{"nearest_root_distance",nearest}});}
 {auto s=fixture(3);s.step(1.f/60);check("Enemy torso and turret face their chosen target",std::abs(wrap(s.world.targets[0].ai.weaponYaw-s.world.targets[0].yaw))<.01f);
  int sounds=0;for(const auto &sound:s.world.sounds)if(sound.kind==SoundEvent::Shot)++sounds;check("Launched enemy attacks emit positional firing audio",sounds>0,{{"shot_sounds",sounds}});}
 {auto s=fixture(5,3);float nearest=10000;for(int n=0;n<600;++n){s.step(1.f/60);nearest=std::min(nearest,std::hypot(s.world.targets[0].origin.x-s.member(1).body.position.x,s.world.targets[0].origin.z-s.member(1).body.position.z));}
  auto aircraft=fixture(26);position(aircraft,0,{0,22,17});aircraft.world.cooperative=true;aircraft.world.teammates={&aircraft.member(1).body};bool overhead=aircraft.world.moveEnemy(0,{0,0,3},1.f/60,true);aircraft.world.teammates.clear();
  check("Low hover units respect player collision while aircraft pass overhead",nearest>=2.49f&&overhead,{{"nearest_root_distance",nearest},{"aircraft_passes_overhead",overhead}});}
 // A disabler must use its damaging beam as well as its zero-damage EMP.
 {auto s=fixture(14);int damage=0,emp=0;for(int i=0;i<200;++i){position(s,0,{});s.member(1).body.position={0,0,20};s.enemies[0].fireClock=0;s.step(1.f/60);for(const auto &shot:s.ordnance)if(shot.body.life>2.98f){if(shot.body.damage>0)++damage;if(shot.effect==AttackEffect::Reactor)++emp;}s.ordnance.clear();}
  check("Disabler alternates damage and EMP instead of starving one attack",damage>0&&emp>0,{{"damage_attacks",damage},{"emp_attacks",emp}});}
 // DOT death must not regenerate or launch a final attack in the same tick.
 {auto s=fixture(31);s.enemies[0].hp=1;s.world.targets[0].health=1;s.enemies[0].corrosion=.001f;s.enemies[0].regenClock=2.99f;s.step(.05f);
  check("Lethal corrosion does not resurrect or fire",s.enemies[0].hp==0&&s.ordnance.empty(),{{"hp_after_dot",s.enemies[0].hp},{"shots_after_dot",s.ordnance.size()}});}
 // A revealed distant player may be detected at night; an unlit player outside
 // base visibility must not steal that attack as a secondary candidate.
 {auto s=fixture(11,8,2);s.mission.recipe.environment=3;s.environmentCycle.startHour=22;
  for(unsigned n=0;n<10000&&s.environmentCycle.sample(s.world.climate).settings.weather!=Weather::Clear;++n)++s.environmentCycle.seed;
  s.member(1).body.position={0,0,105};s.member(1).body.searchlightOn=true;s.member(2).body.position={100,0,0};int invisibleShots=0;
  for(int i=0;i<120;++i){s.enemies[0].fireClock=0;s.step(1.f/60);for(const auto &shot:s.ordnance)if(shot.body.life>2.98f&&shot.body.velocity.x>50)++invisibleShots;s.ordnance.clear();}
  check("Co-op secondary targets obey night visibility",invisibleShots==0,{{"shots_at_unlit_out_of_visibility_player",invisibleShots}});}
 // The explicitly selected defense asset outranks a closer player; the chosen
 // ranged attack must not be retargeted below its minimum range.
 {auto s=fixture(3);s.mission.recipe.objective=1;s.mission.assets={int(s.enemies.size())};auto &asset=s.world.targets[s.enemies.size()];asset=Target{};asset.active=true;asset.health=1000;asset.origin={0,0,20};asset.center={0,6,20};asset.half={2,6,2};s.member(1).body.position={5,0,0};int wrong=0;
  for(int i=0;i<120;++i){position(s,0,{});s.enemies[0].fireClock=0;s.step(1.f/60);for(const auto &shot:s.ordnance)if(shot.body.life>2.98f&&shot.body.velocity.x>5&&std::abs(shot.body.velocity.x)>std::abs(shot.body.velocity.z))++wrong;s.ordnance.clear();}
  check("Defense priority and minimum-range target filtering",wrong==0,{{"ranged_shots_retargeted_to_close_player",wrong}});}
 // True ambushes stay quiet until someone approaches; concealment cancels fire.
 {auto s=fixture(4,9);s.member(1).body.position={0,0,80};s.enemies[0].ambushed=false;for(int i=0;i<120;++i)s.step(1.f/60);bool quiet=s.ordnance.empty();s.member(1).body.position={0,0,20};s.enemies[0].fireClock=0;s.step(1.f/60);bool engaged=s.world.targets[0].ai.muzzleFlash>0;
  s.member(1).sheet.hidden=true;s.member(1).hiddenRemaining=20;s.ordnance.clear();s.enemies[0].fireClock=0;s.step(1.f/60);check("Ambush proximity and player concealment",quiet&&engaged&&s.ordnance.empty());}
 // A carrier can redeploy both nearby ground allies, not repeatedly select only
 // the first ally in the vector.
 {auto s=fixture(26,8,1,3);s.member(1).body.position={0,0,60};for(int index=1;index<3;++index){s.enemies[index].spec=3;s.enemies[index].disabled=10000;applyOriginalEnemy(s.world.targets[index],3);s.world.targets[index].ai.airborne=false;position(s,index,{index==1?15.f:-15.f,0,-10});}
  auto a=s.world.targets[1].origin,b=s.world.targets[2].origin;s.step(1.f/60);
  check("Carrier can redeploy two allies",length(s.world.targets[1].origin-a)>3&&length(s.world.targets[2].origin-b)>3,{{"first_ally_travel",length(s.world.targets[1].origin-a)},{"second_ally_travel",length(s.world.targets[2].origin-b)}});}
 // Expected archetype cadence and altitude differences in the same geometry.
 {auto recruit=fixture(3),ace=fixture(13),hover=fixture(5),fly=fixture(26),regen=fixture(31);int normal=0,fast=0;regen.enemies[0].hp=1;
  for(int n=0;n<600;++n){float a=recruit.enemies[0].fireClock,b=ace.enemies[0].fireClock;recruit.step(1.f/60);ace.step(1.f/60);hover.step(1.f/60);fly.step(1.f/60);regen.step(1.f/60);if(recruit.enemies[0].fireClock>a+.1f)++normal;if(ace.enemies[0].fireClock>b+.1f)++fast;}
  check("Ace attack cadence exceeds recruit cadence",fast>normal,{{"recruit_bursts",normal},{"ace_bursts",fast}});
  check("Hover and flight keep distinct altitudes",hover.world.targets[0].origin.y>2&&fly.world.targets[0].origin.y>20,{{"hover_y",hover.world.targets[0].origin.y},{"flight_y",fly.world.targets[0].origin.y}});
  check("Living regenerators heal without exceeding maximum",regen.enemies[0].hp>1&&regen.enemies[0].hp<=regen.enemies[0].maxHp,{{"hp",regen.enemies[0].hp},{"maximum",regen.enemies[0].maxHp}});}
 // Archetype-specific support and perception, with actual projectile output.
 {auto supported=fixture(3,8,1,2);supported.enemies[1].spec=20;supported.enemies[1].disabled=10000;position(supported,1,{-20,0,0});auto plain=supported;plain.enemies[1].hp=0;plain.world.targets[1].health=0;double boosted=0,normal=0;
  for(int i=0;i<160;++i){supported.enemies[0].fireClock=plain.enemies[0].fireClock=0;supported.step(1.f/60);plain.step(1.f/60);for(auto &shot:supported.ordnance)if(shot.body.owner==0&&shot.body.life>2.98f)boosted+=shot.body.damage;for(auto &shot:plain.ordnance)if(shot.body.owner==0&&shot.body.life>2.98f)normal+=shot.body.damage;supported.ordnance.clear();plain.ordnance.clear();}
  check("Commander aura improves nearby ally damage",boosted>normal,{{"supported_damage",boosted},{"unsupported_damage",normal}});}
 {auto s=fixture(4);position(s,0,{0,0,160});s.member(1).body.position={};bool hidden=!s.view(1).targets[0].active;s.member(1).scanRemaining=s.member(1).scanDuration=3;s.member(1).scannedContacts.push_back({0,4,160});bool scan=!s.view(1).targets[0].active&&!s.view(1).rpgRadar.contacts.empty();s.member(1).scanRemaining=0;s.member(1).drone=60;bool drone=s.view(1).targets[0].active;s.member(1).drone=0;position(s,0,{0,0,20});check("Radar scans detect camouflage without optical reveal while drones and proximity expose it",hidden&&scan&&drone&&s.view(1).targets[0].active);}
 {auto s=fixture(3,3,1,2);s.member(1).body.position={0,0,80};position(s,0,{0,0,-10});position(s,1,{0,0,10});float a=s.world.targets[0].origin.x,b=s.world.targets[1].origin.x;for(int n=0;n<60;++n)s.step(1.f/60);check("Pincer units take opposing flanks",(s.world.targets[0].origin.x-a)*(s.world.targets[1].origin.x-b)<-1,{{"left_lateral",s.world.targets[0].origin.x-a},{"right_lateral",s.world.targets[1].origin.x-b}});}
 {auto s=fixture(3,4,1,2);s.member(1).body.position={0,0,60};position(s,0,{-5,0,0});position(s,1,{5,0,0});float start=length(s.world.targets[0].origin-s.world.targets[1].origin);for(int n=0;n<120;++n)s.step(1.f/60);float end=length(s.world.targets[0].origin-s.world.targets[1].origin);check("Mobile dispersal increases ally separation",end>start+4,{{"initial_separation",start},{"final_separation",end}});}
 std::set<int> effects;int armed=0;
 for(int spec=0;spec<int(enemyCatalog().size());++spec){auto base=fixture(spec);for(float range:{5.f,20.f,60.f,150.f}){auto s=base;bool fired=false;for(int n=0;n<80;++n){position(s,0,{});s.member(1).body.position={0,0,range};s.enemies[0].fireClock=0;s.step(1.f/60);for(const auto &shot:s.ordnance)if(shot.body.owner==0&&shot.body.life>0){effects.insert(int(shot.effect));fired=true;}s.ordnance.clear();}armed+=fired;}}
 std::set<int> expectedEffects;for(int effect=0;effect<=int(AttackEffect::DisruptSystems);++effect)expectedEffects.insert(effect);
 check("Catalog attacks expose all12 realtime enemy effect types",effects==expectedEffects,{{"effect_ids",effects},{"enemy_range_cases_that_fired",armed}});
 // Real generated layouts retain their terrain, collision, buildings and routes.
 Json stress=Json::array();bool mapsOk=true;size_t mapTicks=0;
 for(int biomeCase=0;biomeCase<3;++biomeCase)for(int scenario=0;scenario<3;++scenario){
  Session s(uint32_t(42+biomeCase*71+scenario*313));s.add(1,"Stationary target");Dice d{17};auto sheet=createSheet(Chassis::Titan,PilotKind::Engineer,d,10);for(int m=0;sheet.moduleChoices&&m<int(Module::Count);++m)if(!sheet.has(Module(m)))installModule(sheet,Module(m));s.command(1,{{"op","configure"},{"profile",sheet}});
  GameSettings cfg;cfg.extent=500;cfg.enemyCount=12;cfg.theme=biomeCase*3+scenario;cfg.objective=scenario;cfg.opener=5;cfg.environment=biomeCase==0?3:biomeCase==1?5:1;cfg.world=biomeCase==2?2:0;cfg.location=biomeCase==2?7:biomeCase==1?1:0;cfg.tactic=(biomeCase*3+scenario)%10;
  s.command(1,{{"op","settings"},{"settings",cfg}});s.command(1,{{"op","ready"},{"value",true}});s.command(1,{{"op","deploy"}});s.member(1).sheet.hp=s.member(1).sheet.maxHp=1000000;
  // This is a geometry/AI soak, not a mission-balance test. Keep the defended
  // asset alive just like the stationary pilot, while preserving normal attack
  // selection, collision, damage and all runtime/movement/output bounds below.
  for(int asset:s.mission.assets)s.world.targets[asset].health=1000000;

  Json engagement=stageGeneratedEngagement(s);
  std::vector<Vec3> initial;for(const auto &t:s.world.targets)initial.push_back(t.origin);int observed=0,caseTicks=0;double ms=0;std::vector<double> frameTimes;int resets=0;bool caseBounds=true;
  for(int tick=0;tick<600&&s.phase==Phase::Combat;++tick){auto time=std::chrono::steady_clock::now();s.step(1.f/60);double tickMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-time).count();ms+=tickMs;frameTimes.push_back(tickMs);++mapTicks;++caseTicks;
    for(size_t index=0;index<s.enemies.size();++index)
      if(s.world.targets[index].ai.muzzleFlash>=.12f)++observed;
    for(size_t i=0;i<s.enemies.size();++i){const auto &t=s.world.targets[i];caseBounds&=std::isfinite(t.origin.x)&&std::isfinite(t.origin.y)&&std::isfinite(t.origin.z)&&insideArea(t.origin,s.settings.extent);}
    caseBounds&=s.ordnance.size()<=maxLiveProjectiles&&s.world.effects.size()<=maxLiveEffects;
    if(s.member(1).sheet.hp==0){auto &m=s.member(1);m.sheet.hp=m.sheet.maxHp;m.ejected=false;m.body.lifeSpent=false;m.body.ejectionTime=0;m.body.missionStatus=MissionStatus::Active;if(s.phase==Phase::Defeat&&std::all_of(s.mission.assets.begin(),s.mission.assets.end(),[&](int i){return s.world.targets[i].health>0;}))s.phase=Phase::Combat;++resets;}
  }
  int moved=0;for(size_t i=0;i<s.enemies.size();++i)if(length(s.world.targets[i].origin-initial[i])>1)++moved;
  bool caseOk=!engagement.is_null()&&caseBounds&&s.clock>=9.99f&&moved>0&&observed>0&&s.enemies.size()>=12;
  mapsOk&=caseOk;
  Json assets=Json::array();for(int asset:s.mission.assets)assets.push_back({{"index",asset},{"health",s.world.targets[asset].health}});
  if(!caseOk)std::cerr<<"Generated mission soak failed: seed="<<s.seed<<" phase="<<int(s.phase)<<" ticks="<<caseTicks<<" firing pulses="<<observed<<" assets="<<assets.dump()<<'\n';
  std::sort(frameTimes.begin(),frameTimes.end());
  stress.push_back({{"mean_tick_ms",ms/std::max(1,caseTicks)},{"p95_tick_ms",frameTimes.empty()?0:frameTimes[size_t((frameTimes.size()-1)*.95)]},{"maximum_tick_ms",frameTimes.empty()?0:frameTimes.back()},{"engagement",engagement},{"seed",s.seed},{"environment",cfg.environment},{"world",cfg.world},{"objective",cfg.objective},{"tactic",cfg.tactic},{"enemies",s.enemies.size()},{"actors_moved",moved},{"firing_pulses_observed",observed},{"cpu_ms",ms},{"simulated_seconds",s.clock},{"ticks",caseTicks},{"bounded",caseBounds},{"dummy_resets",resets},{"phase",int(s.phase)},{"protected_assets",assets},{"pilot_hp",s.member(1).sheet.hp},{"pilot_reactor",s.member(1).sheet.reactor},{"pilot_ejected",s.member(1).ejected}});
 }
 check("Nine generated mission/terrain stress cases remain bounded",mapsOk,{{"cases",stress.size()},{"ticks",mapTicks}});
 int failed=0;for(const auto &f:findings)if(!f.at("passed").get<bool>())++failed;
 Json report={{"passed",failed==0},{"failed",failed},{"checks",findings},{"matrix",matrix},{"generated_missions",stress},{"scope","Authoritative headless simulations using real game assets; no player profiles or remote server"}};
 if(argc==2){std::ofstream out(argv[1]);out<<report.dump(2)<<'\n';}
 std::cout<<Json{{"passed",failed==0},{"failed",failed},{"checks",findings}}.dump(2)<<'\n';return failed?1:0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}}
