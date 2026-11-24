#include "simulation.hpp"
#include <algorithm>
namespace iron {
void World::stepEjection(float dt){
    float before=ejectionTime;ejectionTime=std::min(7.f,ejectionTime+dt);
    for(float cue:{1.f,1.5f,3.f})if(before<cue&&ejectionTime>=cue)sounds.push_back({SoundEvent::Explosion,0,eye()});
    if(ejectionTime>=7)missionStatus=MissionStatus::Failed;
}
Vec3 World::pilotPosition() const {
    if(!lifeSpent)return eye();
    float t=ejectionTime,launch=std::clamp(t-1.5f,0.f,3.5f);
    float rise=36*launch-4*launch*launch,back=12*launch;
    float shake=t<1.5f?.035f*std::sin(t*91):0;
    return eye()+rotate(angles(facing()),{shake,rise,back});
}
Quaternion World::pilotOrientation() const {
    float t=ejectionTime,u=std::clamp((t-2)/2,0.f,1.f);u=u*u*(3-2*u);
    float roll=lifeSpent?.12f*std::sin(t*8)*std::exp(-std::max(0.f,t-2)):0;
    return angles(facing(),lifeSpent?-1.04f*u:0,roll);
}
}
