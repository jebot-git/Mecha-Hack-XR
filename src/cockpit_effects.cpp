#include "cockpit_effects.hpp"
#include <algorithm>
#include <cmath>
namespace iron {
using namespace avp;
float cockpitFade(float begin,float end,float time){float x=std::clamp((time-begin)/(end-begin),0.f,1.f);return x*x*(3-2*x);}
float cockpitNoise(float tick,unsigned seed){
    // Integer hashing keeps a particular time slice stable across repeated eye
    // draws, without per-draw random state or large-time sine precision loss.
    uint32_t x=uint32_t(std::max(0.f,tick))+seed*0x9e3779b9u;
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return float(x&0x00ffffffu)/16777216.f;
}
static Matrix roll(float a){auto m=identity;m[0]=m[5]=std::cos(a);m[1]=std::sin(a);m[4]=-std::sin(a);return m;}
void CockpitEffects::reset(){*this=CockpitEffects{};bootTime=0;}
float CockpitEffects::recoil(float frequency)const{
    float sum=0;
    for(auto hit:impacts)if(hit.age<2.5f)sum+=hit.strength*std::exp(-hit.age*4.2f)*(std::cos(hit.age*frequency)+.22f*std::sin(hit.age*frequency*2.1f));
    return std::clamp(sum,-1.6f,1.6f);
}
void CockpitEffects::update(const Pilot& pilot,float dt,bool active){
    updateIntegrity(pilot,dt,active,pilot.health);
}
void CockpitEffects::updateIntegrity(const Pilot& pilot,float dt,bool active,float integrity){
    insertion={};
    dt=std::isfinite(dt)&&dt>0?dt:0;
    const float physicalArmor=std::isfinite(integrity)?std::clamp(integrity,0.f,100.f):previousArmor;
    armor=pilot.injured(MechaInjury::Severe)?std::min(physicalArmor,6.f):physicalArmor;
    const bool disabled=pilot.lifeSpent||armor<=0||(std::isfinite(pilot.health)&&pilot.health<=0);
    if(disabled)bootTime=5;
    active=active&&!disabled;
    visible=active&&!pilot.missile.active;
    if(active){
        if(!started){started=true;bootTime=0;}
        clock+=dt;bootTime=std::min(5.f,bootTime+dt);
        for(auto& hit:impacts)hit.age=std::min(10.f,hit.age+dt);
        lossAge+=dt;if(lossAge>.12f)recentLoss=0;
        float loss=sampled?previousArmor-physicalArmor:0;
        if(loss>0){
            if(recentLoss==0)lossAge=0;
            recentLoss+=loss;
            if(recentLoss>=8){
                auto hit=std::max_element(impacts.begin(),impacts.end(),[](auto a,auto b){return a.age<b.age;});
                *hit={0,std::clamp(recentLoss/26.f,.3f,1.5f)};recentLoss=0;
            }
        }else if(loss<0)recentLoss=0; // armour repair is not an impact
    }else{recentLoss=0;lossAge=0;}
    sampled=true;previousArmor=physicalArmor;
    if(std::isfinite(pilot.gaitPhase)){
        float delta=std::remainder(pilot.gaitPhase-previousGait,18.f);
        if(gaitSampled&&std::abs(delta)>.0001f)reverse=delta<0;
        previousGait=pilot.gaitPhase;gaitSampled=true;
    }
    float time=float(std::fmod(clock,3600.0)),damage=1-armor*.01f;
    for(unsigned id=0;id<power.size();++id){
        float gain=1;
        if(booting())gain=cockpitFade(.5f+id*.19f,1.3f+id*.19f,bootTime);
        else{
            float n=cockpitNoise(std::floor(time*18),id);
            gain=1-damage*.27f+(damage>.4f?.11f*std::sin(time*47+id*3):0);
            if(damage>.2f&&n<(damage-.2f)*.21f)gain=.035f;
            if(armor<25&&id==3)gain=.015f;
            if(armor<15&&id==2&&std::fmod(time,3.1f)>.9f)gain=.025f;
            if(armor<8&&id==1&&std::fmod(time,2.5f)>1.1f)gain=.04f;
        }
        if(pilot.injured(MechaInjury::Severe))gain=std::min(gain,.42f);
        if(id==1&&!pilot.sensorsWorking())gain=0;
        power[id]=gain;
    }
    seat=hull=identity;
    float target=visible&&std::isfinite(pilot.walkingSpeed)?std::clamp(std::abs(pilot.walkingSpeed)/2.f,0.f,1.f):0;
    walkBlend+=(target-walkBlend)*(1-std::exp(-12*dt));
    if(!visible)return;
    // Source gait has 18 frames with alternate footfalls at 3 and 12.
    // It advances only for actual travel, including reverse movement.
    float gait=std::isfinite(pilot.gaitPhase)?(pilot.gaitPhase-(reverse?4:3))*2*pi/18:0;
    float walk=walkBlend,heel=std::pow(std::max(0.f,std::cos(gait*2)),12.f);
    hull=multiply(translation(walk*.009f*std::sin(gait),walk*.012f*std::cos(gait*2),0),
        multiply(rotation(.008f*recoil(24),.012f*recoil(21)),roll(walk*.004f*std::sin(gait))));
    seat=multiply(translation(walk*.028f*std::sin(gait),walk*(.028f*std::cos(gait*2)-.018f*heel)-.065f*recoil(27),.045f*recoil(22)),
        multiply(rotation(walk*.006f*std::sin(gait)+.018f*recoil(19),walk*.012f*std::sin(gait*2)+.047f*recoil(25)),roll(walk*.012f*std::sin(gait)+.032f*recoil(18))));
    thrusterBlend=thrusterBlend+(pilot.rpgThrust-thrusterBlend)*(1-std::exp(-22*dt));
    if(length(thrusterBlend)>.001f){
        const auto &t=thrusterBlend;
        const float shake=float(std::sin(clock*73))*std::min(1.f,length(t));
        const auto tilt=multiply(rotation(.012f*shake,-.13f*t.z+.09f*t.y),
                                 roll(-.17f*t.x+.014f*shake));
        seat=multiply(seat,multiply(translation(-.06f*t.x,-.11f*t.y+.028f*shake,-.07f*t.z),tilt));
        hull=multiply(hull,multiply(translation(-.02f*t.x,.012f*shake,0),
            multiply(rotation(0,-.055f*t.z+.04f*t.y),roll(-.07f*t.x))));
    }
    if(pilot.injured(MechaInjury::Hobbled)) {
        // One damaged leg strikes hard once per full gait cycle. The other
        // foot has a softer, shorter stroke; motion follows distance, not Hz.
        float bad=std::pow(std::max(0.f,std::cos(gait)),12.f);
        float good=std::pow(std::max(0.f,-std::cos(gait)),12.f);
        seat=multiply(seat,multiply(translation(walk*.045f*std::sin(gait),walk*(-.14f*bad+.025f*good),0),roll(walk*(.035f*std::sin(gait)+.02f*bad))));
        hull=multiply(hull,multiply(translation(0,walk*(-.035f*bad+.006f*good),0),roll(walk*.012f*std::sin(gait))));
    }
}
void CockpitEffects::update(const World& world,float dt,bool active){
    updateIntegrity(world,dt,active,world.rpgPrototype?world.rpgArmor:world.health);
    if(!world.rpgPrototype||world.lifeSpent||!world.rpgLocalPilot||slime.pilot!=world.rpgLocalPilot||
       world.elapsed+.01f<slime.lastContact||world.elapsed-slime.lastContact>2.f)slime={};
    if(world.rpgPrototype&&!world.lifeSpent&&world.rpgLocalPilot&&world.rpgCanopyAttacker>=0&&
       size_t(world.rpgCanopyAttacker)<world.targets.size()){
        const auto &ai=world.targets[size_t(world.rpgCanopyAttacker)].ai;
        if(ai.rpgGrapplePilot==world.rpgLocalPilot&&ai.voidmawPose.phase==rpg::VoidmawPose::Cling){
            if(slime.pilot!=world.rpgLocalPilot||slime.attachedAt!=ai.voidmawPose.started)
                slime={world.rpgLocalPilot,ai.voidmawPose.side,ai.voidmawPose.started,world.elapsed,-10};
            slime.lastContact=world.elapsed;
            if(ai.combatMotion.action==EnemyAction::Strike&&ai.combatMotion.started>=slime.attachedAt)
                slime.biteAt=ai.combatMotion.started;
        }
    }
    insertion=world.rpgPrototype&&!world.lifeSpent
        ?rpg::sampleInsertion(rpg::InsertionKind(world.rpgInsertion),world.rpgInsertionTime,world.waterDepthAt(world.position))
        :rpg::InsertionSample{};
    if(!visible||!insertion.active)return;
    const auto& s=insertion;
    if(s.kind==rpg::InsertionKind::Water){
        auto lift=translation(0,s.cameraOffsetY,0);
        seat=multiply(seat,lift);hull=multiply(hull,lift);
        return;
    }
    float shake=s.kind==rpg::InsertionKind::Drop?s.entry*.026f:0;
    float sx=shake*std::sin(s.time*73),sy=shake*std::sin(s.time*91);
    auto entrySeat=multiply(translation(sx,sy-.13f*s.settle,.11f*s.brake+.065f*s.settle),
        multiply(rotation(.018f*s.entry*std::sin(s.time*7),.035f*s.brake+.045f*s.settle),roll(shake*.6f*std::sin(s.time*37))));
    auto entryHull=multiply(translation(sx*.22f,sy*.18f-.018f*s.settle,0),
        multiply(rotation(0,.008f*s.brake+.012f*s.settle),roll(shake*.12f*std::sin(s.time*37))));
    seat=multiply(seat,entrySeat);hull=multiply(hull,entryHull);
}
}
