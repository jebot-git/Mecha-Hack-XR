#include "rpg/session.hpp"
#include "rpg/generation.hpp"
#include "rpg/enemy_navigation.hpp"
#include <cmath>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
using namespace iron;using namespace iron::rpg;
namespace {
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
void connected(const World &world){
  require(!world.interiorNavNodes.empty(),"Section has no navigation anchors");
  std::set<unsigned> seen;std::queue<unsigned> pending;seen.insert(0);pending.push(0);
  while(!pending.empty()){auto here=pending.front();pending.pop();for(auto edge:world.interiorNavLinks){unsigned next=edge[0]==here?edge[1]:edge[1]==here?edge[0]:unsigned(world.interiorNavNodes.size());if(next<world.interiorNavNodes.size()&&seen.insert(next).second)pending.push(next);}}
  require(seen.size()==world.interiorNavNodes.size(),"Section sites are disconnected from the existing map");
}
void routesClear(const World &world){
  for(auto edge:world.interiorNavLinks){auto a=world.interiorNavNodes[edge[0]],b=world.interiorNavNodes[edge[1]];
    int samples=std::max(1,int(std::ceil(std::hypot(a.x-b.x,a.z-b.z)/5)));
    for(int i=0;i<=samples;++i){auto p=a+(b-a)*(float(i)/samples);p.y=world.surfaceHeight(p.x,p.z);
      if(!world.groundPassable(p,3.5f))throw std::runtime_error("Section floor blocked seed="+std::to_string(world.prototypeSeed)+" at="+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z)+" depth="+std::to_string(world.waterDepthAt(p,3.5f))+" limit="+std::to_string(world.waterTraversalLimitAt(p,3.5f))+" heights="+std::to_string(world.surfaceHeight(p.x+3.5f,p.z))+","+std::to_string(world.surfaceHeight(p.x-3.5f,p.z))+","+std::to_string(world.surfaceHeight(p.x,p.z+3.5f))+","+std::to_string(world.surfaceHeight(p.x,p.z-3.5f)));
      for(const auto &t:world.targets)if(t.active&&t.health>0&&t.invulnerable&&t.spawnType<0){
        float dx=std::max(0.f,std::abs(p.x-t.center.x)-t.half.x),dz=std::max(0.f,std::abs(p.z-t.center.z)-t.half.z);
        if(t.center.y+t.half.y<=p.y+.01f||t.center.y-t.half.y>=p.y+28)continue;
        if(std::hypot(dx,dz)<=18)throw std::runtime_error("Section wall blocks full-mech route seed="+std::to_string(world.prototypeSeed)+" at="+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z)+" target="+std::to_string(t.center.x)+","+std::to_string(t.center.y)+","+std::to_string(t.center.z)+" half="+std::to_string(t.half.x)+","+std::to_string(t.half.y)+","+std::to_string(t.half.z));
      }
    }
  }
}
bool walk(World &world,Vec3 from,Vec3 to){
  world.position=from;
  for(int step=0;step<1800;++step){
    auto travel=to-world.position;travel.y=0;
    if(length(travel)<.03f)return std::abs(world.position.y-to.y)<.8f;
    const auto previous=world.position;
    world.movePilot(previous+unit(travel)*std::min(1.f,length(travel)));
    if(length(world.position-previous)<.001f)return false;
  }
  return false;
}
void tier(unsigned seed,int biome,int location,int region,bool underground){
  World w;
  std::vector<DungeonSite> sites;
  buildMissionEnvironment(w,seed,biome,750,{},location,0,region,5,-1,2,&sites);
  require(w.interiorDecks.size()>=5&&w.interiorTierNodes.size()==7&&!w.geometry().interiorDeckMesh.empty(),
          "Seed-selected section has no rendered ramp and storey");
  connected(w);
  const auto foot=w.interiorNavNodes[w.interiorTierNodes.front()];
  const auto middle=w.interiorNavNodes[w.interiorTierNodes[1]];
  const auto landing=w.interiorNavNodes[w.interiorTierNodes[2]];
  const auto terminal=w.interiorNavNodes[w.interiorTierNodes.back()];
  require(std::abs(terminal.y-foot.y-76)<.1f&&((terminal.y<0)==underground),
          "Third level has the wrong elevation");
  require(w.interiorCeilingAt(terminal)-terminal.y>=34,
          "Upper terminal has insufficient overhead clearance");
  for(size_t n=1;n<w.interiorTierNodes.size();++n){
    const auto a=w.interiorNavNodes[w.interiorTierNodes[n-1]],b=w.interiorNavNodes[w.interiorTierNodes[n]];
    require(walk(w,a,b)&&walk(w,b,a),"Pilot cannot traverse the switchback in both directions");
  }
  const float lower=w.surfaceHeight(landing.x,landing.z);
  require(landing.y-lower>35&&((landing.y<0)==underground),"Storey elevations are not separated at their shared footprint");
  require(walk(w,foot,landing)&&walk(w,landing,foot),"Player cannot walk the tier ramp in both directions");
  require(walk(w,{landing.x,lower,landing.z},{middle.x,lower,middle.z}),"Lower floor beneath the upper room is blocked");
  require(w.deckContact(landing+Vec3{0,4,0},landing-Vec3{0,4,0})<1&&
          w.deckContact(landing-Vec3{0,4,0},landing+Vec3{0,4,0})<1,"Deck fails to block fire in both directions");
  require(w.interiorCeilingAt(landing)-landing.y>=34,"Upper floor does not provide mech headroom");
  require(w.interiorCeilingAt({landing.x,lower,landing.z})-lower>32,"Lower floor does not provide mech headroom");
  require(w.groundContact(landing+Vec3{0,3,0},landing-Vec3{0,3,0})<1,"Projectiles pass through the deck");
  const auto below=Vec3{landing.x,lower,landing.z};
  const auto down=gravityRay(w,landing+Vec3{0,12,0},below,true);
  const auto up=gravityRay(w,below+Vec3{0,12,0},landing+Vec3{0,4,0});
  require(down&&up&&down->normal.y>.99f&&up->normal.y<-.99f,"Deck does not support floor and ceiling gravity traces");
  const auto aim=w.traceAim(below+Vec3{0,12,0},{0,-1,0},18);
  require(std::abs(aim.position.y-lower)<.1f,"Below-grade aiming clips at sea level or misses the local floor");
  Target guard;guard.origin=foot;guard.half={3,5,3};guard.center=foot+Vec3{0,5,0};
  guard.ai.localCenter={0,5,0};guard.ai.localHalf=guard.half;
  guard.ai.boundsReady=true;guard.spawnType=3;guard.health=100;
  const auto actor=w.targets.size();w.targets.push_back(guard);
  require(enemyNavigationPassage(w,actor,foot,landing,false)&&
          !enemyNavigationPassage(w,actor,{landing.x,lower,landing.z},landing,false),
          "Enemy navigation does not distinguish the ramp from a same-footprint floor jump");
  const auto secondFoot=w.interiorNavNodes[w.interiorTierNodes[4]];
  const auto secondCrest=w.interiorNavNodes[w.interiorTierNodes[5]];
  require(enemyNavigationPassage(w,actor,secondFoot,secondCrest,false)&&
          enemyNavigationPassage(w,actor,secondCrest,secondFoot,false),
          "NPC cannot route along the upper return flight");
  for(int n=0;n<138;++n){
    auto along=unit(Vec3{middle.x-foot.x,0,middle.z-foot.z});
    require(w.moveEnemy(actor,along,.06f,false),"NPC cannot climb the physical ramp");
  }
  require(w.targets[actor].origin.y>middle.y-1.f,"NPC stayed on the lower floor during ascent");
  w.targets.pop_back();
  const auto found=std::find_if(sites.begin(),sites.end(),[&](const auto &s){
    return s.core>=0&&std::abs(w.targets[size_t(s.core)].origin.y-terminal.y)<.1f;
  });
  require(found!=sites.end()&&found->underground==underground,
          "Generated upper storey has no dungeon objective on its floor");
  for(auto edge:found->links){
    const auto a=found->rooms[edge[0]].position,b=found->rooms[edge[1]].position;
    require(walk(w,{a[0],a[1],a[2]},{b[0],b[1],b[2]}),"Keyed upper floor route is obstructed");
  }
}
uint32_t findSeed(int biome,int location,bool underground,int style=-1){
  for(uint32_t seed=0;seed<10000;++seed){auto p=generationPlan(seed,biome,location,0);if(p.underground==underground&&(style<0?p.sectionStyle!=0:p.sectionStyle==style))return seed;}
  throw std::runtime_error("No matching deterministic section sample");
}
World build(uint32_t seed,int biome,int location,int region=0){World w;std::array<int,0> empty;buildMissionEnvironment(w,seed,biome,500,empty,location,0,region,5);return w;}
}
int main(){try{
  std::set<size_t> topologies;
  for(int style=0;style<4;++style){auto seed=findSeed(3,7,false,style);auto w=build(seed,3,7);
    connected(w);routesClear(w);topologies.insert(w.interiorNavNodes.size());
    require(w.interiorRegions.size()>=12,"Full facility lacks meaningful connected rooms");
    require(!w.indoors(missionEntryPosition(w,0)),"Full facility roof covers the insertion court");
    require(w.targets.size()<=maxEncounterTargets,"Facility structure exceeds bounded scene capacity");
    World walker=w;walker.position=w.interiorNavNodes[1];auto goal=w.interiorNavNodes[2];
    for(int tick=0;tick<900&&length(walker.position-goal)>.5f;++tick){auto delta=goal-walker.position;delta.y=0;walker.movePilot(walker.position+unit(delta)*.5f);}
    require(length(walker.position-goal)<1,"Actual pilot cannot walk between facility elevations");
  }
  require(topologies.size()>=3,"Interior layouts repeat one fixed room and passage count");
  {
    auto seed=findSeed(3,0,true);auto w=build(seed,3,0);Vec3 p{-.48f*500,0,-.18f*500};p.y=w.surfaceHeight(p.x,p.z);
    require(p.y<-20&&w.indoors(p+Vec3{0,12,0}),"Underground section is not a usable roofed depression");
    require(std::isfinite(w.interiorCeilingAt(p))&&w.interiorCeilingAt(p)-p.y>=32,"Underground headroom does not fit a mech");
    connected(w);routesClear(w);
  }
  {
    auto seed=findSeed(3,0,false);auto w=build(seed,3,0);Vec3 p{-.48f*500,0,-.18f*500};p.y=w.surfaceHeight(p.x,p.z);
    require(w.indoors(p+Vec3{0,12,0}),"Surface mission has no fitting partial facility");
    require(!w.indoors(missionEntryPosition(w,0)),"Partial facility encloses the entire map or entry");
    connected(w);routesClear(w);
  }
  {
    auto w=build(42,2,8);Vec3 p{-.48f*500,0,-.18f*500};p.y=w.surfaceHeight(p.x,p.z);
    require(w.waterDepthAt(p)>12&&w.indoors(p+Vec3{0,12,0}),"Submerged section lacks underwater usable floor and shelter");
    require(w.groundPassable(p,3.5f),"Sealed mech cannot enter the submerged section");
    require(w.waterDepthAt(missionEntryPosition(w,0))<.1f,"Submerged mission lost its dry insertion court");
    connected(w);routesClear(w);
  }
  {
    auto w=build(42,3,1,3);Vec3 p{0,w.surfaceHeight(0,-100),-100};
    require(p.y<0&&w.indoors(p+Vec3{0,12,0}),"Tunnel world does not place its facility below grade");
    connected(w);routesClear(w);
  }
  tier(4,3,0,0,true);
  tier(4,5,1,3,true);
  tier(5,3,0,0,false);
  require(build(1,3,0).interiorTierNodes.empty(),"Small map unexpectedly added a storey");
  World optional;
  buildMissionEnvironment(optional,1,3,750,{},0,0,0);
  require(optional.interiorTierNodes.empty(),"Eligible map seed always generates an extra floor");
  for(unsigned seed=0;seed<60;++seed)
    require(!generationPlan(seed,2,8,0).upperTier&&!generationPlan(seed,2,8,0).lowerTier,
            "Flooded terrain selected a dry vertical modifier");
  {
    World world;std::vector<DungeonSite> sites;
    buildMissionEnvironment(world,1,5,750,{},1,0,3,5,-1,2,&sites);
    require(sites.size()==1&&std::abs(sites.front().entrance[2]-300.f)<.2f,
            "Keyed tunnel entrance misses the physical insertion portal");
    for(auto edge:sites.front().links){
      auto a=sites.front().rooms[edge[0]].position,b=sites.front().rooms[edge[1]].position;
      require(walk(world,{a[0],a[1],a[2]},{b[0],b[1],b[2]}),"Keyed tunnel room link crosses a wall");
    }
  }
  std::cout<<"Varied facilities, underground approaches and submerged routes remain connected and physically traversable\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
