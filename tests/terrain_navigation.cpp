#include "simulation.hpp"
#include <iostream>
#include <stdexcept>
using namespace iron;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void quad(World& w,float x0,float z0,float x1,float z1,float low,float high){
    Vec3 a{x0,low,z0},b{x1,high,z0},c{x1,high,z1},d{x0,low,z1};for(auto p:{a,b,c,a,c,d})w.editGeometry().terrain.push_back({p,{.3f,.4f,.2f}});
}
int main(){try{
    World w;w.targets.clear();w.editGeometry().terrain.clear();w.proceduralTerrain=true;w.mission=0;w.missionStatus=MissionStatus::Active;
    quad(w,-100,-100,100,100,-7,-7);w.indexTerrain();check(std::abs(w.surfaceHeight(0,0)+7)<.001f,"lake bed must retain negative elevation");
    ProceduralWater lake;lake.bounds={-10,-10,10,10};lake.level=-1;lake.depth=6;w.waterRegions.push_back(lake);
    check(w.hasWater(0,0)&&!w.groundPassable({0,-7,0},2),"water cannot be traversed as dry land");
    w.waterCrossings.push_back({-20,-4,20,4});quad(w,-20,-4,20,4,0,0);w.indexTerrain();
    check(!w.hasWater(0,0,2)&&w.groundPassable({0,0,0},2),"road crossing must support the full footprint");
    check(w.hasWater(0,3,2),"crossing edge cannot hide water under half a footprint");
    w.waterCrossings.back()={-12,-4,12,4};
    check(!w.hasWater(-12,0,3.5f)&&!w.hasWater(12,0,3.5f),"dry bank outside bridge ends must not block entry");
    check(w.hasWater(-12,3,3.5f),"bank approach still requires support over every wet corner");
    w.position={-15,0,0};w.movePilot({15,0,0});check(w.position.x==15&&w.position.y==0,"player must cross water along the dry road");
    w.position={-15,-7,8};w.movePilot({15,-7,8});check(w.position.x==-15,"swept player movement cannot jump over water");
    Target tank;tank.spawnType=0;tank.origin=tank.center={-15,-6,8};tank.half={1,1,1};w.targets={tank};w.updateEnemyBounds(w.targets[0]);
    check(!w.moveEnemy(0,{30,0,0},.1f,false)&&w.targets[0].ai.gaitPhase==0,"ground enemy cannot cross water or advance blocked gait");
    w.targets[0].spawnType=1;w.targets[0].origin.y=40;w.updateEnemyBounds(w.targets[0]);check(w.moveEnemy(0,{30,0,0},.1f,true)&&w.targets[0].origin.y==40,"air unit may cross water without grounding");
    w.targets.clear();w.waterRegions.clear();w.waterCrossings.clear();w.editGeometry().terrain.clear();quad(w,-20,-20,20,20,0,4);w.indexTerrain();w.position={-10,1,0};w.chassisYaw=-pi/2;w.missionPrototype=w.proceduralLayout=true;
    Input walk;walk.forward=1;w.step(walk,.02f);check(w.position.x>-10&&std::abs(w.position.y-w.surfaceHeight(w.position.x,w.position.z))<.001f,"player feet follow graded terrain");check(w.walkingSpeed>0&&w.gaitPhase>0,"actual travel drives walking animation");
    auto phase=w.gaitPhase;Input turn;turn.turn=1;w.step(turn,.02f);check(w.walkingSpeed==0&&w.gaitPhase==phase&&std::abs(w.torsoYaw)>.001f,"stationary torso rotation must leave legs idle");
    w.editGeometry().terrain.clear();quad(w,-100,-100,100,100,4,4);w.indexTerrain();w.effects.clear();w.projectiles={{{0,4.1f,0},{0,-20,0},1,5,RightHand}};w.stepProjectiles(.02f);check(w.projectiles.empty()&&!w.effects.empty()&&std::abs(w.effects.back().position.y-4)<.001f,"projectile contact follows terrain above zero");
    w.editGeometry().terrain.clear();quad(w,-100,-100,100,100,-7,-7);w.indexTerrain();w.waterRegions={lake};w.effects.clear();w.projectiles={{{0,-.9f,0},{0,-20,0},1,5,RightHand}};w.stepProjectiles(.02f);check(w.projectiles.empty()&&!w.effects.empty()&&std::abs(w.effects.back().position.y+1)<.001f,"projectile contacts water surface rather than lake bed");
    std::cout<<"Negative terrain, water/road footprints, swept ground movement, air passage, graded feet, locomotion and projectile contacts passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
