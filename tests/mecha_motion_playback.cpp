#include "rpg/motion_playback.hpp"
#include "simulation.hpp"
#include <cassert>
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>

using namespace iron;
using namespace iron::rpg;
int main(int argc,char** argv){
    assert(argc==2);
    std::filesystem::path root=argv[1];
    MotionPlayback library;library.load(root/"motions");
    std::ifstream input(root/"motions/library.json");nlohmann::json data;input>>data;
    size_t actors=0,clips=0;
    for(const auto& [id,row]:data.at("actors").items()){
        const auto* a=library.find(id);assert(a);++actors;
        if(a->family!="mecha"||(!id.starts_with("chassis_")&&id!="iron-soldier")){
            nlohmann::json mesh;std::ifstream f(root/"meshes"/(id+".json"));f>>mesh;
            size_t vertices=mesh.at("faces").size()*3;
            const auto* source=enemyAnimationRig(root/"models",id,vertices);
            assert(source);
            assert(library.visualRig(id,source)->parts.size()==a->joints.size());
        }
        for(const auto& [name,c]:a->clips){
            ++clips;
            WalkerPose pose;pose.parts.resize(a->joints.size());
            for(float p:{0.f,.27f,.5f,.91f,1.f}){
                library.apply(*a,{&c,p},pose);
                for(const auto& s:pose.parts)assert(valid(s.orientation)&&std::isfinite(length(s.offset)));
            }
        }
    }
    const auto& ace=*library.find("mecha_ace");
    Target target;target.health=1;target.ai.combatMotion.play(EnemyAction::Strike,4,1,MechaTechnique::LeftCut);
    assert(library.enemyCue(ace,target,4.2f).clip==&ace.clips.at("left-cut"));
    const auto& jumper=*library.find("jump_pod");target.ai.combatMotion={};target.ai.velocity={19,0,0};
    assert(library.enemyCue(jumper,target,8).clip==&jumper.clips.at("leap-land"));
    const auto& grub=*library.find("kaiju_flea");target.ai.voidmawPose={VoidmawPose::Tackle,3,.8f,1};
    assert(library.enemyCue(grub,target,3.25f).clip==&grub.clips.at("tackle"));
    assert(actors==120&&clips==2164);
    std::cout<<actors<<" runtime rig bindings, "<<clips<<" clips sampled\n";
}
