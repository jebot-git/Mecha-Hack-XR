#pragma once
#include "rpg/enemy_animation.hpp"
#include <filesystem>
#include <map>
#include <string_view>
#include <cstdint>
namespace iron { struct Pilot; }
namespace iron::rpg {
struct MotionClip { size_t first; unsigned frames; float scale; };
struct MotionActor {
    std::vector<std::string> joints;
    std::map<std::string,MotionClip,std::less<>> clips;
    std::string kind,family;
    std::vector<EnemyPart> elbowRig;
};
struct MotionCue { const MotionClip* clip=nullptr; float phase=0; };
void blendMotionTransition(WalkerPose&,const WalkerPose& from,float fraction);
class MotionPlayback {
public:
    void load(const std::filesystem::path& directory);
    const MotionActor* find(std::string_view actor)const;
    const EnemyRig* visualRig(const std::string& actor,const EnemyRig* source);
    MotionCue enemyCue(const MotionActor&,const Target&,float now)const;
    MotionCue pilotCue(const MotionActor&,const Pilot&,float now)const;
    static MotionCue at(const MotionActor&,std::initializer_list<std::string_view>,float phase);
    void apply(const MotionActor&,MotionCue,WalkerPose&,const EnemyRig* enemy=nullptr,
               const WalkerRig* player=nullptr,bool preserveAim=false,bool canopy=false,float yaw=0,float pitch=0,bool discharge=false)const;
private:
    std::map<std::string,MotionActor,std::less<>> actors;
    std::map<std::string,EnemyRig> visualRigs;
    std::vector<int16_t> samples;
};
}
