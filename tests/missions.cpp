#include "simulation.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace iron;
int main(int argc,char** argv){
    assert(argc==2);World w;Input in;
    for(int i=0;i<16;++i){
        w=World{};w.loadMission(argv[1],i);
        assert(w.missionStatus==MissionStatus::Active&&w.targets.size()>50&&!w.geometry().terrain.empty());
        assert(std::isfinite(w.position.x)&&length(w.groundColor)>0);
        for(auto& t:w.targets){assert(t.mesh&&!t.mesh->empty());assert(length(t.half)>0);
            for(auto& v:*t.mesh){assert(std::isfinite(length(v.position)));assert(v.color.x>=0&&v.color.x<=1&&v.color.y>=0&&v.color.y<=1&&v.color.z>=0&&v.color.z<=1);if(v.textured){assert(v.u>=0&&v.u<=1&&v.v>=0&&v.v<=1);}}}
        auto elapsed=w.elapsed;in.focused=false;w.step(in,.02f);assert(w.elapsed==elapsed);in.focused=true;
        w.step(in,.02f);assert(w.elapsed>elapsed);
        w.counters={0,0,0};w.missionFlags=0xffffffffu;
        if(i==5||i==11)w.missionFlags&=~1u;
        if(i==6)w.missionFlags&=~(1u<<30);
        if(i==13)w.counters[0]=1;
        if(i==9)for(auto& t:w.targets)if(t.spawnType==1)t.health=0;
        w.evaluateMission();assert(w.missionStatus==MissionStatus::Complete);
    }
    w=World{};w.loadMission(argv[1],0);auto& warehouse=*std::find_if(w.targets.begin(),w.targets.end(),[](auto& t){return t.tag==32;});
    int before=w.counters[0];w.targetDestroyed(warehouse);w.targetDestroyed(warehouse);assert(w.counters[0]==before-1); // one death, one decrement
    w.counters[0]=0;w.missionFlags=0;w.evaluateMission();assert(w.missionStatus==MissionStatus::Active); // supplies required
    w.effects.clear();w.pickups.push_back({w.position,21});w.stepMission(.01f);assert(w.missionStatus==MissionStatus::Complete);
    w=World{};w.loadMission(argv[1],1);w.elapsed=480;w.evaluateMission();assert(w.missionStatus==MissionStatus::Failed);
    w=World{};w.loadMission(argv[1],5);w.missionFlags=1;w.counters[0]=0;w.evaluateMission();assert(w.missionStatus==MissionStatus::Failed); // failure wins tie
    w=World{};w.loadMission(argv[1],7);w.hurt(100);assert(w.missionStatus==MissionStatus::Ejecting&&w.health==0&&w.lifeSpent);
    auto deathPosition=w.position;auto ammunition=w.ammo;in.fire.fill(true);in.forward=1;w.missile.active=false;
    in.focused=false;w.step(in,.05f);assert(w.ejectionTime==.05f);in.focused=true; // forced ejection continues through focus loss
    for(int i=0;i<160;++i)w.step(in,.05f);
    assert(w.missionStatus==MissionStatus::Failed&&w.ejectionTime==7&&w.health==0);
    assert(length(w.position-deathPosition)==0&&w.ammo==ammunition&&w.pilotPosition().y>w.eye().y+50);
    w.hurt(100);assert(w.ejectionTime==7);in=Input{};
    w=World{};w.loadMission(argv[1],0);assert(!w.generators.empty());
    auto& generator=w.generators.front();w.position=generator.origin;auto quota=generator.remaining;
    w.stepGenerators(.01f);assert(generator.remaining==quota-1);
    auto activeSlot=generator.slots.front();assert(w.targets[activeSlot].active&&w.targets[activeSlot].generated);
    for(int i=0;i<500;++i)w.stepGenerators(.1f);
    size_t living=0;for(auto slot:generator.slots)living+=w.targets[slot].active&&w.targets[slot].health>0;
    assert(living<=generator.slots.size());
    w.targets[activeSlot].health=0;w.targetDestroyed(w.targets[activeSlot]);generator.clock=0;
    quota=generator.remaining;w.stepGenerators(.01f);assert(generator.remaining==quota-1&&w.targets[activeSlot].health>0&&!w.targets[activeSlot].processed);
    w=World{};w.loadMission(argv[1],1);for(auto& t:w.targets)if(t.spawnType==9)w.targetDestroyed(t);assert((w.missionFlags&3)==3&&w.counters[0]==0);
    w=World{};w.targets.clear();w.missionStatus=MissionStatus::Active;w.position={0,0,0};
    Target enemy;enemy.spawnType=0;enemy.center=enemy.origin={0,6,-30};enemy.half={1,1,1};enemy.yaw=enemy.ai.weaponYaw=pi;
    Target wall;wall.center=wall.origin={0,6,-15};wall.half={5,10,2};
    w.targets={enemy,wall};w.stepMission(.01f);assert(w.projectiles.empty());
    w.targets[1].health=0;w.stepMission(.01f);assert(w.projectiles.size()==1);
    std::cout<<"16 mission loads, objectives, failure priority, timed failure, supplies and idempotent destruction passed\n";
}
