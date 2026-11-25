#include "simulation.hpp"
#include <algorithm>
namespace iron {
static void positionArm(Arm &arm,Vec3 target,Vec3 pole,Vec3 fallback,const ArmRig &rig,bool fixedWrist,const ArmRest* rest) {
    const Vec3 delta=target-arm.shoulder;
    const Vec3 axis=unit(delta);
    const float upper=rest?length(rest->elbow-rest->shoulder):rig.upperLength;
    const float fore=rest?length(rest->wrist-rest->elbow):rig.foreLength;
    const float distance=std::clamp(length(delta),std::abs(upper-fore)+.001f,upper+fore-.001f);
    arm.wrist=fixedWrist?target:arm.shoulder+axis*distance;
    const float along=(upper*upper-fore*fore+distance*distance)/(2*distance);
    const float height=std::sqrt(std::max(0.f,upper*upper-along*along));
    const Vec3 bend=unit(pole-axis*dot(pole,axis),fallback);
    arm.elbow=arm.shoulder+axis*along+bend*height;
}
void braceArm(Arm &arm,int hand,const Arm &weapon,Vec3 grip,Quaternion cockpit,const ArmRig &rig,const ArmRest* rest) {
    const float side=hand?1.f:-1.f;
    positionArm(arm,grip,rotate(cockpit,{side*.8f,-1,0}),rotate(cockpit,{0,0,1}),rig,true,rest);
    arm.grip=arm.aim=weapon.aim;
    arm.muzzle=grip;
    arm.forward=weapon.forward;
}
Arm solveArm(const HandPose& pose,int hand,Vec3 eye,float yaw,const ArmRig& rig,int heldModel,Quaternion cockpit,const ArmRest* rest) {
    Arm out;float side=hand?1.f:-1.f;auto body=compose(angles(yaw),cockpit);
    out.shoulder=eye+rotate(body,rest?rest->shoulder:Vec3{side*2.5f,-1.2f,-.1f});
    for(float v:{rig.trackingScale,rig.upperLength,rig.foreLength})if(!std::isfinite(v)||v<.1f||v>10)return out;
    if(!pose.valid||!valid(pose.grip)||!valid(pose.aim)||!std::isfinite(length(pose.position))||length(pose.position)>3)return out;
    out.grip=compose(body,attackOrientation(normalized(pose.grip)));out.aim=compose(body,attackOrientation(normalized(pose.aim)));
    // Constrain the hand target too: moving a controller behind the player
    // must not move a firing/melee origin behind the torso.
    Vec3 local=attackDirection(pose.position,0,handYawLimit)*rig.trackingScale;
    Vec3 pole{side*.8f,-1,0};
    if(rest){
        // A rested controller places the mecha wrist at its authored socket.
        // Controller displacement and recoil then move the authoritative IK target.
        const auto neutralController=cockpitNeutralController(hand);
        auto travel=local-neutralController*rig.trackingScale;
        // A real controller can retract all the way to the pilot's chest.
        // Compress that last portion of travel outside the canopy, while
        // preserving the full forward reach used for parries and bracing.
        if(travel.z>.60f){
            const float extra=travel.z-.60f;
            travel.z=.60f+.60f*extra/(.60f+extra);
        }
        local=rest->wrist+travel;
        local.z=std::min(local.z,-2.8f);
        const float nearCanopy=std::clamp(std::max((local.z+4.1f)/1.3f,(pose.position.z+.30f)/.30f),0.f,1.f);
        if(nearCanopy>0)local.x=side*std::max(side*local.x,1.75f*nearCanopy);
        local=attackDirection(local,0,handYawLimit);
        const Vec3 restAxis=unit(rest->wrist-rest->shoulder);
        const Vec3 upper=rest->elbow-rest->shoulder;
        pole=unit(upper-restAxis*dot(upper,restAxis),pole);
    }
    Vec3 target=eye+rotate(body,local);
    positionArm(out,target,rotate(body,pole),rotate(body,{0,0,1}),rig,false,rest);
    out.forward=rotate(out.aim,{0,0,-1});
    Vec3 muzzle=rig.muzzle[std::clamp(heldModel,0,int(rig.muzzle.size())-1)];muzzle.x*=side;
    out.muzzle=out.wrist+rotate(out.aim,muzzle);out.tracked=true;
    return out;
}
}
