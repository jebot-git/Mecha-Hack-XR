#pragma once
#include "simulation.hpp"
namespace iron::rpg {
enum class WeakPoint {Optics,Reactor,LeftArm,RightArm,Drive,Count};
inline constexpr Vec3 weakPointOffsets[]={{0,.57f,-1},{0,.19f,1},{-1,.25f,0},{1,.25f,0},{0,-.65f,-1}};
inline Vec3 weakPointPosition(const Target &target,WeakPoint part){
    const auto local=weakPointOffsets[unsigned(part)];
    const auto half=target.ai.boundsReady?target.ai.localHalf:target.half;
    return target.center+rotate(compose(target.ai.gravity.frame,angles(target.yaw)),{local.x*half.x,local.y*half.y,local.z*half.z});
}
inline WeakPoint hitWeakPoint(const Target &target,Vec3 impact){
    const auto local=rotate(gravityInverse(compose(target.ai.gravity.frame,angles(target.yaw))),impact-target.center);
    const auto half=target.ai.boundsReady?target.ai.localHalf:target.half;
    const Vec3 p{local.x/std::max(.1f,half.x),local.y/std::max(.1f,half.y),local.z/std::max(.1f,half.z)};
    float nearest=.50f;
    WeakPoint result=WeakPoint::Count;
    for(unsigned n=0;n<unsigned(WeakPoint::Count);++n){
        const float range=length(p-weakPointOffsets[n]);
        if(range<nearest){nearest=range;result=WeakPoint(n);}
    }
    return result;
}
}
