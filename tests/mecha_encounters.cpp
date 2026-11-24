#include "rpg/mission_board.hpp"
#include "rpg/enemy_tactics.hpp"
#include "rpg/enemy_effects.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <queue>
using namespace iron;using namespace iron::rpg;
namespace {
Json findings=Json::array();
void check(const char *name,bool pass,Json data=Json::object()){findings.push_back({{"name",name},{"passed",pass},{"evidence",data}});}
Session mission(int location,int biome,int party=1,int /*legacyLevel*/=5,int world=0,int objective=3,int count=0){
  Session s(4271);Dice d{73};for(int i=1;i<=party;++i){s.add(unsigned(i),"Pilot "+std::to_string(i));s.member(unsigned(i)).sheet=createSheet(Chassis::Striker,PilotKind::Engineer,d);finishCreation(s.member(unsigned(i)).sheet);}
  GameSettings cfg;cfg.maps=1;cfg.location=location;cfg.environment=biome;cfg.world=world;cfg.theme=2;cfg.objective=objective;cfg.opener=5;cfg.enemyCount=count;
  s.command(1,{{"op","settings"},{"settings",cfg}});for(auto &m:s.members)s.command(m.id,{{"op","ready"},{"value",true}});s.command(1,{{"op","deploy"}});
  return s;
}
Vec3 v(std::array<float,3> p){return {p[0],p[1],p[2]};}
}
int main(int argc,char **argv){try{
  GameSettings cfg;cfg.maps=1;cfg.theme=2;cfg.location=0;cfg.environment=3;cfg.objective=0;
  bool budgets=true,deterministic=true,overrides=true;Json scale=Json::array();
  for(int level:{1,5,10})for(int party:{1,2,4}){int contacts=0,threat=0;
    auto board=generateMissionBoard(4271,cfg,level,0,party),copy=generateMissionBoard(4271,cfg,level,0,party);
    for(int slot=0;slot<9;++slot){auto &r=board[slot];contacts+=int(r.enemies.size());for(int id:r.enemies)threat+=enemySpec(size_t(id)).hd;
      deterministic&=Json(r)==Json(copy[slot])&&Json(r)==Json(generateMissionBoard(4271,cfg,10,0,party)[slot]);budgets&=r.partySize==party&&r.level==1&&r.difficulty==slot%3&&r.enemies.size()<=maxInitialEnemies;
      if(slot%3)budgets&=r.threatBudget>board[slot-1].threatBudget;}
    scale.push_back({{"level",level},{"party",party},{"contacts_per_offer",contacts/9.f},{"mean_threat_hd",threat/9.f}});
  }
  budgets&=scale.back()["mean_threat_hd"].get<float>()>scale.front()["mean_threat_hd"].get<float>()*2;
  cfg.enemyCount=37;auto forced=generateMission(3,0,cfg,10,4);overrides&=forced.enemies.size()==37;
  cfg.enemyCount=maxInitialEnemies+1;try{validateSettings(cfg);overrides=false;}catch(const std::exception&){}
  check("Offered difficulty scales by party and contract tier",budgets,{{"samples",scale}});
  check("Generation repeats exactly and explicit count retains its bound",deterministic&&overrides);
  const auto largestMobileGroup=[](const Session &s){
    size_t result=0;
    for(const auto &group:s.mission.groups)
      if(!group.actors.empty()&&combatSpec(s.enemies[size_t(group.actors.front())]).kind!=EnemyKind::Installation)
        result=std::max(result,group.actors.size());
    return result;
  };
  const auto solo=largestMobileGroup(mission(0,3,1,10));
  const auto fireteam=largestMobileGroup(mission(0,3,4,10));
  check("Larger parties face larger mobile groups",fireteam>solo&&fireteam<=7,
        {{"solo",solo},{"fireteam",fireteam}});
  Json routeFailures=Json::array();Json maps=Json::array();bool groups=true,sites=true,clearance=true,replication=true,doors=true,connected=true;
  for(auto setting:std::vector<std::array<int,3>>{{0,3,0},{1,5,0},{3,0,0},{4,3,0},{8,2,0},{6,7,0},{7,1,2},{0,4,3}}){
    auto s=mission(setting[0],setting[1],2,5,setting[2]);std::set<int> assigned;
    for(size_t n=0;n<s.mission.groups.size();++n){auto &g=s.mission.groups[n];groups&=!g.actors.empty()&&!g.route.empty();
      for(int i:g.actors){groups&=assigned.insert(i).second&&s.enemies[size_t(i)].group==int(n);
        for(int j:g.actors)groups&=enemyUnitsCompatible(enemySpec(s.enemies[size_t(i)].spec),enemySpec(s.enemies[size_t(j)].spec));}}
    for(const auto &g:s.mission.groups)if(g.site>=0)for(int i:g.actors){const auto &body=s.world.targets[size_t(i)];
      clearance&=body.half.y*2+3<32&&std::max(body.half.x,body.half.z)<17;}
    groups&=assigned.size()==s.enemies.size()&&s.enemies.size()<=maxEncounterEnemies&&s.world.targets.size()<=maxEncounterTargets;
    for(const auto &site:s.mission.sites){sites&=site.rooms.size()>=3&&site.core>=int(s.enemies.size())&&size_t(site.core)<s.world.targets.size();
      for(const auto &room:site.rooms){auto p=v(room.position);clearance&=s.world.indoors(p+Vec3{0,12,0})&&s.world.interiorCeilingAt(p)-s.world.surfaceHeight(p.x,p.z)>=31;}}
    if(setting[1]!=2)sites&=!s.mission.sites.empty();
    std::set<unsigned> seen;std::queue<unsigned> pending;seen.insert(0);pending.push(0);
    while(!pending.empty()){auto here=pending.front();pending.pop();for(auto edge:s.world.interiorNavLinks){unsigned next=edge[0]==here?edge[1]:edge[1]==here?edge[0]:unsigned(s.world.interiorNavNodes.size());if(next<s.world.interiorNavNodes.size()&&seen.insert(next).second)pending.push(next);}}
    for(const auto &site:s.mission.sites){for(const auto &room:site.rooms){
      bool found=false;for(size_t n=0;n<s.world.interiorNavNodes.size();++n)if(length(s.world.interiorNavNodes[n]-v(room.position))<2&&seen.count(unsigned(n)))found=true;
      connected&=found;if(!found)routeFailures.push_back({{"location",setting[0]},{"world",setting[2]},{"kind","disconnected room"},{"position",room.position},{"seen",seen.size()},{"nodes",s.world.interiorNavNodes.size()}});
    }
    if(site.style!=4){auto walker=s.world;for(size_t n=0;n<s.enemies.size();++n)walker.targets[n].active=false;
      auto start=v(site.entrance)+Vec3{0,0,10},goal=v(site.entrance)-Vec3{0,0,10};start.y=walker.surfaceHeight(start.x,start.z);goal.y=walker.surfaceHeight(goal.x,goal.z);
      walker.position=start;for(int n=0;n<80&&length(walker.position-goal)>.1f;++n)walker.movePilot(walker.position+unit(goal-walker.position)*.5f);
      doors&=length(walker.position-goal)<.1f;
      if(length(walker.position-goal)>=.1f){
        for(size_t q=0;q<walker.targets.size();++q){const auto &t=walker.targets[q];if(t.active&&t.health>0&&std::abs(t.center.x-walker.position.x)<t.half.x+4&&std::abs(t.center.z-walker.position.z)<t.half.z+8&&std::abs(t.center.y-(walker.position.y+5))<t.half.y+3)
          routeFailures.push_back({{"blocker",q},{"asset",t.generatorAsset},{"center",{t.center.x,t.center.y,t.center.z}},{"half",{t.half.x,t.half.y,t.half.z}},{"invulnerable",t.invulnerable}});}
      routeFailures.push_back({{"location",setting[0]},{"kind","blocked doorway"},{"remaining_m",length(walker.position-goal)},{"entrance",site.entrance},{"stopped",{walker.position.x,walker.position.y,walker.position.z}}});}
    }}
    Session client;client.readSnapshot(s.snapshot());replication&=client.snapshot()==s.snapshot();
    maps.push_back({{"location",setting[0]},{"biome",setting[1]},{"world",setting[2]},{"enemies",s.enemies.size()},{"groups",s.mission.groups.size()},{"sites",s.mission.sites.size()},{"targets",s.world.targets.size()}});
  }
  check("Contextual groups cover the roster with compatible allies and routes",groups,{{"maps",maps}});
  check("Terrain variants contain usable keyed dungeon sites",sites);
  check("All keyed rooms clear the player mecha and reconstruct on clients",clearance&&replication);
  check("Dungeon graph joins exterior routes and mechs physically cross doors",connected&&doors,{{"failures",routeFailures}});
  auto valid=mission(0,3);auto invalid=valid.snapshot();bool rejects=false;
  if(!valid.mission.groups.empty()){invalid["mission"]["groups"][0]["actors"][0]=999999;
    try{Session client;client.readSnapshot(invalid);}catch(const std::exception&){rejects=true;}}
  check("Malformed replicated encounter membership is rejected",rejects);
  auto patrol=mission(0,3);for(auto &m:patrol.members){m.sheet.hidden=true;m.hiddenRemaining=10000;}
  std::vector<Vec3> starts;for(size_t i=0;i<patrol.enemies.size();++i)starts.push_back(patrol.world.targets[i].origin);
  for(int tick=0;tick<600;++tick)patrol.step(1.f/60);
  int moved=0;for(size_t i=0;i<starts.size();++i)if(length(starts[i]-patrol.world.targets[i].origin)>2)++moved;
  check("Unalerted groups patrol without an omniscient player target",moved>0,{{"moving_actors",moved}});
  auto loot=mission(0,3);bool rewards=false;
  if(!loot.mission.sites.empty()){
    auto &site=loot.mission.sites.front();for(auto &e:loot.enemies)e.disabled=10000;
    for(const auto &group:loot.mission.groups)if(group.site==0)for(int i:group.actors){loot.enemies[size_t(i)].hp=0;loot.world.targets[size_t(i)].health=0;}
    auto cache=std::find_if(site.rooms.begin(),site.rooms.end(),[](const auto &r){return r.kind==3;});
    if(cache!=site.rooms.end()){
      loot.member(1).body.position=v(cache->position);auto &target=loot.world.targets[size_t(site.core)];
      Shot shot;shot.body.position=target.center-Vec3{0,0,target.half.z+5};shot.body.velocity={0,0,180};shot.body.damage=100;shot.body.shooter=1;shot.body.life=1;loot.ordnance.push_back(shot);
      for(int tick=0;tick<30&&!site.cleared;++tick)loot.step(.05f);
      size_t drops=loot.supplies.size();for(int n=0;n<10;++n)loot.step(.05f);
      rewards=site.cleared&&site.cacheClaimed&&drops>0&&loot.supplies.size()==drops;
      Session client;client.readSnapshot(loot.snapshot());rewards&=client.mission.sites.front().cleared&&client.mission.sites.front().cacheClaimed;
    }
  }
  check("Cleared site releases bounded salvage once and replicates",rewards);
  auto safe=mission(0,3,2,1,0,0);
  safe.phase=Phase::Combat;safe.mission.openerTime=20;
  for(auto &enemy:safe.enemies){enemy.disabled=10000;enemy.hp=0;}
  for(size_t i=0;i<safe.enemies.size();++i)safe.world.targets[i].health=0;
  safe.mission.groups.front().role=int(EncounterRole::Garrison);
  safe.member(2).body.position={-safe.settings.extent*.6f,0,-safe.settings.extent*.65f};
  safe.step(.05f);
  auto garrison=std::find_if(safe.supplies.begin(),safe.supplies.end(),[](const auto &drop){
    return drop.kind==SafeBoxKind::Garrison;
  });
  Json evidence={{"opened",safe.mission.recoveryOpen},{"boxes",safe.supplies.size()},{"objective",safe.mission.recipe.objective}};
  bool perPilot=garrison!=safe.supplies.end()&&safe.mission.recoveryOpen;
  if(perPilot){
    const auto index=garrison->pickup;
    auto &first=safe.member(1);
    auto &second=safe.member(2);
    const auto gear1=safeBoxReward(*garrison,1),gear2=safeBoxReward(*garrison,2);
    perPilot=gearInfo(gear1).module!=Module::Count&&gearInfo(gear2).module!=Module::Count;
    const auto original1=first.sheet.owned[size_t(gear1)],original2=second.sheet.owned[size_t(gear2)];
    first.body.position=safe.world.pickups[index].position;safe.step(.05f);
    const auto mask=safe.world.pickups[index].collectedBy;
    evidence["first_mask"]=mask;evidence["first_claims"]=first.pendingRecovery.size();
    evidence["pickup_distance"]=length(first.body.position-safe.world.pickups[index].position);
    perPilot&=mask==1&&first.sheet.owned[size_t(gear1)]==original1&&
      std::any_of(first.pendingRecovery.begin(),first.pendingRecovery.end(),[&](const auto &reward){return reward.gear==gear1;});
    Session replica;replica.readSnapshot(safe.snapshot());
    perPilot&=replica.world.pickups[index].collectedBy==1&&replica.member(1).pendingRecovery==first.pendingRecovery;
    second.body.position=safe.world.pickups[index].position;safe.step(.05f);
    perPilot&=safe.world.pickups[index].collectedBy==3;
    evidence["second_mask"]=safe.world.pickups[index].collectedBy;
    if(safe.phase==Phase::Combat){
      first.body.position=second.body.position=safe.world.missionEntry;
      for(int tick=0;tick<102&&safe.phase==Phase::Combat;++tick)safe.step(.05f);
    }
    evidence["final_phase"]=int(safe.phase);evidence["final_events"]=safe.events;
    perPilot&=safe.phase==Phase::Complete&&first.pendingRecovery.empty()&&second.pendingRecovery.empty()&&
      first.sheet.owned[size_t(gear1)]>original1&&second.sheet.owned[size_t(gear2)]>original2;
    auto fitted=first.sheet;
    equipGear(fitted,LeftShoulder,gear1);
    perPilot&=fitted.equipment[LeftShoulder]==gear1;
  }
  check("Static groups release one recoverable module per pilot and bank it only at mission success",perPilot,evidence);
  auto discovery=mission(0,3);
  bool found=false;
  if(!discovery.mission.sites.empty()){
    auto &site=discovery.mission.sites.front();
    const auto room=std::find_if(site.rooms.begin(),site.rooms.end(),[](const auto &r){return r.kind!=0&&r.kind!=3;});
    if(room!=site.rooms.end()){
      site.moduleCacheRoom=int(room-site.rooms.begin());
      for(auto &e:discovery.enemies)e.disabled=10000;
      discovery.mission.openerTime=100;
      discovery.member(1).body.position=v(room->position);
      for(int n=0;n<4;++n)discovery.step(.05f);
      Session replica;replica.readSnapshot(discovery.snapshot());
      found=site.moduleCacheRoom==-1&&std::count_if(replica.supplies.begin(),replica.supplies.end(),
          [](const auto &drop){return drop.kind==SafeBoxKind::ModuleCache;})==1;
    }
  }
  check("Exploring a dungeon room reveals its optional module box once and replicates it",found);
  bool grades=true;Json rates=Json::array();
  for(int tier=0;tier<3;++tier){
    std::array<int,3> bins{};
    std::set<WeaponRoll> rolls;
    for(unsigned seed=0;seed<4096;++seed){
      SupplyDrop box{0,SafeBoxKind::ModuleCache,seed,tier};
      const auto item=safeBoxEquipment(box,2);
      grades&=gradedModule(item.gear)&&item==safeBoxEquipment(box,2);
      ++bins.at(size_t(item.rarity));rolls.insert(item.roll);
    }
    grades&=std::all_of(bins.begin(),bins.end(),[](int count){return count>0;})&&rolls.size()==27;
    rates.push_back(bins);
  }
  check("Discovered module boxes roll all three grades and 27 deterministic manufacturing spreads per tier",grades,{{"grade_counts",rates}});
  int rareAt[3]{};
  for(int tier=0;tier<3;++tier)for(unsigned seed=0;seed<1024;++seed){
    auto gear=safeBoxReward({0,SafeBoxKind::Dungeon,seed,tier},1);
    rareAt[tier]+=weaponRarity(gear)==Rarity::Rare;
  }
  check("Dungeon difficulty shifts the chance of rare equipment",rareAt[0]<rareAt[1]&&rareAt[1]<rareAt[2],
        {{"rare_rewards",{rareAt[0],rareAt[1],rareAt[2]}}});
  auto field=mission(0,3,2);
  field.phase=Phase::Combat;field.mission.openerTime=20;
  for(auto &enemy:field.enemies)enemy.disabled=10000;
  auto &recipient=field.member(1);
  const auto spot=recipient.body.position;
  const auto index=field.world.pickups.size();
  field.world.pickups.push_back({spot,rpgSafeBoxPickup});
  field.supplies.push_back({index,SafeBoxKind::Dungeon,0x9e3779b9u,2});
  const auto reward=safeBoxReward(field.supplies.back(),1);
  const int oldStock=recipient.sheet.owned[size_t(reward)];
  field.step(.05f);const bool enhanced=recipient.fieldAmmo&&recipient.sheet.owned[size_t(reward)]==oldStock;
  for(auto &pilot:field.members){pilot.sheet.hp=0;pilot.ejected=true;}
  for(int tick=0;tick<71;++tick)field.step(.1f);
  check("Immediate ammunition upgrades stay mission-local and defeat discards unbanked rewards",
    enhanced&&field.phase==Phase::Defeat&&!recipient.fieldAmmo&&recipient.pendingRecovery.empty()&&
    recipient.sheet.owned[size_t(reward)]==oldStock);

  bool objectives=true,spawns=true;Json objectiveData=Json::array();
  for(int objective=0;objective<10;++objective){auto s=mission(objective%2?3:0,objective%2?0:3,4,10,0,objective);
    for(size_t i=0;i<s.enemies.size();++i){const auto &a=s.world.targets[i];spawns&=insideArea(a.origin,s.settings.extent*.98f);
      for(size_t j=i+1;j<s.enemies.size();++j){const auto &b=s.world.targets[j];
        if(std::abs(a.center.x-b.center.x)<a.half.x+b.half.x-.1f&&std::abs(a.center.y-b.center.y)<a.half.y+b.half.y-.1f&&std::abs(a.center.z-b.center.z)<a.half.z+b.half.z-.1f)spawns=false;}
      if(objective==6&&!factionDetachment(s,i))objectives&=a.origin.z<0;
    }
    objectiveData.push_back({{"objective",objective},{"enemies",s.enemies.size()},{"groups",s.mission.groups.size()},{"sites",s.mission.sites.size()}});
  }
  check("All ten objective variants deploy expanded parties within bounds",objectives,{{"missions",objectiveData}});
  check("Expanded deployment separates physical enemy bounds",spawns);
  auto survey=mission(0,3,2,1,0,3);
  survey.member(1).injuries|=uint32_t(MechaInjury::Sensors);
  const auto surveyView=survey.view(1);
  const auto &surveyAssets=survey.mission.assets;
  bool passiveObjectives=!surveyAssets.empty()&&!surveyView.sensorsWorking()&&surveyView.mapPilots.size()==1&&
      surveyView.rpgRadar.contacts.empty()&&surveyView.rpgRadar.objectives.size()==surveyAssets.size();
  if(!surveyAssets.empty()){
    survey.world.targets[surveyAssets.front()].active=false;
    const auto remaining=survey.view(1).rpgRadar.objectives;
    passiveObjectives&=remaining.size()+1==surveyAssets.size()&&
        std::none_of(remaining.begin(),remaining.end(),[&](Vec3 marker){
          return length(marker-survey.world.targets[surveyAssets.front()].origin)<.01f;
        });
  }
  check("Objective and party markers remain available when sensors fail and completed sites clear",passiveObjectives);
  Json pacing=Json::array();
  bool paced=true,staged=true,wandering=true,arrivalKinds=true,passivePulses=true;
  for(const auto scenario:{std::array<int,4>{0,3,2,0}, {3,0,7,0}, {7,1,2,2}}){
    auto s=mission(scenario[0],scenario[1],1,1,scenario[3],0);
    s.mission.recipe.theme=scenario[2];
    s.mission.recipe.manualMission=-1;
    for(auto &e:s.enemies)e.disabled=10000;
    auto &pilot=s.member(1);
    pilot.sheet.hidden=true;pilot.hiddenRemaining=1000;
    // Sample arrivals over two fronts without ending the test on exposure.
    pilot.sheet.hp=pilot.sheet.maxHp=10000;
    std::set<unsigned> modes;
    std::vector<float> arrivals;
    bool moved=false,within=true;
    for(int tick=1;tick<=1450;++tick){
      if(tick==640&&scenario[3]==0){
        pilot.body.position={s.settings.extent*.89f,0,0};
        pilot.body.position.y=s.world.surfaceHeight(pilot.body.position.x,pilot.body.position.z);
      }
      s.step(.1f);
      bool newWave=false;
      for(const auto &effect:s.world.effects)
        if(effect.age==0&&((effect.model>=rpgArrivalDropship&&effect.model<=rpgArrivalEdge)||
            effect.model==rpgArrivalKaijuRift||effect.model==rpgArrivalAirstrike)){
          newWave=true;modes.insert(effect.model);
        }
      if(newWave){
        arrivals.push_back(tick*.1f);
        const auto radar=s.view(1).rpgRadar;
        passivePulses&=radar.contacts.empty()&&radar.arrivals.size()==1&&radar.arrivals.front().progress==0;
        staged&=std::any_of(s.enemies.begin(),s.enemies.end(),[&](const Enemy &e){
          return e.ambient&&e.arrivalDelay>0&&!s.world.targets[size_t(&e-s.enemies.data())].active;
        });
        if(arrivals.size()==1){
          Session guest;guest.readSnapshot(s.snapshot());staged&=guest.snapshot()==s.snapshot();
          const auto remote=guest.view(1).rpgRadar;
          passivePulses&=radar.arrivals.size()==1&&remote.arrivals.size()==1&&
              length(remote.arrivals.front().position-radar.arrivals.front().position)<.01f;
        }
      }
      if(!arrivals.empty()&&std::abs(tick*.1f-arrivals.back()-4.3f)<.05f)
        passivePulses&=s.view(1).rpgRadar.arrivals.empty();
      for(size_t i=0;i<s.enemies.size();++i)if(s.enemies[i].ambient&&s.enemies[i].airstrikeUntil<=0&&s.enemies[i].hp>0&&s.world.targets[i].active){
        const auto &e=s.enemies[i];const auto p=s.world.targets[i].origin;
        const float traveled=std::hypot(p.x-e.anchorX,p.z-e.anchorZ);
        moved|=traveled>2;within&=traveled<=31.f;
      }
    }
    float average=arrivals.size()<2?145.f:(arrivals.back()-arrivals.front())/float(arrivals.size()-1);
    paced&=arrivals.size()>=2&&average<120.f;
    for(size_t i=1;i<arrivals.size();++i)paced&=arrivals[i]-arrivals[i-1]<120.f;
    wandering&=moved&&within;
    if(scenario[2]==7){
      arrivalKinds&=modes.contains(rpgArrivalBurrow)&&modes.contains(rpgArrivalKaijuRift)&&modes.size()==2;
      for(const auto &enemy:s.enemies)if(enemy.ambient)
        arrivalKinds&=combatSpec(enemy).kind==EnemyKind::SmallKaiju||combatSpec(enemy).kind==EnemyKind::LargeKaiju;
    }else if(scenario[3]==2)arrivalKinds&=modes.size()==1&&modes.contains(rpgArrivalWarp);
    else arrivalKinds&=modes.contains(rpgArrivalEdge)||modes.contains(rpgArrivalDropship);
    for(auto &e:s.enemies)if(!e.ambient)e.hp=0;
    s.step(.1f);
    if(s.mission.recoveryOpen){
      pilot.body.position=s.world.missionEntry;
      for(int tick=0;tick<52&&s.phase==Phase::Combat;++tick)s.step(.1f);
    }
    paced&=s.phase==Phase::Complete;
    pacing.push_back({{"biome",scenario[1]},{"wave_seconds",arrivals},{"mean_seconds",average},{"arrivals",modes},{"patrolled",moved},{"within_radius",within}});
  }
  check("Operations get recurring staged arrivals within the encounter pacing target",paced&&staged&&arrivalKinds,{{"scenarios",pacing}});
  check("Host and guest see one passive pulse per wave, and the site expires",passivePulses);
  check("Ambient enemies patrol a small area and stay outside objective accounting",wandering);
  auto ambientFixture=[](uint32_t seed,int party=1){
    auto s=mission(0,3,party,1,0,0);
    s.mission.recipe.seed=seed;
    s.mission.recipe.theme=2;
    s.mission.recipe.manualMission=-1;
    for(auto &e:s.enemies)e.disabled=10000;
    auto &pilot=s.member(1);
    pilot.sheet.hidden=true;pilot.hiddenRemaining=1000;
    pilot.sheet.hp=pilot.sheet.maxHp=10000;
    return s;
  };
  auto airstrike=ambientFixture(700);
  bool warned=false,bombed=false,departed=false;
  int volleys=0;
  for(int i=0;i<390&&!departed;++i){
    airstrike.step(.1f);
    for(const auto &e:airstrike.enemies)if(e.ambientWave==1&&e.airstrikeUntil>0){
      warned|=e.vehiclePass.phase==VehiclePassPhase::Warning;
      volleys=std::max(volleys,e.vehiclePass.pulse);
      departed|=e.hp<=0&&airstrike.world.targets[size_t(&e-airstrike.enemies.data())].escaped;
    }
    bombed|=std::any_of(airstrike.ordnance.begin(),airstrike.ordnance.end(),
                        [](auto &shot){return shot.coverSensitiveBlast;});
  }
  check("An airstrike telegraphs and drops three bombs before exiting",warned&&bombed&&departed&&volleys==3,
        {{"warning",warned},{"bombs",volleys},{"ordnance",bombed},{"exited",departed}});

  auto armor=ambientFixture(701);
  for(int tick=0;tick<111;++tick)armor.step(.1f);
  int tanks=0,membersOfPlatoon=0;
  for(const auto &e:armor.enemies)if(e.ambientWave==1){
    ++membersOfPlatoon;
    const std::string_view id=combatSpec(e).id;
    tanks+=id=="anti_mecha_hovertank"||id=="heavy_hover_tank";
  }
  check("An armored encounter stages multiple tanks in one group",tanks>=2&&tanks==membersOfPlatoon,
        {{"tanks",tanks},{"wave_members",membersOfPlatoon}});

  auto combatPay=ambientFixture(701,2);
  for(auto &m:combatPay.members){prepare(m.sheet);m.sheet.hidden=true;m.hiddenRemaining=1000;}
  // A second pilot shares the bounty regardless of who lands the killing blow.
  auto &guest=combatPay.member(2);
  const auto hostWallet=combatPay.member(1).sheet.credits,wingWallet=guest.sheet.credits;
  for(int n=0;n<136;++n)combatPay.step(.1f);
  auto destroyWaveActor=[&](size_t index){
    auto &e=combatPay.enemies[index];e.hp=1;e.corrosion=.001f;e.corrosionOwner=1;
    combatPay.step(.05f);
  };
  std::vector<size_t> targets;
  for(size_t i=0;i<combatPay.enemies.size();++i)if(combatPay.enemies[i].ambientWave==1)targets.push_back(i);
  bool delayed=targets.size()>=2;
  auto noBox=combatPay;noBox.mission.recipe.seed=706;
  for(auto i:targets){
    delayed&=combatPay.mission.encounterCredits==0;destroyWaveActor(i);
    noBox.enemies[i].hp=1;noBox.enemies[i].corrosion=.001f;noBox.enemies[i].corrosionOwner=1;
    noBox.step(.05f);
  }
  auto fieldBox=[](const SupplyDrop &drop){return drop.kind==SafeBoxKind::FieldSupplies;};
  const auto box=std::find_if(combatPay.supplies.begin(),combatPay.supplies.end(),fieldBox);
  bool rolled=box!=combatPay.supplies.end()&&std::none_of(noBox.supplies.begin(),noBox.supplies.end(),fieldBox)&&
              noBox.mission.encounterCredits==combatPay.mission.encounterCredits;
  if(rolled){
    auto supplies=combatPay;
    const auto pickup=box->pickup;
    auto &crate=supplies.world.pickups[pickup];
    for(auto &pilot:supplies.members){
      pilot.body.position=crate.position;
      pilot.sheet.inventory[size_t(Gear::Repair)]=pilot.sheet.repairKits=0;
    }
    // Start one pilot with loaded ammo to verify that a duplicate turns into a nanite.
    supplies.member(1).explosive=1;
    supplies.step(.05f);
    const auto firstStock=supplies.member(1).sheet.repairKits;
    const auto secondStock=supplies.member(2).sheet.repairKits;
    supplies.step(.05f);
    Session remote;
    const auto state=supplies.snapshot();
    remote.readSnapshot(state);
    const auto &wire=state.at("supplies").at(size_t(box-combatPay.supplies.begin()));
    rolled&=wire.size()==5&&wire.at(1)==int(SafeBoxKind::Garrison)&&
            std::any_of(remote.supplies.begin(),remote.supplies.end(),fieldBox);
    rolled&=firstStock==1&&supplies.member(1).sheet.repairKits==firstStock&&supplies.member(2).sheet.repairKits==secondStock&&
            (secondStock==1||supplies.member(2).explosive>0)&&supplies.member(1).pendingRecovery.empty()&&
            crate.collectedBy==3&&remote.world.pickups[pickup].collectedBy==3&&remote.member(1).explosive>0;
  }
  check("Random encounter supplies roll once on clearance and each pilot claims an immediate consumable independently",rolled);
  const int bonus=10*(1+combatPay.mission.recipe.difficulty);
  delayed&=combatPay.mission.encounterCredits==bonus&&combatPay.member(1).sheet.credits==hostWallet&&guest.sheet.credits==wingWallet;
  auto lostPay=combatPay;
  for(auto &m:lostPay.members){m.sheet.hp=0;m.ejected=true;}
  for(int n=0;n<75&&lostPay.phase==Phase::Combat;++n)lostPay.step(.1f);
  delayed&=lostPay.phase==Phase::Defeat&&lostPay.mission.encounterCredits==0&&lostPay.member(2).sheet.credits==wingWallet;
  Session observer;observer.readSnapshot(combatPay.snapshot());
  delayed&=observer.mission.encounterCredits==bonus;
  combatPay.mission.mapCount=2;combatPay.mission.exitOpen=true;combatPay.mission.transitTime=1.99f;
  for(auto &m:combatPay.members)m.body.position=v(combatPay.mission.exit);
  combatPay.step(.05f);
  delayed&=combatPay.phase==Phase::Briefing&&combatPay.mission.mapIndex==1&&combatPay.mission.encounterCredits==bonus;
  combatPay.command(1,{{"op","deploy"}});
  combatPay.mission.openerTime=100;combatPay.mission.recoveryOpen=true;combatPay.mission.transitTime=4.99f;
  for(auto &m:combatPay.members)m.body.position=combatPay.world.missionEntry;
  combatPay.step(.05f);
  combatPay.step(.05f);
  const int payout=combatPay.mission.contractReward+bonus;
  delayed&=combatPay.phase==Phase::Complete&&combatPay.member(1).sheet.credits==hostWallet+payout&&
           combatPay.member(2).sheet.credits==wingWallet+payout&&airstrike.mission.encounterCredits==0;
  check("Cleared random encounters pay both pilots once at mission success, survive transit and expire on defeat",delayed,
        {{"expected_bonus",bonus},{"encounter_credits",combatPay.mission.encounterCredits},{"phase",int(combatPay.phase)},{"wallet",combatPay.member(1).sheet.credits},{"expected_wallet",hostWallet+payout},{"log",combatPay.events}});
  auto surviving=ambientFixture(702);
  const auto cohort=[](const Session &s){
    return int(std::count_if(s.enemies.begin(),s.enemies.end(),[](auto &e){return e.ambientWave==1&&e.hp>0;}));
  };
  for(int tick=0;tick<240&&cohort(surviving)==0;++tick)surviving.step(.1f);
  const int originalCohort=cohort(surviving);
  const auto firstArrival=surviving.world.elapsed;
  auto cleared=surviving;
  for(size_t i=0;i<cleared.enemies.size();++i)if(cleared.enemies[i].ambientWave==1){
    cleared.enemies[i].hp=0;cleared.world.targets[i].health=0;cleared.world.targets[i].active=false;
  }
  int pulses=0;
  float firstReserve=-1;
  bool cap=true;
  for(int tick=0;tick<1000;++tick){
    surviving.step(.1f);
    cleared.step(.1f);
    int active=0;
    for(const auto &e:surviving.enemies)active+=e.ambient&&e.hp>0;
    cap&=active<=5;
    if(cohort(surviving)>originalCohort+pulses){
      if(firstReserve<0)firstReserve=surviving.world.elapsed-firstArrival;
      pulses=cohort(surviving)-originalCohort;
    }
  }
  check("Only a surviving encounter can receive a single timed reserve wave within the actor cap",
        originalCohort==2&&pulses==1&&firstReserve>=48&&firstReserve<=94&&cohort(cleared)==0&&cap,
        {{"original",originalCohort},{"additional",pulses},{"delay",firstReserve},{"cleared",cohort(cleared)},{"cap",cap}});
  Json cpu=Json::array();bool bounded=true;
  for(int count:{12,24,48}){auto s=mission(0,3,4,10,0,3,count);std::vector<double> timings;
    for(int tick=0;tick<600;++tick){
      for(auto &m:s.members){m.sheet.hp=m.sheet.maxHp=1000000;m.sheet.reactor=m.sheet.maxReactor=12;m.ejected=false;m.body.lifeSpent=false;m.body.missionStatus=MissionStatus::Active;}
      s.phase=Phase::Combat;auto before=std::chrono::steady_clock::now();s.step(1.f/60);
      timings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count());
      bounded&=s.enemies.size()<=maxEncounterEnemies&&s.ordnance.size()<=maxLiveProjectiles&&s.world.targets.size()<=maxEncounterTargets;
    }
    double total=0;for(auto ms:timings)total+=ms;std::sort(timings.begin(),timings.end());
    cpu.push_back({{"initial_enemies",count},{"final_enemies",s.enemies.size()},{"mean_tick_ms",total/timings.size()},{"p95_tick_ms",timings[size_t(timings.size()*.95)]},{"worst_tick_ms",timings.back()},{"snapshot_bytes",s.snapshot().dump().size()}});
  }
  check("Expanded combat stays within actor, scene and projectile limits",bounded,{{"desktop_cpu_only",cpu}});
  int failed=0;for(const auto &f:findings)failed+=!f["passed"].get<bool>();Json report={{"passed",failed==0},{"failed",failed},{"checks",findings}};
  if(argc==2){std::ofstream out(argv[1]);out<<report.dump(2)<<'\n';}
  std::cout<<report.dump(2)<<'\n';return failed?1:0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}}
