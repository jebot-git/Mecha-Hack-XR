#include "rpg/enemy_navigation.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace iron;using namespace iron::rpg;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
Target actor(Vec3 p,Vec3 half={2,4,2}){Target t;t.spawnType=3;t.origin=p;t.center=p+Vec3{0,half.y,0};t.half=half;t.ai.boundsReady=true;t.ai.localCenter={0,half.y,0};t.ai.localHalf=half;return t;}
Target wall(Vec3 center,Vec3 half){Target t;t.spawnType=-1;t.origin=center-Vec3{0,half.y,0};t.center=center;t.half=half;t.invulnerable=true;return t;}
void floorQuad(World &w,float x0,float x1,float z0,float z1,float low,float high){
  Vec3 a{x0,low,z0},b{x1,high,z0},c{x1,high,z1},d{x0,low,z1};
  for(auto p:{a,b,c,a,c,d})w.editGeometry().terrain.push_back({p,{.2f,.2f,.2f}});
}
World base(){World w;w.targets.clear();w.position={120,0,120};w.movementBounds={-150,-150,150,150};w.targets.push_back(actor({-35,0,0}));return w;}
}
int main(){try{
 {
  auto w=base();auto &tank=w.targets[0];
  const float dt=.05f;
  auto side=tankDriveStep(w,0,{1,0,0},{0,0,-1},{},8,dt,true,false);
  check(side.x>0&&side.x<8*dt&&std::abs(tank.yaw)<.001f,"Hover skirt cannot build lateral thrust facing its target");
  tank.ai.velocity={};tank.yaw=0;
  auto pivot=tankDriveStep(w,0,{1,0,0},{1,0,0},{},8,dt,false,false);
  check(length(pivot)<.001f&&tank.yaw<0&&tank.yaw>-.1f,"Treads failed to pivot before moving sideways");
  tank.yaw=0;
  auto reverse=tankDriveStep(w,0,{0,0,1},{0,0,-1},{},8,dt,false,true);
  check(reverse.z>0&&reverse.z<8*dt&&std::abs(tank.yaw)<.001f,"Tracked egress did not reverse under traction");
 }
 {
  auto w=base();
  w.rpgPrototype=true;w.proceduralTerrain=true;w.editGeometry().terrain.clear();
  // A continuous bank exercises independent pitch and roll; neither can
  // come from a locomotion oscillator or the turret's independent aiming.
  for(const auto p:{Vec3{-40,-8.8f,-40},Vec3{40,3.2f,-40},Vec3{40,8.8f,40},
                    Vec3{-40,-8.8f,-40},Vec3{40,8.8f,40},Vec3{-40,-3.2f,40}})
    w.editGeometry().terrain.push_back({p,{}});
  w.indexTerrain();
  auto &tank=w.targets[0];
  tank.origin={0,0,15};tank.yaw=0;
  const auto expected=unit(Vec3{-.15f,1,-.07f});
  for(int i=0;i<60;++i){
    check(w.moveEnemy(0,{0,0,-.14f},1.f/60,false,false,true),"Tracked chassis could not roll up a bank");
    check(std::abs(tank.origin.y-w.walkableHeight(tank.origin))<.001f,"Tracked chassis lost ground contact");
  }
  const auto chassisNormal=rotate(angles(tank.yaw,tank.ai.pitch,tank.ai.bank),Vec3{0,1,0});
  check(dot(chassisNormal,expected)>.9999f,"Tracked chassis did not settle to its terrain support plane");
  check(tank.ai.gaitPhase>2,"Tracked motion phase did not advance with ground travel");
 }
 {
  auto w=base();
  w.rpgPrototype=true;w.proceduralTerrain=true;
  w.interiorDecks.push_back({{-30,-10,30,10},{-30,8,0},{30,8,0}});
  auto &tank=w.targets[0];
  tank.origin={0,8,0};tank.center={0,12,0};tank.yaw=-pi/2;
  // Renderable targets also pass through updateEnemyBounds each tick.
  tank.mesh=std::make_shared<WorldMesh>(1);
  for(int i=0;i<20;++i){
    check(w.moveEnemy(0,{.18f,0,0},1.f/60,false,false,true),"Tracked chassis could not cross an elevated deck");
    check(std::abs(tank.origin.y-8)<.001f,"Rebuilding tracked bounds pulled the chassis down through a raised deck");
    check(std::abs(tank.ai.pitch)<.001f&&std::abs(tank.ai.bank)<.001f,"Level deck introduced a chassis oscillation");
  }
 }
 {
  auto w=base();w.targets.push_back(wall({0,10,0},{3,10,20}));
  check(!enemyNavigationPassage(w,0,w.targets[0].origin,{35,0,0},false),"Wall accepted as a clear route");
  EnemyNavigationState cache;float travelled=0,maxSide=0;
  for(int tick=0;tick<1000;++tick){auto start=w.targets[0].origin;auto toward=Vec3{35,0,0}-start;if(length(toward)<2)break;
    auto d=enemyNavigationDirection(w,0,unit(toward),{35,0,0},8,.05f,false,cache);
    check(std::isfinite(length(d)),"Nonfinite wall steering");
    if(length(d)>.001f){check(w.moveEnemy(0,d*.4f,.05f,false),"Route steered into an obstacle");travelled+=length(w.targets[0].origin-start);}
    maxSide=std::max(maxSide,std::abs(w.targets[0].origin.z));
  }
  if(length(w.targets[0].origin-Vec3{35,0,0})>=3){const auto &p=w.targets[0].origin;std::cerr<<"wall diagnostic position="<<p.x<<","<<p.y<<","<<p.z<<" travel="<<travelled<<" maxSide="<<maxSide<<" cursor="<<cache.cursor<<" path="<<cache.path.size()<<" blocked="<<w.targets[0].ai.blocked<<'\n';}
  check(length(w.targets[0].origin-Vec3{35,0,0})<3,"Exterior A* failed to go around wall without authored nodes");
  check(maxSide>21&&travelled>70,"Wall route did not make a real detour");
 }
 {
  auto w=base();w.targets[0]=actor({-6,0,0});w.targets.push_back(wall({0,10,0},{3,10,20}));EnemyNavigationState cache;
  EnemyNavigationBudget none{0,0};auto d=enemyNavigationDirection(w,0,{1,0,0},{35,0,0},8,.05f,false,cache,&none);
  check(cache.searching&&cache.path.empty(),"Zero shared budget still performs a synchronous route search");
  check(none.edgeChecks==0&&none.terrainSamples==0,"Exhausted planning quota was exceeded");
  check(length(d)>.1f,"Pending planning budget unnecessarily freezes safe local movement");
  bool complete=false;
  for(int n=0;n<80;++n){EnemyNavigationBudget budget{2,48};
    enemyNavigationDirection(w,0,{1,0,0},{35,0,0},8,.05f,false,cache,&budget);
    check(budget.edgeChecks>=0&&budget.edgeChecks<=2&&budget.terrainSamples>=0&&budget.terrainSamples<=48,"Incremental search exceeded deterministic planning quota");
    if(!cache.searching&&!cache.path.empty()){complete=true;break;}}
  check(complete,"Bounded incremental planner did not finish wall detour across ticks");
 }
 {
  auto w=base();w.movementBounds={-500,-500,500,500};w.targets[0]=actor({-8,0,0});w.targets.push_back(wall({0,10,0},{3,10,300}));
  EnemyNavigationState cache;bool resumed=false,complete=false;
  for(int n=0;n<1000;++n){EnemyNavigationBudget budget{4,64};
    enemyNavigationDirection(w,0,{1,0,0},{80,0,0},8,.05f,false,cache,&budget);
    resumed=resumed||cache.searchEdgeSample>0;
    check(budget.edgeChecks>=0&&budget.terrainSamples>=0,"Long-edge continuation exceeded planning quota");
    if(!cache.searching&&!cache.path.empty()){complete=true;break;}}
  check(resumed,"Long wall did not exercise resumable terrain validation");
  check(complete,"Incremental planner lost routes with segments longer than192metres");
  bool around=false;for(auto p:cache.path)around=around||std::abs(p.z)>300;
  check(around,"Long wall route did not reach its actual far corner");
 }
 {
  auto w=base();EnemyNavigationState cache;
  for(Vec3 desired:std::array<Vec3,4>{{{1,0,0},{0,0,1},{-1,0,0},{0,0,-1}}}){
    auto result=enemyNavigationDirection(w,0,desired,{35,0,0},8,.05f,false,cache);
    check(length(result-desired)<.0001f,"Free-space route overrode changing combat strafe direction");
    check(!cache.searching&&cache.searchNodes.empty(),"Free-space strafe unnecessarily initializes a route graph");
  }
 }
 {
  auto w=base();w.targets[0]=actor({-35,0,0},{2,4,2});w.targets.push_back(wall({0,10,-25},{3,10,21}));w.targets.push_back(wall({0,10,25},{3,10,21}));
  check(enemyNavigationPassage(w,0,{-35,0,0},{35,0,0},false),"Small unit cannot cross adequate doorway");
  w.targets[0]=actor({-35,0,0},{8,8,8});
  check(!enemyNavigationPassage(w,0,{-35,0,0},{35,0,0},false),"Large unit uses small-unit clearance");
 }
 {
  auto w=base();w.targets.push_back(actor({-31,0,0}));EnemyNavigationState cache;
  auto d=enemyNavigationDirection(w,0,{1,0,0},{35,0,0},8,.1f,false,cache);
  check(length(d)>.1f&&std::abs(d.z)>.1f,"Neighbor did not produce a local sidestep");
  check(enemyNavigationPassage(w,0,w.targets[0].origin,w.targets[0].origin+d*.8f,false,true),"Avoidance outputs colliding displacement");
  auto hold=enemyNavigationDirection(w,0,{},{35,0,0},8,.1f,false,cache);check(length(hold)==0,"Navigation overrode tactical hold");
 }
 {
  auto w=base();w.interiorRegions.push_back({{-10,-20,10,20},0,7});
  check(!enemyNavigationPassage(w,0,{-35,0,0},{35,0,0},false),"Unit taller than interior ceiling enters tunnel");
  w.targets[0]=actor({-35,0,0},{2,2,2});check(enemyNavigationPassage(w,0,{-35,0,0},{35,0,0},false),"Short unit rejected by sufficient tunnel clearance");
  w.targets[0]=actor({-35,20,0},{2,4,2});w.targets.push_back(wall({0,5,0},{3,5,20}));
  // A low obstacle may be flown over; a low ceiling still cannot be crossed.
  w.interiorRegions.clear();check(enemyNavigationPassage(w,0,{-35,20,0},{35,20,0},true),"Aircraft cannot clear low obstacle vertically");
 }
 {
  auto w=base();w.proceduralTerrain=true;
  floorQuad(w,-100,-30,-60,60,0,0);floorQuad(w,-30,30,-60,60,0,12);floorQuad(w,30,100,-60,60,12,12);w.indexTerrain();
  w.interiorNavNodes={{-30,0,0},{0,6,0},{30,12,0}};w.interiorNavLinks={{{0,1}},{{1,2}}};
  check(enemyNavigationPassage(w,0,{-35,0,0},{35,12,0},false),"Valid terrain ramp rejected between floor levels");
  EnemyNavigationState cache;
  for(int n=0;n<1000&&w.targets[0].origin.x<34;++n){auto delta=Vec3{35,12,0}-w.targets[0].origin;delta.y=0;
    auto d=enemyNavigationDirection(w,0,unit(delta),{35,12,0},8,.05f,false,cache);
    check(w.moveEnemy(0,d*.4f,.05f,false),"Ramp navigation blocked by terrain movement");}
  check(w.targets[0].origin.x>34&&std::abs(w.targets[0].origin.y-12)<.01f,"Actor did not physically climb to upper level");
  // A discontinuous cliff at the same upper floor is not a traversable ramp.
  w.editGeometry().terrain.clear();floorQuad(w,-100,0,-60,60,0,0);floorQuad(w,0,100,-60,60,12,12);w.indexTerrain();
  check(!enemyNavigationPassage(w,0,{-35,0,0},{35,12,0},false),"Route crossed an abrupt floor discontinuity");
 }
 {
  auto w=base();w.maxWadeDepth=6;ProceduralWater water;water.bounds={-10,-20,10,20};water.level=10;water.depth=10;w.waterRegions.push_back(water);
  check(!enemyNavigationPassage(w,0,{-35,0,0},{35,0,0},false),"Ground route crosses water deeper than body/wading limit");
  check(enemyNavigationPassage(w,0,{-35,20,0},{35,20,0},true),"Air route inherits ground wading restriction");
  w.waterCrossings.push_back({-12,-5,12,5});
  check(enemyNavigationPassage(w,0,{-35,0,0},{35,0,0},false)==false,"Dry bridge marking alone bypasses deep-water physical floor requirement");
 }
 {
  auto w=base();EnemyNavigationState cache;float bad=std::numeric_limits<float>::quiet_NaN();
  check(length(enemyNavigationDirection(w,0,{bad,0,0},{},8,.1f,false,cache))==0,"Nonfinite direction enters steering");
  check(!enemyNavigationPassage(w,0,{-35,0,0},{bad,0,0},false),"Nonfinite navigation target accepted");
  check(!enemyNavigationPassage(w,0,{-35,0,0},{151,0,0},true),"Aircraft route escaped movement bounds");
 }
 {
  // A crowded room has more nearby staging points than the sparse neighbor
  // quota. Its authored farther doorway link must remain reachable in the
  // responsive first pass, rather than requiring the dense fallback graph.
  auto w=base();w.targets[0]=actor({-6,0,0});w.targets.push_back(wall({0,10,-60},{3,10,90}));
  w.interiorNavNodes.push_back(w.targets[0].origin);
  for(int n=0;n<16;++n)w.interiorNavNodes.push_back({-6.5f-.4f*float(n%4),0,-1.5f+float(n/4)});
  unsigned door=unsigned(w.interiorNavNodes.size());w.interiorNavNodes.push_back({-6,0,40});
  unsigned beyond=unsigned(w.interiorNavNodes.size());w.interiorNavNodes.push_back({8,0,40});
  w.interiorNavLinks={{{0,door}},{{door,beyond}}};
  check(!enemyNavigationPassage(w,0,w.targets[0].origin,{20,0,0},false),"Clustered doorway fixture has no actual separating wall");
  check(enemyNavigationPassage(w,0,w.interiorNavNodes[0],w.interiorNavNodes[door],false)&&
        enemyNavigationPassage(w,0,w.interiorNavNodes[door],w.interiorNavNodes[beyond],false),"Authored doorway fixture does not contain a physically clear link");
  EnemyNavigationState cache;bool complete=false;
  EnemyNavigationBudget firstEdge{1,64};
  enemyNavigationDirection(w,0,{1,0,0},{20,0,0},8,.05f,false,cache,&firstEdge);
  bool retainedLink=false;
  for(auto neighbor:cache.searchNeighbors)retainedLink=retainedLink||length(cache.searchNodes[neighbor]-w.interiorNavNodes[door])<.01f;
  check(retainedLink,"Sparse expansion omitted a farther authored doorway behind sixteen nearer staging points");
  for(int tick=0;tick<800;++tick){EnemyNavigationBudget budget{4,64};
    enemyNavigationDirection(w,0,{1,0,0},{20,0,0},8,.05f,false,cache,&budget);
    check(budget.edgeChecks>=0&&budget.terrainSamples>=0,"Clustered doorway exceeded deterministic planning quota");
    if(!cache.searching&&!cache.path.empty()){complete=true;break;}
  }
  check(complete,"Crowded room lost its sole farther authored doorway link");
  check(!cache.searchFallback,"Authored doorway was omitted from the responsive sparse search");
  bool passedDoor=false;for(auto point:cache.path)passedDoor=passedDoor||point.z>30;
  check(passedDoor,"Crowded room route does not traverse its only physical doorway");
  auto previous=w.targets[0].origin;
  for(auto point:cache.path){check(enemyNavigationPassage(w,0,previous,point,false),"Crowded doorway route contains an invalid edge");previous=point;}
 }
 std::cout<<"Enemy navigation scenarios passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
