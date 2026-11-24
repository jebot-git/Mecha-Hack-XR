#include "rpg/encounters.hpp"
#include "rpg/session.hpp"
#include "rpg/enemy_tactics.hpp"
#include "rpg/enemy_effects.hpp"
#include "searchlight.hpp"
#include <algorithm>
#include <queue>
#include <stdexcept>
namespace iron::rpg {
namespace {
Vec3 point(const std::array<float,3> &p){return {p[0],p[1],p[2]};}
std::array<float,3> packed(Vec3 p){return {p.x,p.y,p.z};}
bool flying(const EnemySpec &s){return (s.traits&Flying)!=0;}
bool biological(const EnemySpec &s){return s.kind==EnemyKind::SmallKaiju||s.kind==EnemyKind::LargeKaiju;}
bool roomFits(const World &w,const Target &body,const DungeonSite &site){
  auto p=point(site.rooms.front().position);
  return std::max(body.half.x,body.half.z)<17&&body.half.y*2+3<std::min(32.f,w.interiorCeilingAt(p)-p.y);
}
bool validAnchor(const World &w,Vec3 p,const EnemySpec &spec){
  if(!insideArea(p,w.movementBounds[2]*.9f))return false;
  if(flying(spec))return !w.indoors(p+Vec3{0,12,0});
  if(spec.traits&Submersible)return w.waterDepthAt(p)>3;
  return w.groundPassable(p,4)&&w.waterDepthAt(p,4)<=w.waterTraversalLimitAt(p,4);
}
std::vector<std::array<float,3>> patrolRoute(const World &w,Vec3 anchor,const EnemySpec &spec,Dice &dice,const Session &session,std::span<const int> actors){
  std::vector<std::array<float,3>> route;
  int start=-1;float best=1e20f;
  for(size_t n=0;n<w.interiorNavNodes.size();++n){auto p=w.interiorNavNodes[n];
    if(validAnchor(w,p,spec)&&length(p-anchor)<best){best=length(p-anchor);start=int(n);}}
  if(start<0)return {packed(anchor)};
  std::vector<int> walk{start};
  for(int hop=0;hop<3;++hop){std::vector<int> next;
    for(auto edge:w.interiorNavLinks){int to=int(edge[0])==walk.back()?int(edge[1]):int(edge[1])==walk.back()?int(edge[0]):-1;
      if(to>=0&&std::find(walk.begin(),walk.end(),to)==walk.end()&&validAnchor(w,w.interiorNavNodes[to],spec)){
        // Foundations and mission assets may change an edge. Mixed squads
        // must respect every member's footprint and ground/hover capability.
        bool passable=true;
        for(int actor:actors){const auto &unit=combatSpec(session.enemies[size_t(actor)]);
          auto a=w.interiorNavNodes[size_t(walk.back())],b=w.interiorNavNodes[size_t(to)];
          bool airborne=(unit.traits&(Flying|Hover))!=0;
          if(airborne){float height=flying(unit)?22.f:3.f;a.y+=height;b.y+=height;}
          if(!enemyNavigationPassage(w,size_t(actor),a,b,airborne,false)){passable=false;break;}
        }
        if(passable)next.push_back(to);
      }}
    if(next.empty())break;
    walk.push_back(next[dice.next()%next.size()]);
  }
  for(int n:walk)route.push_back(packed(w.interiorNavNodes[n]));
  for(int n=int(walk.size())-2;n>0;--n)route.push_back(packed(w.interiorNavNodes[walk[size_t(n)]]));
  return route;
}
}
const char *encounterRoleName(int role){
  static constexpr const char *names[]={"PATROL","GARRISON","AMBUSH","ASSAULT","AIR WING","HUNTING PACK"};
  return role>=0&&role<6?names[role]:"DETACHMENT";
}
void validateEncounterState(const Session &s,size_t sceneTargets){
  const auto &r=s.mission.recipe;
  if(s.enemies.size()>maxEncounterEnemies||sceneTargets>maxEncounterTargets||s.mission.groups.size()>maxEncounterEnemies||s.mission.sites.size()>3||
     r.partySize<1||r.partySize>4||r.level<1||r.level>10||r.difficulty<0||r.difficulty>2||r.siteCount<0||r.siteCount>3)
    throw std::runtime_error("Invalid encounter limits");
  auto finite=[&](Vec3 p){return std::isfinite(length(p))&&std::abs(p.x)<=s.missionRadius()*2&&std::abs(p.z)<=s.missionRadius()*2;};
  const auto &mission=s.mission;
  if((mission.offendedNeutral|mission.withdrawnAllies|mission.lostAllies|mission.supportedFactions)>7||
     (mission.lostAllies&~mission.withdrawnAllies)||
     std::any_of(mission.factionKills.begin(),mission.factionKills.end(),[](int k){return k<0||k>12;}))
    throw std::runtime_error("Invalid mission faction incidents");
  int neutral=0,allied=0;
  std::vector<int> owner(s.enemies.size(),-1);
  for(size_t n=0;n<s.mission.groups.size();++n){const auto &g=s.mission.groups[n];
    if(g.role<0||g.role>5||g.site<-1||g.site>=int(s.mission.sites.size())||g.route.empty()||g.route.size()>16||g.cursor<0||g.cursor>=int(g.route.size())||g.actors.size()>maxEncounterEnemies||!std::isfinite(g.alert)||g.alert<0)
      throw std::runtime_error("Invalid encounter route");
    const bool isNeutral=g.factionRole==FactionRole::Neutral,isAllied=g.factionRole==FactionRole::Allied;
    neutral+=isNeutral;allied+=isAllied;
    if((!isNeutral&&!isAllied&&g.factionRole!=FactionRole::Contract)||
       ((isNeutral||isAllied)&&(g.faction<0||g.faction>=3||g.site!=-1||g.actors.empty()))||
       (g.withdrawing&&(!isAllied||!finite(point(g.withdrawal))))||
       (g.retaliating&&!g.withdrawing))
      throw std::runtime_error("Invalid faction detachment");
    for(auto p:g.route)if(!finite(point(p)))throw std::runtime_error("Invalid patrol coordinate");
    for(int i:g.actors){if(i<0||size_t(i)>=s.enemies.size()||owner[size_t(i)]!=-1||s.enemies[size_t(i)].group!=int(n))throw std::runtime_error("Invalid encounter membership");owner[size_t(i)]=int(n);
      if((isNeutral||isAllied)&&actorFaction(s,size_t(i))!=g.faction)throw std::runtime_error("Invalid faction member");}
  }
  if(neutral>3||allied>1)throw std::runtime_error("Too many faction detachments");
  for(size_t i=0;i<s.enemies.size();++i)if(s.enemies[i].group!=owner[i])throw std::runtime_error("Missing encounter member");
  for(const auto &site:s.mission.sites){
    if(site.rooms.size()<3||site.rooms.size()>8||site.links.size()>16||site.core<int(s.enemies.size())||size_t(site.core)>=sceneTargets||!finite(point(site.entrance))||site.bounds[0]>=site.bounds[2]||site.bounds[1]>=site.bounds[3]||(site.cacheClaimed&&!site.cleared))
      throw std::runtime_error("Invalid dungeon site");
    if(site.moduleCacheRoom< -1||site.moduleCacheRoom>=int(site.rooms.size()))throw std::runtime_error("Invalid dungeon module cache room");
    for(auto f:site.bounds)if(!std::isfinite(f))throw std::runtime_error("Invalid site bounds");
    for(const auto &room:site.rooms)if(room.kind<0||room.kind>4||!finite(point(room.position)))throw std::runtime_error("Invalid keyed room");
    for(auto e:site.links)if(e[0]<0||e[1]<0||size_t(e[0])>=site.rooms.size()||size_t(e[1])>=site.rooms.size())throw std::runtime_error("Invalid dungeon link");
  }
}
void planMissionEncounters(Session &s){
  s.mission.groups.clear();
  // Positive explicit roster overrides remain useful for isolated content tests.
  if(s.settings.enemy>=0)return;
  Dice dice{s.mission.recipe.seed^0x656e636fu};
  for(size_t i=0;i<s.enemies.size();++i){const auto &spec=combatSpec(s.enemies[i]);int chosen=-1;
    for(size_t g=0;g<s.mission.groups.size();++g){auto &group=s.mission.groups[g];const auto &leader=combatSpec(s.enemies[size_t(group.actors.front())]);
      if(group.actors.size()<size_t(spec.kind==EnemyKind::Installation?3:(flying(spec)?4:3)+s.mission.recipe.partySize-1)&&
         (spec.kind==EnemyKind::Installation)==(leader.kind==EnemyKind::Installation)&&flying(spec)==flying(leader)&&bool(spec.traits&Submersible)==bool(leader.traits&Submersible)&&enemyUnitsCompatible(spec,leader)){chosen=int(g);break;}}
    if(chosen<0){EncounterGroup group;
      group.role=int(flying(spec)?EncounterRole::AirWing:biological(spec)?EncounterRole::Pack:
        spec.kind==EnemyKind::Installation?EncounterRole::Garrison:
        (s.mission.recipe.objective==1||s.mission.recipe.objective==2||s.mission.recipe.objective==6)?EncounterRole::Assault:
        s.mission.recipe.tactic==9?EncounterRole::Ambush:EncounterRole::Patrol);
      s.mission.groups.push_back(group);chosen=int(s.mission.groups.size()-1);}
    s.enemies[i].group=chosen;s.mission.groups[size_t(chosen)].actors.push_back(int(i));
  }
  // Reposition against already accepted group members, not the discarded
  // isolated spawn spiral. Scenery and protected mission assets stay physical.
  for(size_t i=0;i<s.enemies.size();++i)s.world.targets[i].active=false;
  std::vector<Vec3> centers;size_t nextSite=0;
  for(size_t g=0;g<s.mission.groups.size();++g){auto &group=s.mission.groups[g];int lead=group.actors.front();
    auto &spec=combatSpec(s.enemies[size_t(lead)]);Vec3 anchor=s.world.targets[size_t(lead)].origin;
    if(g>0&&nextSite<s.mission.sites.size()&&!flying(spec)&&!(spec.traits&Submersible)&&s.mission.recipe.objective!=6){
      const auto &site=s.mission.sites[nextSite];bool fits=true;
      for(int i:group.actors)fits&=roomFits(s.world,s.world.targets[size_t(i)],site);
      if(fits){group.site=int(nextSite++);group.role=int(EncounterRole::Garrison);anchor=point(site.rooms.back().position);}
    }
    if(group.site<0&&!(g==0&&((s.mission.recipe.opener==5&&s.mission.recipe.objective!=6)||s.mission.recipe.objective==9))){
      float best=-1;for(size_t n=0;n<s.world.interiorNavNodes.size();++n){auto p=s.world.interiorNavNodes[n];
        if(!validAnchor(s.world,p,spec)||length(p-s.world.missionEntry)<150||(s.mission.recipe.objective==6&&p.z>-.35f*s.missionRadius()))continue;
        float separation=centers.empty()?length(p-s.world.missionEntry):1e6f;
        for(auto center:centers)separation=std::min(separation,length(center-p));
        float score=separation+float((dice.next()>>16)%40);
        if(score>best){best=score;anchor=p;}}
    }
    if(spec.traits&Submersible){float best=-1;
      for(const auto &water:s.world.waterRegions){Vec3 p{(water.bounds[0]+water.bounds[2])/2,0,(water.bounds[1]+water.bounds[3])/2};p.y=s.world.surfaceHeight(p.x,p.z);
        float depth=s.world.waterDepthAt(p);if(depth>best&&depth>3&&insideArea(p,s.missionRadius()*.9f)){best=depth;anchor=p;}}}
    if(group.site<0&&s.mission.recipe.objective!=6&&s.mission.recipe.objective!=9&&g%3==1&&!s.mission.assets.empty()){
      int asset=s.mission.assets[g%s.mission.assets.size()];
      if(asset>=0&&size_t(asset)<s.world.targets.size()&&!flying(spec)&&!(spec.traits&Submersible)){
        anchor=s.world.targets[size_t(asset)].origin+Vec3{35,0,35};group.role=int(EncounterRole::Garrison);}}
    centers.push_back(anchor);
    if(group.site>=0){for(const auto &room:s.mission.sites[size_t(group.site)].rooms)group.route.push_back(room.position);}
    else group.route=patrolRoute(s.world,anchor,spec,dice,s,group.actors);
    if(group.role==int(EncounterRole::Garrison)||group.role==int(EncounterRole::Ambush))group.route={packed(anchor)};
    for(size_t slot=0;slot<group.actors.size();++slot){int i=group.actors[slot];auto &body=s.world.targets[size_t(i)];auto &enemy=s.enemies[size_t(i)];
      const auto &unit=combatSpec(enemy);bool placed=false;
      float radius=std::clamp(std::max(body.half.x,body.half.z)+2,4.f,24.f);
      Vec3 home=anchor;
      if(group.site>=0){const auto &rooms=s.mission.sites[size_t(group.site)].rooms;home=point(rooms[(slot+1)%(rooms.size()-1)].position);}
      for(int attempt=0;attempt<96&&!placed;++attempt){
        float spacing=radius*2+6,angle=float(attempt%12)*pi/6+float(slot)*1.7f;
        float distance=attempt==0?0.f:spacing*float((attempt+11)/12);
        auto p=home+Vec3{std::sin(angle)*distance,0,std::cos(angle)*distance};p.y=s.world.walkableHeight(p);
        if(group.site>=0&&!s.world.interiorDecks.empty()&&std::abs(p.y-home.y)>18.f)continue;
        if(!insideArea(p,s.missionRadius()*.95f)||length(p-s.world.missionEntry)<45||(s.mission.recipe.objective==6&&p.z>-.2f*s.missionRadius()))continue;
        if((unit.traits&Submersible)&&s.world.waterDepthAt(p)<3)continue;
        if(group.site>=0){const auto &b=s.mission.sites[size_t(group.site)].bounds;
          if(p.x-radius<b[0]+2||p.x+radius>b[2]-2||p.z-radius<b[1]+2||p.z+radius>b[3]-2)continue;}
        if(flying(unit))p.y+=22;
        if(!clearPosition(s.world,p,radius,i))continue;
        if(!(unit.traits&(Flying|Hover|Submersible))&&s.world.waterDepthAt(p,radius)>s.world.waterTraversalLimitAt(p,radius))continue;
        body.origin=p;placed=true;
      }
      if(!placed){
        // A large footprint may not fit a room or the anchor. Fall back to the
        // existing validated floor search rather than create an obstructed actor.
        body.origin=missionNavigablePosition(s.world,home,radius,i,(unit.traits&(Flying|Hover|Submersible))?-1:s.world.maxWadeDepth);
      }
      body.active=true;s.world.updateEnemyBounds(body);enemy.anchorX=body.origin.x;enemy.anchorZ=body.origin.z;
    }
  }
  // Encounter placement replaces the old isolated anchors. Their ambush traps
  // must move with the actors rather than remain at discarded spawn positions.
  s.traps.clear();
  for(size_t i=0;i<s.enemies.size();++i)if(s.enemies[i].tactic==9){
    auto origin=s.world.targets[i].origin;
    auto p=origin+unit(s.world.missionEntry-origin)*20;p.y=s.world.surfaceHeight(p.x,p.z);
    if(insideArea(p,s.missionRadius()*.98f)&&length(p-s.world.missionEntry)>30&&clearPosition(s.world,p,1,int(i)))s.traps.push_back({p,false});
  }
}
void stepEncounterGroups(Session &s,float dt){
  for(auto &group:s.mission.groups){group.alert=std::max(0.f,group.alert-dt);int leader=-1;
    for(int i:group.actors)if(i>=0&&size_t(i)<s.enemies.size()&&s.enemies[size_t(i)].hp>0){
      if(leader<0)leader=i;
      for(const auto &member:s.members)if(enemyCanSeeMember(s,size_t(i),member)){group.alert=5;break;}}
    if(leader<0||group.route.size()<2||group.alert>0)continue;
    group.cursor%=int(group.route.size());
    if(length(s.world.targets[size_t(leader)].origin-point(group.route[size_t(group.cursor)]))<10)group.cursor=(group.cursor+1)%int(group.route.size());
  }
}
int Session::spawnAmbientWave(const Member &pilot,int count,unsigned wave,AmbientKind kind,bool reinforcing){
  const auto player=pilot.body.position;
  const bool indoor=world.indoors(player+Vec3{0,10,0});
  const bool dry=!indoor&&biome!=1&&biome!=2&&biome!=7&&world.waterDepthAt(player)<world.maxWadeDepth;
  const bool kaiju=mission.recipe.theme==7;
  if(kind!=AmbientKind::Patrol&&(!dry||kaiju))return 0;
  const float extent=missionRadius();
  const bool xEdge=std::abs(player.x)>std::abs(player.z);
  const float edgeCoordinate=xEdge?player.x:player.z;
  const bool mapEdge=dry&&!kaiju&&extent-std::abs(edgeCoordinate)<125&&
      (kind==AmbientKind::Armor||wave%3==0);
  const bool burrow=kaiju&&dry&&wave%3!=2&&!reinforcing;
  const unsigned arrival=kind==AmbientKind::Airstrike?rpgArrivalAirstrike:
      kaiju?(burrow?rpgArrivalBurrow:rpgArrivalKaijuRift):
      (mapEdge||kind==AmbientKind::Armor)?rpgArrivalEdge:
      (!dry||reinforcing||wave%2==0)?rpgArrivalWarp:rpgArrivalDropship;
  std::vector<int> roster;
  const int tier=encounterThreatLimit(mission.recipe.difficulty);
  for(size_t id=0;id<enemyCatalog().size();++id){
    const auto &spec=enemySpec(id);
    const std::string_view name=spec.id;
    if(kind==AmbientKind::Airstrike){
      if(name=="bomber")roster.push_back(int(id));
      continue;
    }
    if(kind==AmbientKind::Armor){
      if((name=="anti_mecha_hovertank"||name=="heavy_hover_tank")&&spec.hd<=tier)roster.push_back(int(id));
      continue;
    }
    if(spec.hd>tier||spec.kind==EnemyKind::Installation||spec.traits&(Carrier|Starship)||
       biological(spec)!=kaiju||(burrow&&!canBurrow(spec))||
       ((indoor||arrival==rpgArrivalDropship)&&spec.traits&(Flying|Submersible)))continue;
    if(std::find(mission.recipe.enemies.begin(),mission.recipe.enemies.end(),int(id))!=mission.recipe.enemies.end())
      roster.push_back(int(id));
  }
  if(roster.empty()){
    const std::string_view fallback=kaiju?"kaiju_spawnling":"mecha_recruit";
    for(size_t id=0;kind==AmbientKind::Patrol&&id<enemyCatalog().size();++id)
      if(enemySpec(id).id==fallback){roster.push_back(int(id));break;}
    if(roster.empty())return 0;
  }
  std::vector<Vec3> nearbyRoutes;
  if(kind==AmbientKind::Patrol&&!mapEdge){
    for(auto node:world.interiorNavNodes){
      const float distance=std::hypot(node.x-player.x,node.z-player.z);
      if(distance>=34.f&&distance<=96.f&&indoor==world.indoors(node+Vec3{0,10,0}))nearbyRoutes.push_back(node);
    }
    std::sort(nearbyRoutes.begin(),nearbyRoutes.end(),[&](Vec3 a,Vec3 b){
      return std::abs(length(a-player)-58.f)<std::abs(length(b-player)-58.f);
    });
  }
  const auto seed=mission.recipe.seed;
  const float headingRoll=weatherNoise(seed,wave+351+(reinforcing?279u:0u));
  float seedAngle=headingRoll*2*pi;
  if(reinforcing){
    float nearest=155.f;
    for(size_t i=0;i<enemies.size();++i){
      if(enemies[i].ambientWave!=wave||enemies[i].hp<=0||!world.targets[i].active)continue;
      const auto delta=world.targets[i].origin-player;
      const float distance=std::hypot(delta.x,delta.z);
      if(distance<.001f||distance>=nearest)continue;
      nearest=distance;
      seedAngle=std::atan2(delta.z,delta.x)+(headingRoll-.5f)*.70f;
    }
  }
  const float radius=kind==AmbientKind::Airstrike?115.f:kind==AmbientKind::Armor?79.f:
      std::clamp(visibilityRanges(world).base*.79f,41.f,68.f);
  int placed=0;
  for(int i=0;i<count;++i){
    const int id=roster[(wave-1+size_t(i)+(reinforcing?1u:0u))%roster.size()];
    const int routeTries=int(std::min<size_t>(nearbyRoutes.size(),8));
    for(int attempt=0;attempt<8+routeTries;++attempt){
      const float angle=seedAngle+float(attempt)*pi*.25f+
          (kind==AmbientKind::Patrol?float(i)*.32f:0.f);
      const Vec3 radial{std::cos(angle),0,std::sin(angle)};
      const auto across=cross(radial,Vec3{0,1,0});
      const float lateral=(i-(count-1)*.5f)*(kind==AmbientKind::Airstrike?34.f:16.f);
      Vec3 point=player+radial*radius+across*lateral;
      if(mapEdge&&kind!=AmbientKind::Airstrike){
        const float border=std::copysign(extent*.955f,edgeCoordinate);
        const float shift=lateral+float((attempt+1)/2)*(attempt%2?-18.f:18.f);
        point=xEdge?Vec3{border,0,player.z+shift}:Vec3{player.x+shift,0,border};
      }
      if(attempt>=8)point=nearbyRoutes[size_t(attempt-8)];
      point.x=std::clamp(point.x,-extent*.955f,extent*.955f);
      point.z=std::clamp(point.z,-extent*.955f,extent*.955f);
      point.y=world.surfaceHeight(point.x,point.z);
      if(indoor!=world.indoors(point+Vec3{0,10,0}))continue;
      size_t actor=0;
      if(!spawnManualEnemy(id,point,kind==AmbientKind::Armor?7:1,arrival,
           unit(player-point),placed==0,FactionRole::Contract,&actor))continue;
      auto &enemy=enemies[actor];
      enemy.ambientWave=wave;
      if(kind==AmbientKind::Airstrike){
        enemy.airstrikeUntil=world.elapsed+28.f;
        enemy.vehiclePass.aim=player+across*(lateral*.7f);
        enemy.vehiclePass.aim.y=world.surfaceHeight(enemy.vehiclePass.aim.x,enemy.vehiclePass.aim.z)+6.f;
      }
      ++placed;
      break;
    }
  }
  if(placed){
    if(!reinforcing)ambientBounties.push_back({wave,placed});
    else for(auto &bounty:ambientBounties)if(bounty.wave==wave)bounty.remaining+=placed;
  }
  return placed;
}

void Session::finishAmbientActor(size_t index,bool destroyed,bool partyKill){
  const unsigned wave=enemies[index].ambientWave;
  if(!wave||phase!=Phase::Combat)return;
  const auto it=std::find_if(ambientBounties.begin(),ambientBounties.end(),
      [wave](const auto &bounty){return bounty.wave==wave;});
  if(it==ambientBounties.end())return;
  it->partyKill|=partyKill;
  if(destroyed&&--it->remaining>0)return;
  if(destroyed&&it->partyKill){
    const int bonus=10*(1+std::clamp(mission.recipe.difficulty,0,2));
    mission.encounterCredits=std::min(1000000,mission.encounterCredits+bonus);
    log("ENCOUNTER CLEARED / "+std::to_string(bonus)+" CR ON EXTRACTION");
    Dice drop{mission.recipe.seed^0x73757070u^wave};
    if(drop.next()%3u==0&&placeSafeBox(world.targets[index].origin,SafeBoxKind::FieldSupplies,wave,mission.recipe.difficulty))
      log("FIELD SUPPLIES / SAFE BOX DROPPED");
  }
  ambientBounties.erase(it);
}

void Session::stepAmbientEncounters(float dt){
  if(!world.proceduralTerrain||settings.enemy>=0||settings.enemyCount>0||mission.exitOpen||
     mission.recoveryOpen||mission.recipe.finale)return;
  encounterClock-=dt;
  const bool rally=std::any_of(ambientReserves.begin(),ambientReserves.end(),
      [&](const auto &reserve){return world.elapsed>=reserve.due;});
  if(!rally&&encounterClock>0)return;
  std::vector<const Member*> pilots;
  for(const auto &m:members)if(m.combatControls()&&!m.body.lifeSpent)pilots.push_back(&m);
  if(pilots.empty())return;
  const int limit=3+2*int(pilots.size());
  int active=0;
  for(size_t i=0;i<enemies.size();++i){
    auto &enemy=enemies[i];auto &actor=world.targets[i];
    if(!enemy.ambient||enemy.hp<=0)continue;
    const bool distant=std::all_of(pilots.begin(),pilots.end(),[&](const Member *m){
      return length(actor.origin-m->body.position)>210.f&&!enemyCanSeeMember(*this,i,*m);
    });
    if(distant&&enemy.arrivalDelay<=0){finishAmbientActor(i,false);enemy.hp=0;actor.active=false;actor.health=0;}
    else ++active;
  }
  bool sentRelief=false;
  for(auto &reserve:ambientReserves){
    if(world.elapsed>=reserve.expires||world.elapsed<reserve.due)continue;
    const Member *nearby=nullptr;
    float nearest=155.f;
    for(size_t i=0;i<enemies.size();++i){
      if(enemies[i].ambientWave!=reserve.wave||enemies[i].hp<=0||!world.targets[i].active)continue;
      for(const auto *pilot:pilots){
        const float gap=length(world.targets[i].origin-pilot->body.position);
        if(gap<nearest){nearest=gap;nearby=pilot;}
      }
    }
    if(!nearby){reserve.expires=world.elapsed;continue;}
    if(active<limit&&!sentRelief){
      const int count=std::min(limit-active,1+int(pilots.size())/2);
      int placed=spawnAmbientWave(*nearby,count,reserve.wave,reserve.kind,true);
      if(!placed&&reserve.kind!=AmbientKind::Patrol)
        placed=spawnAmbientWave(*nearby,count,reserve.wave,AmbientKind::Patrol,true);
      if(placed){
        active+=placed;
        sentRelief=true;
        log(mission.recipe.theme==7?"VOIDMAW REINFORCEMENTS":"HOSTILE REINFORCEMENTS INBOUND");
        encounterClock=std::max(encounterClock,12.f);
        reserve.expires=world.elapsed;
        continue;
      }
    }
    reserve.due=world.elapsed+8.f;
  }
  std::erase_if(ambientReserves,[&](const auto &reserve){return world.elapsed>=reserve.expires;});
  if(encounterClock>0)return;
  if(active>=limit){encounterClock=8;return;}
  // New encounters fill lulls. The one reserve wave can join its own patrol
  // during an ongoing battle without bypassing the same actor cap.
  if(ambientWaves>=2&&std::any_of(enemyMemory.begin(),enemyMemory.end(),
       [](const auto &contact){return contact.remaining>0;})){
    encounterClock=76.f;
    return;
  }
  const auto &pilot=*pilots[ambientWaves%pilots.size()];
  const auto wave=ambientWaves+1;
  const auto roll=(mission.recipe.seed%5+wave-1)%5;
  auto kind=roll==0?AmbientKind::Airstrike:roll==1?AmbientKind::Armor:AmbientKind::Patrol;
  const int groupSize=kind==AmbientKind::Airstrike?1+int(pilots.size()>=3):
      int(pilots.size())+(kind==AmbientKind::Armor?2:1);
  int placed=spawnAmbientWave(pilot,std::min(limit-active,groupSize),wave,kind);
  if(!placed&&kind!=AmbientKind::Patrol){
    kind=AmbientKind::Patrol;
    placed=spawnAmbientWave(pilot,std::min(limit-active,1+int(pilots.size())),wave,kind);
  }
  if(!placed){encounterClock=8;return;}
  ++ambientWaves;
  if(kind!=AmbientKind::Airstrike){
    const float due=world.elapsed+48.f+weatherNoise(mission.recipe.seed,wave+923)*14.f;
    ambientReserves.push_back({wave,kind,due,due+32.f});
  }
  encounterClock=ambientWaves==1?23.f:77.f+weatherNoise(mission.recipe.seed,ambientWaves+821)*12.f;
}
void Session::patrolEnemy(size_t index,float dt,EnemyNavigationBudget *budget){
  auto &e=enemies[index];auto &body=world.targets[index];const auto &spec=combatSpec(e);
  if(spec.kind==EnemyKind::Installation)return;
  Vec3 destination{};
  if(e.ambient){
    const float angle=(e.patrolStep%4)*pi*.5f+float(index%7)*.3f;
    const float radius=world.indoors(body.origin+Vec3{0,10,0})?12.f:24.f;
    destination={e.anchorX+std::cos(angle)*radius,body.origin.y,e.anchorZ+std::sin(angle)*radius};
    if(std::hypot(destination.x-body.origin.x,destination.z-body.origin.z)<5.f||
       !insideArea(destination,missionRadius()*.967f)||
       (!(spec.traits&(Flying|Hover|Submersible))&&!world.groundPassable(destination,4))){++e.patrolStep;return;}
  }else{
    if(e.group<0||size_t(e.group)>=mission.groups.size())return;
    auto &group=mission.groups[size_t(e.group)];
    if(group.route.empty())return;
    destination=point(group.route[size_t(group.cursor)%group.route.size()]);
    if(group.alert>0)for(int ally:group.actors)if(size_t(ally)<enemyMemory.size()&&enemyMemory[size_t(ally)].remaining>0){
      auto known=std::find_if(members.begin(),members.end(),[&](const Member &m){return m.id==enemyMemory[size_t(ally)].target;});
      if(known!=members.end()&&known->sheet.hp>0&&!known->ejected&&!known->sheet.hidden&&known->hiddenRemaining<=0&&!known->body.lifeSpent){destination=enemyMemory[size_t(ally)].position;break;}
    }
    if(group.alert<=0&&(group.role==int(EncounterRole::Garrison)||group.role==int(EncounterRole::Ambush)))destination={e.anchorX,body.origin.y,e.anchorZ};
    auto offset=std::find(group.actors.begin(),group.actors.end(),int(index))-group.actors.begin();
    if(group.site<0&&offset>0&&group.route.size()>1&&!world.indoors(body.origin+Vec3{0,12,0})&&!world.indoors(destination+Vec3{0,12,0})){float spacing=std::clamp(std::max(body.half.x,body.half.z)*2+6,12.f,40.f);destination.x+=(offset%2?1:-1)*spacing;destination.z+=float((offset+1)/2)*spacing;}
  }
  if(body.ai.gravity.mode==GravityMode::Attached){
    const auto up=body.ai.gravity.up();
    const auto delta=destination-body.origin;
    const auto heading=delta-up*dot(delta,up);
    if(length(heading)>5){
      const auto local=rotate(gravityInverse(body.ai.gravity.frame),heading);
      body.yaw=std::atan2(-local.x,-local.z);body.ai.weaponYaw=body.yaw;
      moveRpgEnemy(index,unit(heading)*(4.f*environmentCatalog()[biome].speed*dt),dt,false);
    }
    return;
  }
  auto delta=destination-body.origin;delta.y=0;
  if(length(delta)<5){body.ai.velocity={};return;}
  bool swimming=(spec.traits&Submersible)&&biome==2&&world.waterDepthAt(body.origin)>0;
  const bool airborne=(spec.traits&(Flying|Hover))!=0||swimming;
  const bool tracked=groundDriveTank(spec);
  body.ai.airborne=airborne;
  const float speed=airborne?8.f:4.f*environmentCatalog()[biome].speed;
  if(spec.kind==EnemyKind::SmallKaiju&&!e.transformed&&
     gravityImpact(world,body.ai.gravity,body.origin,unit(delta)*(speed*dt),
                   std::clamp(body.ai.localCenter.y+body.ai.localHalf.y,3.f,28.f),
                   std::clamp(std::min(body.ai.localHalf.x,body.ai.localHalf.z),1.f,7.f)))return;
  auto heading=enemyNavigationDirection(world,index,unit(delta),destination,speed,dt,airborne,navigation[index],budget);
  if(length(heading)<.00001f){
    if(e.ambient)++e.patrolStep;
    return;
  }
  if(e.ambient){
    const Vec3 home{e.anchorX,body.origin.y,e.anchorZ};
    const auto away=body.origin-home;
    if(length(away)>=29&&dot(away,heading)>0)heading=unit(home-body.origin);
  }
  const float desiredYaw=std::atan2(-heading.x,-heading.z);
  body.yaw=tracked?wrap(body.yaw+std::clamp(wrap(desiredYaw-body.yaw),-1.75f*dt,1.75f*dt)):desiredYaw;
  body.ai.weaponYaw=body.yaw;world.updateEnemyBounds(body);
  const auto forward=direction(body.yaw,0);
  auto displacement=tracked?forward*(speed*dt*std::max(0.f,dot(heading,forward))):heading*(speed*dt);
  if(airborne){auto ahead=body.origin+displacement;float floor=world.walkableHeight(ahead);
    float desired=floor+(swimming?2.f:flying(spec)?22.f:3.f);float top=body.ai.localCenter.y+body.ai.localHalf.y;
    displacement.y=std::min(desired,world.interiorCeilingAt(ahead)-top-.5f)-body.origin.y;
    if(!swimming&&!flying(spec))displacement.y=std::clamp(displacement.y,-3.f*dt,3.f*dt);}
  if(insideArea(body.origin+displacement,missionRadius()*.97f))moveRpgEnemy(index,displacement,dt,airborne);
}
void Session::stepDungeonSites(){
  for(size_t i=0;i<mission.depots.size();++i){
    auto &depot=mission.depots[i];
    if(size_t(depot.target)>=world.targets.size())continue;
    const auto &structure=world.targets[size_t(depot.target)];
    if(!depot.opened&&structure.health<=0&&placeSafeBox(structure.origin,SafeBoxKind::Depot,unsigned(i),mission.recipe.difficulty)){
      depot.opened=true;
      log("SALVAGE DEPOT BREACHED / RECOVER THE CACHE");
    }
  }
  auto defeated=[&](const EncounterGroup &group){
    return std::all_of(group.actors.begin(),group.actors.end(),[&](int index){
      return enemies[size_t(index)].hp<=0&&!world.targets[size_t(index)].escaped;
    });
  };
  for(size_t n=0;n<mission.sites.size();++n){
    auto &site=mission.sites[n];
    for(auto &room:site.rooms)for(const auto &m:members)
      if(m.sheet.hp>0&&!m.ejected&&length(m.body.position-point(room.position))<22)room.visited=true;
    if(site.moduleCacheRoom>=0&&size_t(site.moduleCacheRoom)<site.rooms.size()){
      const auto &room=site.rooms[size_t(site.moduleCacheRoom)];
      if(room.visited&&placeSafeBox(point(room.position),SafeBoxKind::ModuleCache,unsigned(n),mission.recipe.difficulty)){
        site.moduleCacheRoom=-1;
        log("DUNGEON / MODULE SAFE BOX DISCOVERED");
      }
    }
    const bool guards=std::any_of(mission.groups.begin(),mission.groups.end(),[&](const auto &group){
      return group.site==int(n)&&!defeated(group);
    });
    const bool core=site.core>=0&&size_t(site.core)<world.targets.size()&&world.targets[size_t(site.core)].health>0;
    if(!site.cleared&&!guards&&!core){
      site.cleared=true;log("DUNGEON "+std::to_string(n+1)+" SECURED / SAFE BOX UNLOCKED");
    }
    if(site.cleared&&!site.cacheClaimed){
      const auto cache=std::find_if(site.rooms.begin(),site.rooms.end(),[](const auto &r){return r.kind==3;});
      const auto &room=cache==site.rooms.end()?site.rooms.back():*cache;
      site.cacheClaimed=placeSafeBox(point(room.position),SafeBoxKind::Dungeon,unsigned(n),mission.recipe.difficulty);
    }
  }
  for(size_t n=0;n<mission.groups.size();++n){
    auto &group=mission.groups[n];
    const auto role=EncounterRole(group.role);
    if(group.factionRole!=FactionRole::Contract||group.rewardPlaced||group.actors.empty()||group.route.empty()||
       (role!=EncounterRole::Garrison&&role!=EncounterRole::Ambush)||!defeated(group))continue;
    group.rewardPlaced=placeSafeBox(point(group.route.front()),SafeBoxKind::Garrison,unsigned(n),mission.recipe.difficulty);
  }
}
}
