#pragma once
#include "simulation.hpp"
#include <sstream>
namespace iron {
struct SightPlacement {Vec3 position;float scale;};
inline SightPlacement sightPlacement(Vec3 target){
    float depth=std::clamp(-target.z,8.f,120.f);
    return {{target.x*depth/-target.z,target.y*depth/-target.z,-depth},depth/2.f};
}
inline Vec3 radarColor(const Target& target,int game){
    if(target.team!=Team::Neutral)return teamColour(target.team);
    if(game&&target.spawnType==20)return {.2f,.8f,1.f};
    if(game&&target.spawnType==21)return {1.f,.8f,.2f};
    return target.spawnType>=0?Vec3{1.f,.2f,.1f}:Vec3{.25f,.5f,.2f};
}
inline std::vector<std::string> missionLines(const std::string& text,size_t columns=64){
    std::vector<std::string> lines;std::istringstream words(text);std::string word,line;
    while(words>>word){
        if(!line.empty()&&line.size()+word.size()+1>columns){lines.push_back(line);line.clear();}
        if(!line.empty())line+=' ';line+=word;
    }
    if(!line.empty())lines.push_back(line);return lines;
}
inline std::vector<Pilot> radarPilots(const World& world){
    auto pilots=world.mapPilots;
    auto local=std::find_if(pilots.begin(),pilots.end(),[&](const auto& p){return p.playerSlot==world.playerSlot;});
    if(local==pilots.end())pilots.push_back(world);else *local=world;
    return pilots;
}
}
