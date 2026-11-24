#include "simulation.hpp"
#include <cassert>
#include <iostream>
using namespace iron;
static void kill(World& w,Target& t){t.health=0;w.targetDestroyed(t);}
int main(int argc,char** argv){assert(argc==2);
 for(int mission=0;mission<16;++mission){World w;w.loadMission(argv[1],mission);World failure=w;
  auto killType=[&](int type){for(auto& t:w.targets)if(t.spawnType==type&&!t.generated)kill(w,t);};
  auto killTag=[&](int tag){for(auto& t:w.targets)if(t.spawnType<0&&t.tag==tag)kill(w,t);};
  auto supply=[&](int kind){w.pickups.push_back({w.position,kind});w.collectPickups();};
  switch(mission){
  case 0:killTag(32);w.evaluateMission();assert(w.missionStatus==MissionStatus::Active);supply(21);break;
  case 1:killType(9);assert((w.missionFlags&3)==3);{Target mech;mech.spawnType=3;++w.counters[0];w.evaluateMission();assert(w.missionStatus==MissionStatus::Active);kill(w,mech);}break;
  case 2:killTag(32);break;
  case 3:killTag(32);assert(w.counters[1]==0);killTag(22);for(auto& p:w.pickups)if(p.kind==22)p.position=w.position;w.collectPickups();w.evaluateMission();assert(w.missionStatus==MissionStatus::Active);supply(8);break;
  case 4:killType(3);break;
  case 5:for(int i=0;i<20;++i){Target tank;tank.spawnType=0;kill(w,tank);}break;
  case 6:killType(8);w.evaluateMission();assert(w.missionStatus==MissionStatus::Active);supply(14);break;
  case 7:killType(9);break;
  case 8:killType(3);w.evaluateMission();assert(w.missionStatus==MissionStatus::Active);supply(20);break;
  case 9:for(int tag=32;tag<=36;++tag)killTag(tag);killType(1);assert((w.missionFlags&31)==31);break;
  case 10:for(auto& t:w.targets)if(t.spawnType<0&&t.origin.x>=14.08f&&t.origin.x<=290.56f&&t.origin.z>=-71.68f&&t.origin.z<=102.4f)kill(w,t);break;
  case 11:killType(3);break;
  case 12:for(auto& t:w.targets)if(t.spawnType<0&&t.tag<128)kill(w,t);break;
  case 13:for(auto& t:w.targets)if(t.spawnType==8){w.arrivedEnemy(t);t.escaped=true;}break;
  case 14:killType(9);break;
  case 15:killType(3);killTag(33);break;
  }
  w.effects.clear();w.evaluateMission();if(w.missionStatus!=MissionStatus::Complete){std::cerr<<"IS1 mission "<<mission+1<<" counters="<<w.counters[0]<<","<<w.counters[1]<<","<<w.counters[2]<<" flags="<<w.missionFlags<<"\n";return 1;}
  if(mission==1||mission==7){failure.elapsed=mission==1?480:750;failure.evaluateMission();assert(failure.missionStatus==MissionStatus::Failed);}
  if(mission==5||mission==11){for(auto& t:failure.targets)if(t.protectedTarget)kill(failure,t);assert(failure.missionStatus==MissionStatus::Failed);}
  if(mission==6){auto& t=*std::find_if(failure.targets.begin(),failure.targets.end(),[](auto& t){return t.spawnType==8;});failure.arrivedEnemy(t);failure.evaluateMission();assert(failure.missionStatus==MissionStatus::Failed);}
  if(mission==13){for(auto& t:failure.targets)if(t.spawnType==8)kill(failure,t);failure.missionFlags|=1u<<30;failure.evaluateMission();assert(failure.missionStatus==MissionStatus::Failed);}
 }
 World settling;settling.loadMission(argv[1],2);for(auto& t:settling.targets)if(t.tag==32)kill(settling,t);settling.evaluateMission();assert(settling.missionStatus==MissionStatus::Active);for(int n=0;n<180;++n)settling.stepProjectiles(.02f);settling.evaluateMission();assert(settling.missionStatus==MissionStatus::Complete);
 // Shared collection advances the mission once, but each player's refill remains instanced.
 World coop;coop.loadMission(argv[1],3);coop.cooperative=true;coop.pickups.push_back({coop.position,22});int count=coop.counters[0];coop.collectPickups();coop.playerSlot=1;coop.collectPickups();assert(coop.counters[0]==count-1);
 std::cout<<"All 16 IS1 objective event paths, timers, protection and shared collection passed\n";
}
