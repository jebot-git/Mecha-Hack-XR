#pragma once
#include "matrix.hpp"
#include "simulation.hpp"
#include "rpg/insertion.hpp"
#include <array>
namespace iron {
float cockpitFade(float begin,float end,float time);
float cockpitNoise(float tick,unsigned seed);
inline float reactorWarning(float heat){return cockpitFade(.45f,1.f,heat);}
inline float reactorSteam(float heat,bool venting){return venting?1.f:cockpitFade(.58f,.98f,heat);}

// Local visual state. Advance once per client frame, then read unchanged for
// every eye. No simulation, network, tracking, weapon or saved state lives here.
class CockpitEffects {
    struct Impact { float age=10,strength=0; };
    std::array<Impact,8> impacts{};
    float previousArmor=100,recentLoss=0,lossAge=0,walkBlend=0,previousGait=0;
    Vec3 thrusterBlend{};
    bool sampled=false,started=false,gaitSampled=false,reverse=false;
    float recoil(float frequency)const;
    void updateIntegrity(const Pilot&,float dt,bool active,float integrity);
public:
    double clock=0;
    float bootTime=5,armor=100;
    bool visible=false;
    avp::Matrix seat=avp::identity,hull=avp::identity;
    std::array<float,5> power{1,1,1,1,1}; // overview, radar, ammo, heading, torso
    void reset();
    void update(const Pilot&,float dt,bool active);
    void update(const World&,float dt,bool active);
    // Simulation times keep glass deposits stationary through pauses and stereo eye draws.
    struct CanopySlime {
        uint32_t pilot=0;
        float side=0,attachedAt=0,lastContact=0,biteAt=-10;
    } slime;
    rpg::InsertionSample insertion;
    bool booting()const{return started&&bootTime<5;}
    int fractureStage()const{return armor>=25?0:armor>=15?1:armor>=8?2:3;}
};
}
