#include "frontend.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace iron;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    check(argc==2,"retail path");
    for(bool cooperative:{false,true}){
        World world;world.loadMission(argv[1],2);world.cooperative=cooperative;
        // Keep every scenery collision, but isolate this stationary weapon check
        // from enemy fire and reinforcement timing.
        std::erase_if(world.targets,[](const auto& target){return target.spawnType>=0;});world.generators.clear();world.targetSlots.fill(-1);
        if(cooperative)world.teammates={&world};
        world.configure({Weapon::Empty,Weapon::Rifle,Weapon::Empty,Weapon::Empty,Weapon::Grenade,Weapon::Empty});
        unsigned supports=0,grenades=0;
        for(auto& target:world.targets){
            if(target.spawnType>=0||target.tag!=32)continue;
            ++supports;check(target.active&&target.health==200,"both authored bridge targets are live");
            world.position={target.center.x,0,target.center.z+target.half.z+35};world.chassisYaw=world.torsoYaw=0;
            auto settle=[&](int frames){for(int i=0;i<frames;++i)world.step(Input{},.01f);};
            settle(2);Input rifle;rifle.fire[RightHand]=true;world.step(rifle,.01f);settle(180);
            check(target.health==200,"rifle hits cannot destroy grenade-only bridge");
            world.explode(target.center,1,10000);
            check(target.health==200,"splash cannot bypass direct-grenade requirement");
            for(int hit=1;hit<=4;++hit){
                Input grenade;grenade.fire[LeftHip]=true;auto shots=world.shots[LeftHip];world.step(grenade,.01f);
                check(world.shots[LeftHip]==shots+1,"real grenade launcher fires after cooldown");++grenades;settle(300);
                if(target.health!=200-hit*50)std::cerr<<"cooperative="<<cooperative<<" bridge="<<supports<<" hit="<<hit<<" health="<<target.health<<" pilot="<<world.health<<" position="<<world.position.x<<','<<world.position.y<<','<<world.position.z<<'\n';
                check(target.health==200-hit*50,"ballistic grenade hits the actual bridge collision bounds");
            }
            check(target.processed&&world.counters[0]==2-int(supports),"bridge destruction updates objective exactly once");
            check(target.wreck&&!target.wreck->empty(),"destroyed bridge has replacement wreck geometry");
            auto position=world.wreckPosition(target);float bottom=1e9f;
            for(auto& vertex:*target.wreck)bottom=std::min(bottom,position.y+rotate(angles(target.yaw,target.ai.pitch,target.ai.bank),vertex.position).y);
            check(std::abs(bottom)<.001f,"bridge wreck rests on current terrain");
            if(supports==1)check(world.missionStatus==MissionStatus::Active,"first bridge alone cannot complete mission");
        }
        check(supports==2&&grenades==8&&world.ammo[LeftHip]==0,"starting rack has enough grenades for both bridges");
        for(int i=0;i<400&&world.missionStatus==MissionStatus::Active;++i)world.step(Input{},.01f);
        check(world.missionStatus==MissionStatus::Complete,"London Bridge completes after both bridges and destruction effects");
    }
    std::cout<<"IS1 mission 3: real rifle immunity, direct ballistic grenades, both bridge wrecks and completion pass in solo and co-op\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
