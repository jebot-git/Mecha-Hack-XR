#include "rpg/mission.hpp"
#include "rpg/campaign_story.hpp"
#include "rpg/bosses.hpp"
#include "rpg/manual_missions.hpp"
#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include <stdexcept>
namespace iron::rpg {
std::span<const MissionChoice> enemyThemes() {
  static constexpr MissionChoice data[] = {
      {"insurgents", "GUERILLA INSURGENTS"},
      {"laborers", "REBELLING LABORERS"},
      {"militants", "MILITANT FACTION"},
      {"mercenaries", "MECHA MERCENARIES"},
      {"deserters", "MILITARY DESERTERS"},
      {"bandits", "MECHANIZED BANDITS"},
      {"drones", "WEAPONIZED DRONES"},
      {"kaiju", "ALIEN KAIJU"},
      {"imperials", "WELL-OUTFITTED IMPERIALS"},
      {"aliens", "UNKNOWN ALIEN MECHA"}};
  return data;
}
std::span<const MissionChoice> missionObjectives() {
  static constexpr MissionChoice data[] = {
      {"eradicate", "ERADICATE HOSTILES"},
      {"defend", "DEFEND OBJECTIVE"},
      {"escort", "ESCORT CONVOY"},
      {"recon", "RECONNAISSANCE"},
      {"rescue", "RECOVERY AND RESCUE"},
      {"destroy_resources", "DESTROY VITAL RESOURCES"},
      {"halt_invasion", "HALT INVADING FORCES"},
      {"diversion", "CAUSE A DIVERSION"},
      {"reinforce", "PROVIDE REINFORCEMENTS"},
      {"capture", "CAPTURE ENEMY LEADER"}};
  return data;
}
std::span<const MissionChoice> missionLocations() {
  static constexpr MissionChoice data[] = {
      {"ruined_city", "RUINED CITY"},
      {"wilderness", "DENSE WILDERNESS"},
      {"battlefield", "OLD BATTLEFIELD"},
      {"desert", "BLASTED DESERT"},
      {"docks", "ABANDONED DOCKS"},
      {"graveyard", "MECHA GRAVEYARD"},
      {"colony", "LUNAR COLONY"},
      {"battlecruiser", "DERELICT BATTLECRUISER"},
      {"reef", "CORAL REEF"},
      {"asteroids", "ASTEROID FIELD"}};
  return data;
}
std::span<const MissionChoice> missionOpeners() {
  static constexpr MissionChoice data[] = {
      {"orbital_drop", "ORBITAL COMBAT DROP"},
      {"night_approach", "COVERT NIGHT APPROACH"},
      {"teleport", "EXPERIMENTAL TELEPORT INSERTION"},
      {"tunnel", "ABANDONED TUNNEL INSERTION"},
      {"water", "WATER INFILTRATION"},
      {"base_attack", "BASE UNDER ATTACK - SCRAMBLE"}};
  return data;
}
std::span<const MissionChoice> enemyTactics() {
  static constexpr MissionChoice data[] = {
      {"ranged_cover", "RANGED COVER AND WITHDRAWAL"},
      {"charge", "FAST MELEE CHARGE"},
      {"fortify", "FORTIFY A DEFENSIVE POSITION"},
      {"pincer", "FLANK AND PINCER"},
      {"disperse", "MOBILE DISPERSAL"},
      {"flank", "CONTINUOUS FLANKING"},
      {"berserk", "RECKLESS ASSAULT"},
      {"hit_run", "HIT-AND-RUN"},
      {"precision", "MEASURED PRECISION"},
      {"ambush", "TRAPS AND AMBUSH"}};
  return data;
}
namespace {
// A shuffled deck gives neighbouring offers distinct features; the independent
// salts preserve variety between dimensions without exposing generator controls.
int contractChoice(uint32_t seed,unsigned number,int count,uint32_t salt) {
  Dice choices{seed^salt^(number/unsigned(count))*0x85ebca6bu};
  std::vector<int> deck;
  for(int i=0;i<count;++i)deck.push_back(i);
  for(int i=count-1;i>0;--i)std::swap(deck[i],deck[choices.next()%unsigned(i+1)]);
  return deck[number%unsigned(count)];
}
bool compatibleArrival(const MissionRecipe &r,int opener) {
  if(r.location==7&&(opener==0||opener==4))return false;
  bool vacuum=r.environment==1||r.environment==7||r.location==6||r.location==9;
  if(vacuum&&opener==4)return false;
  return r.environment!=2||opener==2||opener==4;
}
void shuffleRoster(std::vector<int> &pool,Dice &dice) {
  for(size_t i=pool.size();i>1;--i)std::swap(pool[i-1],pool[dice.next()%i]);
}
int encounterTactic(const EnemySpec &enemy,int primary,Dice &dice) {
  if(enemy.kind==EnemyKind::Installation){constexpr int choices[]={2,8,9};return choices[dice.next()%3];}
  if(enemy.kind==EnemyKind::SmallKaiju||enemy.kind==EnemyKind::LargeKaiju){constexpr int choices[]={1,6,7};return choices[dice.next()%3];}
  if(enemy.kind==EnemyKind::Labor){constexpr int choices[]={1,2,9};return choices[dice.next()%3];}
  const int choices[]={primary,4,5,7,8,9};return choices[dice.next()%6];
}
}
void fitMissionPacing(MissionRecipe &r,const GameSettings &settings) {
  if(settings.maps||settings.enemy>=0||settings.objective>=0){
    r.radius=settings.extent;
    return;
  }
  // A convoy, defended site or advancing invasion ties the pilot to a place or a clock.
  const bool focused=r.finale||(r.countdownSeconds>0&&r.countdownSeconds<=720)||r.objective==1||r.objective==2||
                     r.objective==6||r.objective==7||r.objective==8;
  Dice layout{r.seed^0xd682a50bu};
  r.radius=std::min(settings.extent,focused?float(500+25*(layout.next()%7)):
                                           float(725+25*(layout.next()%12)));
  if(r.finale){r.maps=1;r.siteCount=0;r.depotCount=0;return;}
  if(r.objective==2)r.radius=std::min(settings.extent,std::max(r.radius,550.f));
  const bool slow=environmentCatalog()[r.environment].speed<.75f;
  switch(r.objective){
    case 1: case 7: case 8:
      r.maps=2;
      r.objectiveSeconds=std::max(r.objectiveSeconds,95.f);
      break;
    case 2:r.maps=3;break;
    case 9:r.maps=2;break;
    case 3:case 4:case 5:
      r.maps=(!focused&&slow&&r.objectiveCount>=3)?1:2;
      break;
    default:r.maps=1;break;
  }
  if(r.countdownSeconds>0)r.countdownSeconds=std::max(r.countdownSeconds,270.f);
  r.siteCount=focused?int(layout.next()%2):2+int(r.radius>=850 || r.siteCount>=2);
  r.depotCount=focused?int(layout.next()%2):2+int(r.radius>=850);
}
MissionRecipe generateMission(uint32_t seed, unsigned number,
                              const GameSettings &s, int /*legacyLevel*/,int partySize) {
  validateSettings(s);
  Dice d{seed ^ number * 0x9e3779b9u};
  MissionRecipe r;
  r.maps=s.maps?s.maps:1+contractChoice(seed,number,4,0x5ab139d7u);r.seed=d.next();
  r.partySize=std::clamp(partySize,1,4);r.difficulty=int(number%3);
  r.threatBudget=int(std::lround((20+7*r.difficulty)*(1.f+.72f*(r.partySize-1))));
  r.siteCount=s.enemy>=0?0:1+int((r.seed>>8)%2);
  // Choose the physical setting first, then inhabitants and activities within it.
  r.location=s.location<0?contractChoice(seed,number,10,0x487a2b19u):s.location;
  r.theme=s.theme<0?contractChoice(seed,number,10,0x3965e3a7u):s.theme;
  r.objective=s.objective<0?contractChoice(seed,number,10,0x6e21b4c3u):s.objective;
  r.tactic=s.tactic<0?contractChoice(seed,number,10,0x273c912du):s.tactic;
  // Surface sites can appear in several climates, while sealed hulls, reefs and
  // orbital sites keep their physical constraints. Every native biome is used.
  static const std::array<std::vector<int>,10> climates={
    std::vector<int>{3,0,4,6},{5,6,4},{0,3,5,6,4},{0,4},
    {3,2,6},{0,7,3},{7,1},{1},{2},{1,7}};
  const auto &climate=climates[r.location];
  r.environment=s.environment<0?climate[d.next()%climate.size()]:s.environment;
  r.opener=s.opener<0?contractChoice(seed,number,6,0x9d47e1b3u):s.opener;
  if(s.opener<0&&(!compatibleArrival(r,r.opener)||(r.opener==1&&r.seed%3u!=0))){
    const bool allowNight=r.seed%3u==0;
    std::vector<int> choices;
    for(int opener=0;opener<6;++opener)if((opener!=1||allowNight)&&compatibleArrival(r,opener))choices.push_back(opener);
    r.opener=choices[d.next()%choices.size()];
  }
  static const std::array<std::vector<int>,10> themes={
    std::vector<int>{3,4,6,13},{2,10},{3,5,6,8,20},{6,8,13,27,30},
    {3,4,6,8},{9,19,25,26,31},{0,8,11,14,18,21,22},
    {1,7,12,23,28,29,33},{5,13,16,17,20,24,27,30},{15,28,32}};
  auto pool=themes[r.theme];
  // Supplemental inhabitants join thematic pools by their real combat role,
  // retaining the original roster IDs and the party-level threat ceiling.
  for(size_t i=34;i<enemyCatalog().size();++i){
    const auto &e=enemySpec(i);bool use=false;
    switch(e.kind){
    case EnemyKind::SmallKaiju:case EnemyKind::LargeKaiju:use=r.theme==7;break;
    case EnemyKind::Labor:use=r.theme==1||r.theme==5;break;
    case EnemyKind::Installation:use=r.theme==0||r.theme==2||r.theme==6||r.theme==8;break;
    case EnemyKind::Conventional:use=r.theme==2||r.theme==6||r.theme==8;break;
    case EnemyKind::SmallMecha:case EnemyKind::LargeMecha:
      use=r.theme==3||r.theme==4||r.theme==5||r.theme==8||(r.theme==9&&enemyFaction(i)==0);break;
    }
    // Dedicated submersibles are contextual inhabitants, not dry-land enemies.
    if((e.traits&Submersible)&&r.environment!=2&&r.location!=4&&r.location!=8)use=false;
    if(use)pool.push_back(int(i));
  }
  int weakest=pool.front();for(int enemy:pool)if(enemySpec(enemy).hd<enemySpec(weakest).hd)weakest=enemy;
  std::erase_if(pool,[&](int i){return enemySpec(i).hd>encounterThreatLimit(r.difficulty);});
  if(pool.empty()){
    // A patrol offer must not quietly inherit a high-HD faction boss. Use a
    // mechanically appropriate scout proxy until its thematic roster unlocks.
    for(size_t i=0;i<enemyCatalog().size();++i)if(enemySpec(i).hd<=encounterThreatLimit(r.difficulty)&&
      (r.theme==7)==(enemySpec(i).kind==EnemyKind::SmallKaiju||enemySpec(i).kind==EnemyKind::LargeKaiju)&&
      !(enemySpec(i).traits&(Submersible|Starship)))pool.push_back(int(i));
    if(pool.empty())pool.push_back(weakest);
  }
  // Draw distinct inhabitants before reusing a type. This gives a mixed squad
  // while retaining faction/theme and contract-tier bounds instead of isolated rolls.
  shuffleRoster(pool,d);
  int count=s.enemyCount;
  if(!count){
    float mean=0;for(int enemy:pool)mean+=enemySpec(enemy).hd;mean/=pool.size();
    count=std::clamp(int(std::lround(r.threatBudget*1.25f/std::max(1.f,mean))),8+(r.partySize-1)*3+(s.extent>=750?3:0),maxInitialEnemies);
  }
  for(int i=0;i<count;++i){
    if(i&&size_t(i)%pool.size()==0)shuffleRoster(pool,d);
    int enemy=s.enemy>=0?s.enemy:pool[size_t(i)%pool.size()];
    r.enemies.push_back(enemy);
    r.tactics.push_back(s.tactic>=0?s.tactic:i==0?r.tactic:encounterTactic(enemySpec(enemy),r.tactic,d));
  }
  expandManualMission(r,d);
  r.countdownSeconds*=s.extent/500.f;
  if(!s.enemyCount)r.countdownSeconds*=std::max(1.f,float(r.enemies.size())/(6*r.partySize));
  std::array<int,4> allegiance{};for(int enemy:r.enemies)++allegiance[enemyFaction(enemy)];
  r.adversary=int(std::max_element(allegiance.begin(),allegiance.end())-allegiance.begin());
  r.employer=int((r.seed>>16)%(r.adversary==3?3:2));
  if(r.employer>=r.adversary)++r.employer;
  fitMissionPacing(r,s);
  r.rewardCredits=contractCredits(r);
  return r;
}
int contractCredits(const MissionRecipe &r){
  if(r.finale)return 0;
  static constexpr int base[]={70,60,75,60,75,70,85,65,65,85};
  const int track=storyDominant(r.storyTracks);
  const int storyBonus=track!=1&&r.storyTracks[track]>=2?std::min(12,2*r.storyTracks[track]):0;
  // The quote is per pilot. Additional players already increase the encounter budget.
  return base[r.objective]+15*std::clamp(r.difficulty,0,2)+18*(std::max(1,r.maps)-1)
      +3*std::clamp(r.campaignStage-1,0,8)+(r.countdownSeconds>0?8:0)
      +std::min(12,2*r.factionPressure)+std::min(8,r.campaignSupply)+storyBonus+(r.legacyArc>=0?5:0);
}
std::vector<std::string> briefing(const MissionRecipe &r, const EnvironmentCycle *cycle) {
  static const char *tasks[] = {
      "DESTROY ALL HOSTILES IN THE MISSION AREA.",
      "KEEP THE DEFENSE ASSET ALIVE UNTIL THE HOLD TIMER EXPIRES.",
      "STAY NEAR THE THREE VEHICLES UNTIL THE CONVOY REACHES EXTRACTION.",
      "VISIT ALL MARKED RECON BEACONS.",
      "RECOVER ALL MARKED PODS, THEN RETURN TO INSERTION.",
      "DESTROY ALL MARKED ENEMY RESOURCE STORES.",
      "STOP EVERY INVADER BEFORE THE DEFENSE LINE.",
      "DRAW ATTENTION AT THE BEACON UNTIL THE HOLD TIMER EXPIRES.",
      "REACH THE ALLIED UNIT AND HOLD UNTIL THE TIMER EXPIRES.",
      "DISABLE THE LEADER, APPROACH, THEN EXTRACT."};
  const bool alternateFront=r.storyArc>=0&&!r.finale&&r.storyFront>0;
  auto task=alternateFront?std::string{}:manualMissionTask(r);
  if(task.empty())task=tasks[r.objective];
  const std::string salvageTask=r.depotCount>=0?
      "OPTIONAL: BREACH MARKED SALVAGE DEPOTS AND RECOVER THEIR CACHES.":r.maps>1?
      "OPTIONAL: BREACH THE SALVAGE DEPOT IN THE FINAL SECTOR AND RECOVER ITS CACHE.":
      "OPTIONAL: BREACH THE SALVAGE DEPOT AND RECOVER ITS CACHE.";
  const auto choice=[](std::span<const MissionChoice> choices,int index)->std::string {
    return index>=0&&size_t(index)<choices.size()?choices[size_t(index)].name:"";
  };
  auto area=std::string("AREA: ")+missionLocations()[r.location].name;
  if(r.radius>0)area+=" / "+std::to_string(int(r.radius*2))+" M ACROSS";
  if(r.storyArc<0){
    if(r.planetoidFeature>=0)area+=" / "+choice(planetoidFeatures(),r.planetoidFeature);
    if(r.siteCount)area+=" / UP TO "+std::to_string(r.siteCount)+" SALVAGE SITES";
  }
  auto conditions=std::string("CONDITIONS: ")+(cycle?
      environmentDescription(missionEnvironment(r,*cycle),false):environmentCatalog()[r.environment].name);
  if(r.storyArc<0&&r.planetoidHazard>=0)conditions+=" / "+choice(planetoidHazards(),r.planetoidHazard);
  auto insertion=std::string("INSERTION: ")+missionOpeners()[r.opener].name;
  auto intel=std::string("CONTRACT: ")+(r.employer>=0?factions[r.employer]:"LOCAL")+" VS "+(r.adversary>=0?factions[r.adversary]:"HOSTILES");
  if(r.finale)intel=r.boss>=0?std::string("DESTROY THE COMMAND GUARD AND ITS LEADER. ")+campaignBoss(r.boss).counterplay:"DEFEAT THE ENEMY FIELD ARMY AND END THE WAR.";
  auto reward=r.finale?std::string("CAMPAIGN AWARD: +1 / STARTER KIT RESTORED"):"REWARD: "+std::to_string(r.rewardCredits)+" CR / "+std::to_string(r.maps)+" SECTORS";
  if(r.countdownSeconds>0)reward+=" / LIMIT "+std::to_string(int(r.countdownSeconds))+"S";
  else if(r.objective==1||r.objective==7||r.objective==8)reward+=" / HOLD "+std::to_string(int(objectiveDuration(r,r.objective==1?90.f:45.f)))+"S";
  if(r.storyArc>=0){
    auto lines=storyMissionBriefing(r);
    lines.push_back("OBJECTIVE: "+task);
    if(!r.finale&&r.depotCount!=0)lines.push_back(salvageTask);
    lines.insert(lines.end(),{area,insertion,conditions,reward});
    return lines;
  }
  if(!r.finale&&r.depotCount!=0)task+=" "+salvageTask;
  return {std::string("OPERATION: ")+manualMissionName(r),task,area,conditions,
          std::string("HOSTILES: ")+enemyThemes()[r.theme].name+" / "+std::to_string(r.enemies.size())+" / "+std::array<const char*,3>{"PATROL","FRONTLINE","DANGEROUS"}[r.difficulty]+" / PARTY "+std::to_string(r.partySize),
          insertion,intel,reward};
}
TacticalPlan tacticalPlan(int tactic, Vec3 p, Vec3 victim, float optimal,
                          float recovery, float clock, unsigned index,
                          Vec3 anchor, bool ambushed) {
  if (tactic < 0 || tactic >= 10)
    throw std::runtime_error("Unknown tactic");
  Vec3 delta = victim - p;
  delta.y = 0;
  float range = length(delta);
  Vec3 forward = unit(delta), side = cross(forward, {0, 1, 0});
  float sign = index % 2 ? 1.f : -1.f;
  TacticalPlan plan;
  plan.direction = forward * std::clamp((range - optimal) / 5, -1.f, 1.f);
  switch (tactic) {
  case 0: {
    float nearRange=std::max(8.f,std::min(35.f,optimal*.75f));
    float farRange=std::max(10.f,optimal*.95f);
    plan.direction=forward*(range<nearRange?-1.f:range>farRange?1.f:0.f);
    plan.spread = .8f;
    break;}
  case 1:
    plan.direction = range > 8 ? forward : Vec3{};
    plan.speed = 1.5f;
    plan.preferMelee = true;
    break;
  case 2:
    plan.direction = length(anchor - p) > 4 ? unit(anchor - p) : Vec3{};
    plan.spread = .8f;
    break;
  case 3:
    plan.direction = unit(forward * .45f + side * sign);
    plan.speed = 1.2f;
    break;
  case 4:
    plan.direction = unit(plan.direction + side * sign * .7f);
    plan.speed = 1.2f;
    break;
  case 5:
    plan.direction =
        unit(plan.direction * .5f +
             side * (std::sin(clock * .25f + index) > 0 ? 1.f : -1.f));
    plan.speed = 1.3f;
    break;
  case 6:
    plan.direction = range > 8 ? forward : Vec3{};
    plan.speed = 1.3f;
    plan.spread = 1.5f;
    break;
  case 7:
    plan.direction = forward * (recovery > 1 ? -1.f : range > 20 ? 1.f : 0.f);
    plan.speed = 1.4f;
    break;
  case 8:
    plan.direction = std::abs(range - optimal) > 8 ? plan.direction : Vec3{};
    plan.spread = .45f;
    break;
  case 9:
    plan.fire = ambushed || range <= 45;
    plan.direction = plan.fire
                         ? (range < 25 ? forward * -1 : Vec3{})
                         : (length(anchor - p) > 4 ? unit(anchor - p) : Vec3{});
    plan.spread = .7f;
    break;
  }
  return plan;
}
void addMissionAssets(World &world, MissionState &state, float extent) {
  state.assets.clear();
  state.visited.clear();
  state.depots.clear();
  if (state.recipe.objective == 9 && !world.targets.empty())
    world.targets[0].protectedTarget = true;
  int objective = state.recipe.objective;
  constexpr std::array transports{"escort_command","escort_cargo","escort_support"};
  int count = objective == 2 ? int(transports.size())
              : objective == 3 || objective == 5                     ? 3
              : objective == 4                                     ? 2
              : objective == 0 || objective == 6 || objective == 9 ? 0
                                                                   : 1;
  if(objective!=2&&state.recipe.objectiveCount>0&&count>0)count=std::clamp(state.recipe.objectiveCount,1,5);
  std::vector<Vec3> sites;
  auto reservedSite=[&](Vec3 p){return std::any_of(state.sites.begin(),state.sites.end(),[&](const DungeonSite &site){
    return p.x>site.bounds[0]-32&&p.x<site.bounds[2]+32&&p.z>site.bounds[1]-32&&p.z<site.bounds[3]+32;
  });};
  for(auto p:world.interiorNavNodes)
    if(insideArea(p,extent*.8f)&&std::hypot(p.x-world.missionEntry.x,p.z-world.missionEntry.z)>90&&!reservedSite(p))
      sites.push_back(p);
  Dice siteDice{state.recipe.seed^0x719f6a23u};
  for(size_t i=sites.size();i>1;--i)std::swap(sites[i-1],sites[siteDice.next()%i]);
  // Keep primary stops on a tractable route, particularly for slower hulls returning
  // with rescue pods. The remaining parcels are available for optional exploration.
  const bool routeObjective=state.recipe.depotCount>=0&&(objective==3||objective==4||objective==5);
  const float terrainSpeed=8.f*environmentCatalog()[state.recipe.environment].speed;
  const float travelBudget=terrainSpeed*(state.mapCount>1?175.f:290.f);
  const float leg=travelBudget*(objective==4?.62f:1.f)/std::max(1,count)+(objective==5?85.f:15.f);
  Vec3 previousStop=world.missionEntry;
  for (int i = 0; i < count; ++i) {
    Target t;
    applyOriginalModel(t, objective == 2 ? transports[size_t(i)] : objective == 1 ? "defense"
                         : objective == 8 ? "ally" : objective == 5 ? "resources"
                         : objective == 4 ? "pod" : "beacon");
    Vec3 desired = objective == 2 ? Vec3{0, 0, extent * .55f}
                 : objective == 1 ? Vec3{35, 0, extent * .60f}
                 : objective == 8 ? Vec3{-100, 0, extent * .3f}
                                 : Vec3{float(i - 1) * 120, 0,
                                        extent * .15f - float(i % 2) * 100};
    if(objective!=1&&objective!=2&&!sites.empty()){
      if(routeObjective){
        auto cost=[&](Vec3 p){
          float distance=std::hypot(p.x-previousStop.x,p.z-previousStop.z);
          float fromEntry=std::hypot(p.x-world.missionEntry.x,p.z-world.missionEntry.z);
          return std::abs(distance-leg)+std::max(0.f,fromEntry-travelBudget*.62f)*(objective==4?3.f:1.f);
        };
        auto next=std::min_element(sites.begin(),sites.end(),[&](Vec3 a,Vec3 b){return cost(a)<cost(b);});
        std::iter_swap(next,sites.end()-1);
      }
      desired=sites.back();sites.pop_back();
      std::erase_if(sites,[&](Vec3 other){return std::hypot(other.x-desired.x,other.z-desired.z)<60;});
    }
    float clearance = std::hypot(t.half.x, t.half.z) + 4;
    if(objective==2) {
      // The host places the formation once its route is prepared. A
      // joining client receives the same positions through the snapshot.
      t.origin=desired;
      t.ai.rpgMotor=EnemyMotor::Tracked;
    } else t.origin=missionNavigablePosition(world,desired,clearance);
    previousStop=t.origin;
    t.center=t.origin+rotate(angles(t.yaw),t.ai.localCenter);
    bool marker = objective == 3 || objective == 4 || objective == 7;
    t.health = objective == 5 ? 50 : 150;
    t.protectedTarget = true;
    t.invulnerable = marker;
    t.spawnType = marker ? 21 : -1;
    int index = int(world.targets.size()) - 8;
    state.assets.push_back(index);
    state.visited.push_back(false);
    world.targets.insert(world.targets.begin() + index, std::move(t));
  }
  if (objective == 4 || objective == 9) {
    Target beacon;
    beacon.origin = world.missionEntry;
    beacon.origin.y = world.surfaceHeight(beacon.origin.x, beacon.origin.z);
    beacon.half = {2, 3, 2};
    beacon.center = beacon.origin + Vec3{0, 3, 0};
    beacon.health = 1;
    beacon.invulnerable = true;
    beacon.protectedTarget = true;
    beacon.spawnType = 21;
    applyOriginalModel(beacon, "beacon");
    world.targets.insert(world.targets.end() - 8, beacon);
  }
  const int requested=state.recipe.depotCount<0?int(state.mapIndex+1==state.mapCount):state.recipe.depotCount;
  if(state.recipe.finale||requested==0)return;
  Target depot;
  applyOriginalModel(depot,"cover_cargo_armored");
  const float clearance=std::hypot(depot.half.x,depot.half.z)+5.f;
  const auto entry=world.missionEntry;
  const float offset=std::min(180.f,extent*.30f);
  const float side=(state.recipe.seed&1u)?1.f:-1.f;
  std::vector<Vec3> candidates{{entry.x+side*offset,0,entry.z*.70f},{entry.x-side*offset,0,entry.z*.70f}};
  if(requested>1)candidates.insert(candidates.end(),sites.begin(),sites.end());
  for(auto desired:candidates){
    try{depot.origin=missionNavigablePosition(world,desired,clearance);}
    catch(const std::runtime_error &){continue;}
    if(!insideArea(depot.origin,extent*.9f)||std::hypot(depot.origin.x-entry.x,depot.origin.z-entry.z)<50.f||
       depot.origin.y+2.f*depot.half.y>world.interiorCeilingAt(depot.origin,clearance))continue;
    if(std::any_of(state.depots.begin(),state.depots.end(),[&](const SalvageDepot &prior){
      const auto pos=world.targets[prior.target].origin;
      return std::hypot(pos.x-depot.origin.x,pos.z-depot.origin.z)<extent*.38f;
    }))continue;
    depot.center=depot.origin+depot.ai.localCenter;
    depot.health=55;
    const int index=int(world.targets.size())-8;
    state.depots.push_back({index,false});
    world.targets.insert(world.targets.begin()+index,depot);
    if(int(state.depots.size())==requested)break;
  }
}
} // namespace iron::rpg
