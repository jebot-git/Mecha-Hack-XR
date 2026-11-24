#include "simulation.hpp"
#include <cassert>
#include <algorithm>
#include <iostream>
using namespace iron;
static Target actor(int type,Vec3 p={0,6,-30}){
    Target t;t.spawnType=type;t.center=t.origin=p;t.half={1,1,1};t.speed=6;t.yaw=t.ai.weaponYaw=pi;t.ai.spawn=p;t.ai.spawnYaw=pi;t.ai.maxHealth=t.ai.lastHealth=t.health;
    t.ai.words={10000,128,2,512};return t;
}
static World arena(){World w;w.targets.clear();w.missionStatus=MissionStatus::Active;w.position={0,0,0};return w;}
int main(int argc,char** argv){
    assert(argc==2);
    {auto w=arena();auto t=actor(0);t.active=false;t.trigger=3;w.targets={t};w.stepEnemies(.1f);assert(!w.targets[0].active);w.elapsed=3;w.stepEnemies(.1f);assert(w.targets[0].active);
        assert(!w.triggerReady(0x4001));w.missionFlags=2;assert(w.triggerReady(0x4001)&&!w.triggerReady(0x8001));}
    {auto w=arena();auto t=actor(0);t.ai.weaponYaw=0;w.targets={t};w.stepEnemies(.1f);assert(w.projectiles.empty());
        for(int n=0;n<45;++n)w.stepEnemies(.1f);assert(!w.projectiles.empty());}
    {auto w=arena();auto t=actor(0);t.ai.targetSlot=1;Target building;building.center={0,6,20};building.half={3,6,3};
        w.targets={t,building};w.targetSlots[1]=1;int victim=-1;auto p=w.enemyDestination(t,victim);assert(victim==1&&p.z==20);
        w.targets[1].health=0;p=w.enemyDestination(t,victim);assert(victim==-1&&p.z==0);}
    {auto w=arena();auto t=actor(0);t.parameters=2;t.ai.words[3]=1000;w.targets={t};
        w.stepEnemies(.1f);assert(w.targets[0].ai.state==AIState::Pursue&&w.targets[0].origin.z>-30);}
    {auto w=arena();auto t=actor(0,{0,6,-6});w.targets={t};w.stepEnemies(.1f);assert(w.targets[0].ai.state==AIState::Retreat);}
    {auto w=arena();auto t=actor(1,{0,18,-30});t.ai.weaponPitch=std::atan2(-12.f,30.f);t.ai.words={10000,7000,64,1,0};w.targets={t};
        w.stepEnemies(.1f);assert(w.targets[0].ai.state==AIState::Retreat&&!w.projectiles.empty());}
    {auto w=arena();auto t=actor(7,{0,20,-100});t.speed=76;t.ai.words={15000,64,2,1};t.ai.passes=1;w.targets={t};
        for(int n=0;n<150;++n)w.stepEnemies(.1f);assert(w.targets[0].escaped);
        assert(std::count_if(w.projectiles.begin(),w.projectiles.end(),[](auto& p){return p.kind==ProjectileKind::Bomb;})==2);}
    {auto w=arena();auto t=actor(9,{0,1,-40});t.parameters=4|16;t.ai.words[6]=64;t.ai.words[7]=2;t.ai.words[8]=512;
        t.ai.words[9]=64;t.ai.words[10]=2;t.ai.words[11]=512;t.ai.words[12]=t.ai.words[13]=10000;w.targets={t};w.stepEnemies(.1f);assert(w.projectiles.size()==2);}
    {auto w=arena();auto t=actor(10);w.targets={t};w.stepEnemies(.1f);assert(w.projectiles.size()==1);}
    {auto w=arena();auto t=actor(3,{0,6,-3});t.ai.words[0]=10000;w.targets={t};w.stepEnemies(.1f);assert(w.health==88&&w.targets[0].ai.state==AIState::Melee);}
    {auto w=arena();auto t=actor(8);t.route={{0,6,-29}};w.mission=6;w.targets={t};w.stepEnemies(.1f);assert(w.targets[0].escaped&&(w.missionFlags&(1u<<30)));}
    {auto w=arena();auto t=actor(8);t.route={{0,6,-29}};w.mission=13;w.counters[0]=1;w.targets={t};w.stepMission(.1f);assert(w.missionStatus==MissionStatus::Complete);}
    {auto w=arena();auto t=actor(0);Target wall;wall.center={0,6,-20};wall.half={4,10,1};w.targets={t,wall};assert(!w.moveEnemy(0,{0,0,20},.1f));assert(w.targets[0].origin.z==-30);}
    {auto w=arena();auto t=actor(0);t.ai.respawnRange=40;t.activationRange=20;w.targets={t};w.position={500,0,0};w.stepEnemies(.1f);assert(!w.targets[0].active&&w.targets[0].health==100);
        w.position={0,0,-30};w.stepEnemies(.1f);assert(w.targets[0].active);}
    {auto w=arena();auto t=actor(0);t.generated=true;t.ai.generator=0;t.ai.respawnRange=40;t.activationRange=20;w.targets={t};Generator g;g.remaining=3;g.slots={0};w.generators={g};
        w.position={500,0,0};w.stepEnemies(.1f);assert(w.generators[0].remaining==4&&!w.targets[0].active);w.stepEnemies(.1f);assert(w.generators[0].remaining==4);}
    {auto w=arena();auto t=actor(0);w.targets={t};w.enemyFire(0,{0,6,0},-1,ProjectileKind::Bullet,64,4);auto hp=t.health;w.step(Input{},.05f);assert(w.targets[0].health==hp);}
    {auto w=arena();w.projectiles.push_back({{0,.01f,0},{0,-5,0},3,20,-1,ProjectileKind::Bomb,-1,-1,8});w.step(Input{},.05f);assert(w.health==80&&w.projectiles.empty());}
    {auto w=arena();auto ship=actor(9);auto child=actor(0);child.active=false;child.ai.attachment=0;child.ai.attachmentOffset={0,3,10};child.trigger=2;
        w.targets={ship,child};w.stepEnemies(.1f);assert(!w.targets[1].active&&length(w.targets[1].origin-(ship.origin+rotate(angles(ship.yaw),child.ai.attachmentOffset)))<.001f);
        w.elapsed=2;w.stepEnemies(.1f);assert(w.targets[1].active&&w.targets[1].ai.attachment==-1);}
    {auto w=arena();auto t=actor(0,{0,6,-6});t.parameters=4;w.targets={t};w.stepEnemies(.1f);assert(length(w.targets[0].origin-t.origin)==0);}
    // Exercise every authored AI family, all templates, and all 16 maps for two simulated minutes.
    std::array<bool,12> covered{};
    for(int mission=0;mission<16;++mission){World w;w.loadMission(argv[1],mission);size_t size=w.targets.size();
        for(auto& t:w.targets)if(t.spawnType>=0){covered[t.spawnType]=true;if(!t.generated&&!(t.spawnFlags&7))t.active=true;}
        for(int frame=0;frame<1200;++frame){w.elapsed+=.1f;w.stepGenerators(.1f);w.stepEnemies(.1f);w.projectiles.clear();
            assert(w.targets.size()==size);
            for(auto& t:w.targets){assert(std::isfinite(length(t.origin))&&std::isfinite(t.yaw));assert(t.waypoint<=t.route.size());}}
        std::cout<<"Mission "<<mission+1<<": bounded actors "<<size<<"\n";
    }
    for(int type:{0,1,2,3,5,6,7,8,9,10,11})assert(covered[type]);
}
