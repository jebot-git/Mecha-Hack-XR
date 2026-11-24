#pragma once
#include "arms.hpp"

namespace iron::rpg {
struct Knockdown {
    float started=-1, side=0;
    bool active(float now) const {return started>=0&&now>=started&&now-started<2.65f;}
};
struct KnockdownPose {Vec3 offset{};Quaternion orientation{};float pitch=0,roll=0;};

inline KnockdownPose sampleKnockdown(Knockdown event,float now) {
    if(!event.active(now))return {};
    const float age=now-event.started;
    auto ease=[](float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);};
    const float fall=ease(age/.42f),recover=ease((age-1.2f)/1.45f);
    const float amount=fall*(1-recover);
    const float rebound=std::sin(std::max(0.f,age-.42f)*24.f)*
                         std::exp(-8.f*std::max(0.f,age-.42f))*amount;
    const float pitch=.67f*amount+.07f*rebound,roll=event.side*(1.23f*amount+.1f*rebound);
    return {{event.side*3.8f*amount,-8.3f*amount-.55f*rebound,4.5f*amount},
            angles(0,pitch,roll),pitch,roll};
}
}
