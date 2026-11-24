#pragma once
#include "arms.hpp"
#include <vector>
namespace iron {
enum class WalkerJoint { Rigid, Torso, LeftHip, LeftKnee, LeftFoot, RightHip, RightKnee, RightFoot, Head };
struct WalkerPart {
    int parent=-1,arm=-1;
    WalkerJoint joint=WalkerJoint::Rigid;
    Vec3 pivot{},low{},high{};
    bool empty=false;
};
struct WalkerRig {std::vector<WalkerPart> parts;float floor=0;};
struct PartPose {Quaternion orientation{};Vec3 offset{};};
enum class EnemyAction { None, RangedWindup, MeleeWindup, Fire, Strike, Guard, Parry, Stagger, Count };
// Cut directions are named from the perspective of a pilot facing the attacker.
enum class MechaTechnique { None, Rush, LeftCut, RightCut, Overhead, Skirt, MissileRack, Count };
struct EnemyCombatMotion {
    EnemyAction action=EnemyAction::None;
    float started=0,duration=0,aim=0,retreat=0;
    MechaTechnique technique=MechaTechnique::None;
    void play(EnemyAction next,float now,float span,MechaTechnique move=MechaTechnique::None){
        action=next;started=now;duration=span;technique=move;
    }
    bool active(float now) const {return duration>0&&now>=started&&now<started+duration;}
    float phase(float now) const {return duration>0?std::clamp((now-started)/duration,0.f,1.f):0.f;}
};
struct WalkerMotion {
    int chassis=-1;
    Vec3 thrust{};
    float recoil=0,landing=0;
};
struct WalkerPose {std::vector<PartPose> parts;float vertical=0;};
// Source face groups/pivots with a native travel-driven gait, not an emulation
// of the original animation tables. Phase uses the simulation's 18-step cycle.
WalkerPose poseWalker(const WalkerRig&,float phase,float speed,float torsoYaw,float headYaw=0,float headPitch=0,const WalkerMotion& motion={});
WalkerPose poseVehicle(const WalkerRig&,float blend);
Vec3 animatedPoint(const PartPose&,Vec3);
}
