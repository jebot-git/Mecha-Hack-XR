#include "animation.hpp"
#include "walker_gait.hpp"
#include "rpg/chassis_visual.hpp"
#include <stdexcept>
namespace iron {
Vec3 animatedPoint(const PartPose& pose,Vec3 point){return rotate(pose.orientation,point)+pose.offset;}
WalkerPose poseVehicle(const WalkerRig &rig,float blend){
    WalkerPose result;result.parts.resize(rig.parts.size());
    float t=std::isfinite(blend)?std::clamp(blend,0.f,1.f):0;t=t*t*(3-2*t);
    for(size_t i=0;i<rig.parts.size();++i){const auto &part=rig.parts[i];
        if(part.parent>=int(i)||part.parent< -1)throw std::runtime_error("Invalid vehicle hierarchy");
        Quaternion q{};
        switch(part.joint){
        case WalkerJoint::Torso:q=angles(0,-1.35f*t);break;
        case WalkerJoint::LeftHip:case WalkerJoint::RightHip:q=angles(0,-1.15f*t);break;
        case WalkerJoint::LeftKnee:case WalkerJoint::RightKnee:q=angles(0,2.15f*t);break;
        case WalkerJoint::LeftFoot:case WalkerJoint::RightFoot:q=angles(0,-1.f*t);break;
        default:if(part.arm>=0)q=angles((part.arm?1.f:-1.f)*.05f*t,.15f*t,(part.arm?1.f:-1.f)*.12f*t);break;
        }
        Vec3 hinge=part.joint==WalkerJoint::Torso?Vec3{part.pivot.x,part.low.y,part.pivot.z}:part.pivot;
        PartPose local{q,hinge-rotate(q,hinge)};
        if(part.parent<0)result.parts[i]=local;
        else{const auto &parent=result.parts[size_t(part.parent)];result.parts[i]={compose(parent.orientation,local.orientation),animatedPoint(parent,local.offset)};}
    }
    float low=1e9f;
    for(size_t i=0;i<rig.parts.size();++i){const auto &part=rig.parts[i];if(part.empty)continue;
        for(float x:{part.low.x,part.high.x})for(float y:{part.low.y,part.high.y})for(float z:{part.low.z,part.high.z})low=std::min(low,animatedPoint(result.parts[i],{x,y,z}).y);}
    result.vertical=low<1e8f?rig.floor-low:0;
    for(auto &part:result.parts)part.offset.y+=result.vertical;
    return result;
}
WalkerPose poseWalker(const WalkerRig& rig,float phase,float speed,float torsoYaw,float headYaw,float headPitch,const WalkerMotion& motion){
    WalkerPose result;result.parts.resize(rig.parts.size());
    if(!std::isfinite(phase))phase=0;
    if(!std::isfinite(speed))speed=0;
    if(!std::isfinite(torsoYaw))torsoYaw=0;
    headYaw=std::isfinite(headYaw)?std::clamp(wrap(headYaw),-headYawLimit,headYawLimit):0;
    headPitch=std::isfinite(headPitch)?std::clamp(headPitch,-1.45f,1.45f):0;
    float amplitude=std::clamp(std::abs(speed)/2.f,0.f,1.f),cycle=std::remainder(phase,18.f)*2*pi/18;
    float hip[2]{std::sin(cycle)*.24f*amplitude,std::sin(cycle+pi)*.24f*amplitude};
    float knee[2]{std::max(0.f,-std::sin(cycle))*.22f*amplitude,std::max(0.f,-std::sin(cycle+pi))*.22f*amplitude};
    float toe[2]{-hip[0]-knee[0],-hip[1]-knee[1]};
    float lean=0,bank=0,suspension=0,settle=0;
    if(size_t(motion.chassis)<rpg::chassisIds.size()){
        // Stride length and planted fraction follow the frame's weight and stance.
        constexpr float strideScale[]{.21f,.27f,.24f,.18f,.24f,.16f,.23f,.28f};
        constexpr float contact[]{.67f,.54f,.61f,.72f,.59f,.76f,.64f,.55f};
        static_assert(std::size(strideScale)==rpg::chassisIds.size()&&std::size(contact)==rpg::chassisIds.size());
        const auto thrust=std::isfinite(length(motion.thrust))?motion.thrust:Vec3{};
        const float boost=std::min(1.f,length(thrust));
        const auto slide=boost>0?unit(thrust)*boost:Vec3{};
        const float walk=amplitude*(1-boost*.88f);
        const float landing=std::clamp(motion.landing,0.f,1.f);
        const float recoil=std::clamp(motion.recoil,0.f,1.f);
        const size_t chassis=size_t(motion.chassis);
        lean=(speed<0?.09f:-.10f)*walk+slide.z*.16f+recoil*.13f-landing*.12f;
        bank=.052f*std::sin(cycle)*walk-slide.x*.17f;
        suspension=-.23f*boost-.35f*landing-.055f*walk*(1+std::cos(2*cycle));
        settle=std::sin(cycle)*walk*.10f;
        std::array<const WalkerPart*,2> upper{},lower{},foot{};
        for(const auto &part:rig.parts)switch(part.joint){
        case WalkerJoint::LeftHip:upper[0]=&part;break;
        case WalkerJoint::RightHip:upper[1]=&part;break;
        case WalkerJoint::LeftKnee:lower[0]=&part;break;
        case WalkerJoint::RightKnee:lower[1]=&part;break;
        case WalkerJoint::LeftFoot:foot[0]=&part;break;
        case WalkerJoint::RightFoot:foot[1]=&part;break;
        default:break;
        }
        for(int leg=0;leg<2;++leg){
            if(!upper[leg]||!lower[leg]||!foot[leg]||(walk+boost+landing)<.001f)continue;
            const auto step=sampleStep(cycle+leg*pi,contact[chassis]);
            const auto thigh=lower[leg]->pivot-upper[leg]->pivot;
            const auto shin=foot[leg]->pivot-lower[leg]->pivot;
            const float span=std::abs(thigh.y+shin.y);
            const float stagger=leg==0?-.10f:.10f;
            const auto solved=reachAnkle(thigh,shin,span*(step.stride*strideScale[chassis]*walk+
                slide.z*.17f+slide.x*stagger),span*(step.lift*.16f*walk+boost*.12f+landing*.13f));
            hip[leg]=solved.hip;knee[leg]=solved.knee;
            toe[leg]=-hip[leg]-knee[leg]-step.lift*step.stride*.09f*walk;
        }
    }
    for(size_t i=0;i<rig.parts.size();++i){const auto& part=rig.parts[i];
        if(part.parent>=int(i)||part.parent< -1)throw std::runtime_error("Invalid walker hierarchy");
        Quaternion rotation{};Vec3 shift{};
        switch(part.joint){
        case WalkerJoint::Torso:rotation=angles(std::clamp(wrap(torsoYaw),-pi/2,pi/2),lean,bank);shift={settle,suspension,0};break;
        case WalkerJoint::Head:rotation=angles(headYaw,headPitch-lean*.65f,-bank*.82f);break;
        case WalkerJoint::LeftHip:rotation=angles(0,hip[0]);break;
        case WalkerJoint::LeftKnee:rotation=angles(0,knee[0]);break;
        case WalkerJoint::LeftFoot:rotation=angles(0,toe[0]);break;
        case WalkerJoint::RightHip:rotation=angles(0,hip[1]);break;
        case WalkerJoint::RightKnee:rotation=angles(0,knee[1]);break;
        case WalkerJoint::RightFoot:rotation=angles(0,toe[1]);break;
        default:break;
        }
        PartPose local{rotation,part.pivot-rotate(rotation,part.pivot)+shift};
        if(part.parent<0)result.parts[i]=local;
        else {const auto& parent=result.parts[size_t(part.parent)];result.parts[i]={compose(parent.orientation,local.orientation),animatedPoint(parent,local.offset)};}
    }
    // Keep a planted sole at the bind floor and prevent the opposite foot from
    // penetrating it. Counter-rotated feet remain level while the hips stride.
    float lowest=1e9f;
    for(size_t i=0;i<rig.parts.size();++i){auto& part=rig.parts[i];if(part.empty||(part.joint!=WalkerJoint::LeftFoot&&part.joint!=WalkerJoint::RightFoot))continue;
        for(float x:{part.low.x,part.high.x})for(float y:{part.low.y,part.high.y})for(float z:{part.low.z,part.high.z})lowest=std::min(lowest,animatedPoint(result.parts[i],{x,y,z}).y);
    }
    if(lowest<1e8f)result.vertical=rig.floor-lowest;
    for(auto& pose:result.parts)pose.offset.y+=result.vertical;
    return result;
}
}
