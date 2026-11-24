#include "rpg/session.hpp"
#include "skirmish.hpp"
#include <iostream>
#include <queue>
#include <set>
#include <stdexcept>
using namespace iron;
using namespace iron::rpg;
namespace {
void check(bool value,const std::string &message){if(!value)throw std::runtime_error(message);}
bool structuralClear(const World &world,Vec3 p,float radius=3.5f){
  if(!world.groundPassable(p,radius))return false;
  for(const auto &target:world.targets)if(target.active&&target.health>0&&target.invulnerable&&target.spawnType<0&&
    std::abs(p.x-target.center.x)<target.half.x+radius&&std::abs(p.z-target.center.z)<target.half.z+radius&&
    p.y+15>target.center.y-target.half.y&&p.y<target.center.y+target.half.y)return false;
  return true;
}
void connected(const World &world,Vec3 from,Vec3 to){
  from.y=world.surfaceHeight(from.x,from.z);to.y=world.surfaceHeight(to.x,to.z);
  for(int leg=0;leg<80;++leg){
    if(length(from-to)<1)return;
    Vec3 next=missionNavigationWaypoint(world,from,to);
    check(length(next-from)>.1f,"Navigation stalled before objective");
    int samples=std::max(1,int(std::ceil(std::hypot(next.x-from.x,next.z-from.z)/5)));
    for(int i=0;i<=samples;++i){auto p=from+(next-from)*(float(i)/samples);p.y=world.surfaceHeight(p.x,p.z);
      check(structuralClear(world,p),"Connected route crosses solid geometry seed="+std::to_string(world.prototypeSeed)+" at="+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z));}
    from=next;
  }
  throw std::runtime_error("Navigation failed to reach connected objective");
}
uint64_t signature(const World &world){
  uint64_t hash=1469598103934665603ull;
  auto value=[&](float f){hash^=uint64_t(int64_t(std::llround(f*10000)));hash*=1099511628211ull;};
  for(const auto &v:world.geometry().terrain){value(v.position.x);value(v.position.y);value(v.position.z);}
  for(const auto &t:world.targets){value(t.origin.x);value(t.origin.y);value(t.origin.z);value(t.half.x);value(t.half.y);value(t.half.z);}
  return hash;
}
}
int main(){try{
  std::array<int,3> ids{3,28,26};std::set<uint64_t> locations;
  for(int location=0;location<10;++location)for(int biome=0;biome<8;++biome){
    World world;buildMissionEnvironment(world,723,biome,500,ids,location,0,0);
    check(world.maxWadeDepth==6&&world.proceduralTerrain,"RPG map lost waist-depth physical terrain");
    check(world.targets.size()>ids.size()+8&&!world.geometry().terrain.empty(),"Location has no physical scenery");
    std::set<unsigned> visited;std::queue<unsigned> nodes;nodes.push(0);visited.insert(0);
    while(!nodes.empty()){auto here=nodes.front();nodes.pop();for(auto edge:world.interiorNavLinks){
      unsigned next=edge[0]==here?edge[1]:edge[1]==here?edge[0]:unsigned(world.interiorNavNodes.size());
      if(next<world.interiorNavNodes.size()&&visited.insert(next).second)nodes.push(next);
    }}
    check(visited.size()==world.interiorNavNodes.size(),"Location road/room navigation graph has disconnected sections");
    for(size_t i=0;i<ids.size();++i)check(world.targets[i].health==float(enemySpec(ids[i]).hd*8),"Interior/scenery changed enemy target prefix");
    for(size_t i=world.targets.size()-8;i<world.targets.size();++i)check(!world.targets[i].active,"Deployable tail slots lost stable binding");
    for(unsigned slot=0;slot<4;++slot){auto entry=missionEntryPosition(world,slot);
      check(clearPosition(world,entry,3.5f),"Four-pilot insertion overlaps terrain/scenery");
      check(!world.indoors(entry)&&!std::isfinite(world.interiorCeilingAt(entry)),"Combat drop entry must have clear sky");}
    if(biome==3){locations.insert(signature(world));World repeat;
      buildMissionEnvironment(repeat,723,biome,500,ids,location,0,0);
      check(signature(repeat)==signature(world),"Procedural map differs across identical network reconstruction");}
    if(location==4&&biome!=1&&biome!=7&&biome!=4){
      bool deep=false,shallow=false;
      for(const auto &body:world.waterRegions)for(float x:{.2f,.4f,.6f,.8f})for(float z:{.2f,.4f,.6f,.8f}){
        Vec3 p{body.bounds[0]+x*(body.bounds[2]-body.bounds[0]),0,body.bounds[1]+z*(body.bounds[3]-body.bounds[1])};
        p.y=world.surfaceHeight(p.x,p.z);auto depth=world.waterDepthAt(p,3.5f);
        if(depth>6.1f&&world.waterTraversalLimitAt(p,3.5f)<=6){deep=true;check(!world.groundPassable(p),"Deep coast became walkable");}
        if(depth>2&&depth<=6&&world.groundPassable(p)){shallow=true;check(world.waterMovementScale(p)<1,"Waist-deep water does not slow movement");}
      }
      check(deep&&shallow,"Docks require shallow coast and blocked deep-water channel");
    }
  }
  check(locations.size()==10,"Mission location selection does not change physical generation");
  // A regular ship, fortress hangar and underground tunnel network each expose
  // variable connected rooms, actual portal voids and a clear outdoor entry court.
  for(auto [location,region]:std::array<std::array<int,2>,3>{{{7,0},{6,2},{1,3}}}){
    World world;std::array<int,0> empty{};buildMissionEnvironment(world,723,3,500,empty,location,0,region);
    check(world.interiorRegions.size()>=12&&world.interiorNavNodes.size()>=20,"Interior lacks subsidiary spaces and explicit connected passages");
    float low=1e9f,high=-1e9f;
    for(auto p:world.interiorNavNodes){low=std::min(low,p.y);high=std::max(high,p.y);}
    check(high-low>8,"Interior does not provide physically distinct usable elevation tiers");
    check(world.interiorNavLinks.size()>=world.interiorNavNodes.size()+2,"Site graph lacks alternate loops and branches");
    Vec3 entry=missionEntryPosition(world,0);
    for(const auto &target:world.targets)if(target.invulnerable&&target.active){
      check(!(target.half.x>10&&target.half.z>10&&target.half.y>8),"Whole-room collider blocks an interior void");
      for(const auto &v:*target.mesh){auto p=target.origin+rotate(angles(target.yaw),v.position);
        check(std::abs(p.x-target.center.x)<=target.half.x+.001f&&std::abs(p.y-target.center.y)<=target.half.y+.001f&&std::abs(p.z-target.center.z)<=target.half.z+.001f,"Module visual mesh and structural collider differ");}
    }
    for(const auto &site:world.interiorNavNodes) {
      check(std::abs(site.y-world.surfaceHeight(site.x,site.z))<.01f,"Site anchor does not match authoritative tier floor");
      connected(world,entry,site);
    }
    // Walk actual physics up a ramp between district centres. A graph label
    // alone must not turn inaccessible roof geometry into a playable level.
    auto ramp=world.interiorNavLinks.front();float rise=0;
    for(auto edge:world.interiorNavLinks){float delta=std::abs(world.interiorNavNodes[edge[0]].y-world.interiorNavNodes[edge[1]].y);
      if(delta>rise){rise=delta;ramp=edge;}}
    check(rise>1,"Interior has no graded connected route between elevations");
    World walker=world;walker.position=world.interiorNavNodes[ramp[0]];
    Vec3 summit=world.interiorNavNodes[ramp[1]];
    const int ticks=int(std::ceil(length(walker.position-summit)*3))+100;
    for(int tick=0;tick<ticks&&length(walker.position-summit)>.5f;++tick) {
      Vec3 delta=summit-walker.position;delta.y=0;
      walker.movePilot(walker.position+unit(delta)*.5f);
    }
    check(length(walker.position-summit)<1,"Pilot physics cannot traverse a graded district route");
    Vec3 indoorPoint{};bool shelter=false;
    for(auto p:world.interiorNavNodes){float ceiling=world.interiorCeilingAt(p);
      if(std::isfinite(ceiling)){
        check(ceiling-p.y>=28,"Connected indoor route lacks full-mech headroom");
        if(ceiling-p.y>=40&&world.indoors(p+Vec3{0,12,0})){indoorPoint=p+Vec3{0,12,0};shelter=true;}
      }}
    check(shelter,"Interior has no usable roofed room fitting catalog giants");
    world.environment={Lighting::Day,Weather::Storm,723};world.climate=Climate::Temperate;
    check(world.environmentAt(indoorPoint).weather==Weather::Clear,"Rain appears inside enclosed room");
    check(world.environmentAt(entry).weather==Weather::Storm,"Indoor shelter disables weather outside entry courtyard");
    for(int objective=0;objective<10;++objective){
      World missionWorld;buildMissionEnvironment(missionWorld,723,3,500,ids,location,objective,region);
      MissionState state;state.recipe.objective=objective;addMissionAssets(missionWorld,state,500);
      for(int id:state.assets){auto goal=missionNavigablePosition(missionWorld,missionWorld.targets[id].origin+Vec3{0,0,18});
        connected(missionWorld,missionEntryPosition(missionWorld,0),goal);}
      if(objective==2)connected(missionWorld,{0,0,275},{0,0,-325});
    }
  }
  // Graded shallow banks are physical walkable beds. Legacy/no-wading worlds
  // and deep channels remain blocked; bullets still contact the water surface.
  for(int version:{3,4})for(float depth:{6.f,12.f}){
    SkirmishSettings settings;settings.seed=723;settings.terrain="urban";settings.water="none";
    auto recipe=generateSkirmishRecipe(settings);recipe["version"]=version;recipe["roads"]=Json::array();
    for(auto &plot:recipe["plots"])plot["buildings"]=0;
    recipe["terrain"]["water"]=Json::array({{{"bounds",{-80,-80,80,80}},{"level",0},{"depth",depth},{"bank_width",32}}});
    World world;world.targets.clear();world.movementBounds=recipe.at("bounds").get<std::array<float,4>>();
    world.maxWadeDepth=6;world.rpgPrototype=true;buildProceduralTerrain(world,recipe);
    check(std::abs(world.surfaceHeight(0,0)+depth)<.01f,"Water bed does not match physical depth");
    if(depth==6){
      world.position={-100,0,20};
      for(int step=0;step<100;++step)world.movePilot(world.position+Vec3{1,0,0});
      check(world.position.x>-.1f&&std::abs(world.position.y+6)<.01f,"Pilot cannot descend shallow bank into waist-depth bed");
      check(world.waterMovementScale(world.position)<.7f&&world.groundPassable(world.position),"Shallow-water locomotion profile incorrect");
      auto contact=world.groundContact({0,.1f,0},{0,-1,0});
      check(contact>0&&contact<1,"Wading incorrectly removes physical water surface contact");
      Target human;human.spawnType=0;human.origin={-30,-6,20};human.center=human.origin+Vec3{0,1,0};human.half={1,1,1};
      Target mech;mech.spawnType=3;mech.origin={30,-6,20};mech.center=mech.origin+Vec3{0,6,0};mech.half={3,6,3};
      world.targets={human,mech};world.updateEnemyBounds(world.targets[0]);world.updateEnemyBounds(world.targets[1]);
      check(!world.moveEnemy(0,{1,0,0},.1f,false),"Human strike team can fully submerge in mech waist-depth water");
      check(world.moveEnemy(1,{1,0,0},.1f,false),"12m mech cannot wade in its safe half-depth water");
      check(std::abs(world.targets[1].origin.x-30.65f)<.01f,"Ground NPC wading slowdown applied incorrectly v"+std::to_string(version)+" x="+std::to_string(world.targets[1].origin.x)+" depth="+std::to_string(world.waterDepthAt({30,-6,20},1.95f)));
      world.maxWadeDepth=0;check(!world.groundPassable({0,-6,0}),"Legacy water blockade changed implicitly");
    } else {
      check(!world.groundPassable({0,-12,0}),"Deep water is traversable below cockpit-safe depth");
      world.waterRegions[0].bounds={2,-80,3,80};
      check(world.waterDepthAt({0,-12,0},3.5f)>6&&!world.groundPassable({0,-12,0},3.5f),"Thin deep channel disappears between footprint samples");
    }
  }
  // Water insertion is an actual shallow flooded entry, not a camera effect
  // over a dry courtyard. All four pilots begin on the same valid wet bed.
  for(float extent:{500.f,1000.f}) {
    World canal,repeat;std::array<int,0> empty{};
    buildMissionEnvironment(canal,42,3,extent,empty,4,0,0,4);
    buildMissionEnvironment(repeat,42,3,extent,empty,4,0,0,4);
    check(signature(canal)==signature(repeat),"Canal entry is not deterministically reconstructed");
    for(unsigned slot=0;slot<4;++slot){auto p=missionEntryPosition(canal,slot);
      check(canal.groundPassable(p)&&clearPosition(canal,p,3.5f),"Canal insertion footprint is not valid");
      check(std::abs(canal.waterDepthAt(p,3.5f)-2.75f)<.01f,"Water opener entry is dry or wrong depth");
      check(canal.hasWater(p.x,p.z,3.5f),"Submerged road still reports a dry bridge");
      auto exit=p+Vec3{0,0,-100};exit.y=canal.surfaceHeight(exit.x,exit.z);
      int samples=100;for(int i=0;i<=samples;++i){auto sample=p+(exit-p)*(float(i)/samples);sample.y=canal.surfaceHeight(sample.x,sample.z);
        check(canal.groundPassable(sample),"Canal bank blocks connected exit");}
    }
  }
  for(auto spec:std::array<std::array<int,3>,4>{{{0,3,0},{1,3,0},{3,7,0},{3,1,3}}}) {
    World dry;std::array<int,0> empty{};buildMissionEnvironment(dry,42,spec[0],500,empty,spec[1],0,spec[2],4);
    check(dry.waterDepthAt(missionEntryPosition(dry,0))==0,"Water opener forced a canal into inappropriate climate/interior");
  }
  std::cout<<"80 location/biome combinations, variable traversable elevation tiers, connected subsidiary spaces and objectives, indoor weather, waist-depth banks and blocked deep water passed\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
