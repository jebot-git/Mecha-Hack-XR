#pragma once
#include "vector.hpp"
#include <array>
#include <cmath>
#include <algorithm>
namespace iron {
// Core math uses OpenXR's quaternion convention (x,y,z,w), without an XR dependency.
struct Quaternion { float x=0,y=0,z=0,w=1; };
inline float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec3 unit(Vec3 v,Vec3 fallback={0,0,-1}){float n=length(v);return n>.00001f&&std::isfinite(n)?v*(1/n):fallback;}
inline bool valid(Quaternion q){float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;return std::isfinite(n)&&n>.5f&&n<1.5f;}
inline Quaternion normalized(Quaternion q){float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return {q.x/n,q.y/n,q.z/n,q.w/n};}
inline Quaternion compose(Quaternion a,Quaternion b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
inline Vec3 rotate(Quaternion q,Vec3 v){Vec3 u{q.x,q.y,q.z};return v+cross(u,cross(u,v)+v*q.w)*2;}
inline Quaternion angles(float yaw,float pitch=0,float roll=0){return compose(compose({0,std::sin(yaw/2),0,std::cos(yaw/2)},{std::sin(pitch/2),0,0,std::cos(pitch/2)}),{0,0,std::sin(roll/2),std::cos(roll/2)});}
inline constexpr float headYawLimit=pi/4,handYawLimit=pi/2;
inline float attackYaw(float relative,float limit=headYawLimit){return std::clamp(wrap(relative),-limit,limit);}
inline Vec3 attackDirection(Vec3 ray,float torsoYaw=0,float limit=headYawLimit){
    float planar=std::hypot(ray.x,ray.z);if(planar<.000001f)return ray;
    float heading=torsoYaw+attackYaw(std::atan2(-ray.x,-ray.z)-torsoYaw,limit);
    return {-std::sin(heading)*planar,ray.y,-std::cos(heading)*planar};
}
inline Quaternion attackOrientation(Quaternion orientation,float limit=handYawLimit){
    auto forward=rotate(orientation,{0,0,-1});if(std::hypot(forward.x,forward.z)<.000001f)return orientation;
    float heading=std::atan2(-forward.x,-forward.z);
    // A world-Y correction preserves the controller's pitch and roll.
    return compose(angles(wrap(attackYaw(heading,limit)-heading)),orientation);
}
struct HandPose {
    Vec3 position{}; // metres relative to calibrated eye origin, in torso axes
    Quaternion grip{},aim{};
    bool valid=true;
};
struct ArmRest { Vec3 shoulder,elbow,wrist; };
struct ArmRig {
    float trackingScale=6,upperLength=2.1f,foreLength=1.8f;
    std::array<Vec3,5> muzzle{{{-.34f,.08f,-2.28f},{-0.0122386f,0.9280000f,-4.9549517f},{-.92f,.03f,-5.24f},{.1990352f,1.165f,-4.6682154f},{.1062787f,.92f,-4.8466308f}}}; // empty, rifle, cutter, shotgun, heavy MG
};
struct Arm {
    Vec3 shoulder{},elbow{},wrist{},muzzle{},forward{0,0,-1};
    Quaternion grip{},aim{};
    bool tracked=false;
};
void braceArm(Arm &,int hand,const Arm &weapon,Vec3 grip,Quaternion cockpit,const ArmRig &,const ArmRest* rest=nullptr);
Arm solveArm(const HandPose&,int hand,Vec3 eye,float torsoYaw,const ArmRig&,int heldModel,Quaternion cockpit={},const ArmRest* rest=nullptr);
}
