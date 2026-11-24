#include "autopilot.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
using namespace iron;
int main(int argc,char** argv){
    assert(argc>=2);World world;
    world.configure({Weapon::Empty,Weapon::Rifle,Weapon::Rail,Weapon::Shield,Weapon::Rocket,Weapon::Grenade});
    if(argc>3)world.loadProceduralPrototype(argv[1],argv[3]);else world.loadMissionPrototype(argv[1],argc>2?uint32_t(std::stoul(argv[2])):42);
    if(argc>3){
        std::ifstream file(argv[3]);nlohmann::json recipe;file>>recipe;
        assert(world.geometry().terrain.size()==recipe.at("roads").size()*6);
        size_t i=0;
        for(const auto& plot:recipe.at("plots")){
            const auto& t=world.targets.at(i++);auto b=plot.at("bounds").get<std::array<float,4>>();
            assert(t.spawnType<0&&t.mesh&&!t.mesh->empty());
            assert(t.center.x-t.half.x>=b[0]&&t.center.x+t.half.x<=b[2]);
            assert(t.center.z-t.half.z>=b[1]&&t.center.z+t.half.z<=b[3]);
        }
        World replay;replay.loadProceduralPrototype(argv[1],argv[3]);
        assert(replay.targets.size()==world.targets.size());
        for(size_t n=0;n<world.targets.size();++n){assert(length(replay.targets[n].origin-world.targets[n].origin)==0);assert(replay.targets[n].assetGame==world.targets[n].assetGame);}
    }
    auto start=world.position;Autopilot pilot;unsigned killed=0;
    for(int i=0;i<5400&&world.missionStatus==MissionStatus::Active;++i){
        Input input=pilot.update(world,1.f/60);
        assert(std::isfinite(input.headYaw)&&std::isfinite(input.headPitch));
        assert(std::abs(input.forward)<=1&&std::abs(input.steer)<=1);
        world.step(input,1.f/60);
    }
    for(const auto& t:world.targets)if(t.prototypeObjective&&t.health<=0)++killed;
    assert(world.shots[RightHand]>0&&killed>0);

    assert(length(world.position-start)>1);
    std::cout<<"AI seed="<<world.prototypeSeed<<" kills="<<killed<<" rifle_shots="<<world.shots[RightHand]<<" armor="<<world.health<<" elapsed="<<world.elapsed<<" status="<<int(world.missionStatus)<<'\n';
}
