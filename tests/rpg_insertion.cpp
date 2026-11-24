#include "rpg/insertion.hpp"
#include "cockpit_effects.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace iron;
using namespace iron::rpg;
using namespace avp;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static float delta(const Matrix& a,const Matrix& b){float d=0;for(size_t i=0;i<a.size();++i)d=std::max(d,std::abs(a[i]-b[i]));return d;}
static CockpitEffects at(unsigned fps,float time,InsertionKind kind){
    CockpitEffects fx;World w;w.rpgPrototype=true;w.rpgInsertion=int(kind);
    if(kind==InsertionKind::Water)w.waterRegions.push_back({{-20,-20,20,20},2.75f,2.75f,0});
    fx.update(w,0,true);
    unsigned frames=unsigned(std::lround(time*fps));
    for(unsigned i=1;i<=frames;++i){w.rpgInsertionTime=float(i)/fps;fx.update(w,1.f/fps,true);}
    return fx;
}
int main(){try{
    check(insertionKindForOpener(0)==InsertionKind::Drop&&insertionKindForOpener(0,true)==InsertionKind::Warp,"enclosed drop lacks warp fallback");
    check(insertionKindForOpener(2)==InsertionKind::Warp&&insertionKindForOpener(3)==InsertionKind::None,"opener mapping changed tunnel floor semantics");
    auto begin=sampleInsertion(InsertionKind::Drop,0);
    check(begin.altitude==300&&begin.verticalSpeed<0&&begin.controlsLocked&&!begin.landed,"drop does not begin high and locked");
    float previous=begin.altitude;
    for(int i=0;i<=5400;++i){
        auto s=sampleInsertion(InsertionKind::Drop,i*.001f);
        check(s.altitude>=0&&s.altitude<=300&&s.altitude<=previous+.0001f,"drop trajectory rises or penetrates ground");
        check(std::isfinite(s.verticalSpeed)&&s.verticalSpeed<=.001f,"drop has invalid/upward velocity");
        check(s.brake>=0&&s.brake<=1&&s.impact>=0&&s.impact<=1,"presentation envelope escapes bounds");
        previous=s.altitude;
    }
    constexpr float e=.0001f;
    auto before=sampleInsertion(InsertionKind::Drop,dropBrakeTime-e),after=sampleInsertion(InsertionKind::Drop,dropBrakeTime+e);
    check(std::abs(before.altitude-after.altitude)<.04f&&std::abs(before.verticalSpeed-after.verticalSpeed)<.1f,"airbraking trajectory has a position/velocity seam");
    auto land=sampleInsertion(InsertionKind::Drop,dropLandingTime),done=sampleInsertion(InsertionKind::Drop,dropInsertionDuration);
    check(land.altitude==0&&land.verticalSpeed==0&&land.landed&&land.controlsLocked&&land.impact==1,"touchdown timing/settle lock incorrect");
    check(!done.active&&!done.controlsLocked&&done.landed&&done.brake==0&&done.impact==0,"drop fails to release controls/visuals");
    for(unsigned fps:{30u,60u,72u,90u,120u,144u}){
        int landings=0;
        for(unsigned i=1;i<=fps*6;++i)landings+=insertionLandedBetween(InsertionKind::Drop,float(i-1)/fps,float(i)/fps);
        check(landings==1,"frame rate changes authoritative touchdown crossing count");
    }
    check(!insertionLandedBetween(InsertionKind::Drop,4,3),"clock rewind causes a landing");
    auto warp=sampleInsertion(InsertionKind::Warp,0),arrived=sampleInsertion(InsertionKind::Warp,warpArrivalTime);
    check(warp.active&&!warp.landed&&warp.arrivalScale<.1f&&warp.altitude==0,"warp initial geometry/state invalid");
    check(arrived.landed&&arrived.materialization==1&&arrived.arrivalScale==1&&arrived.controlsLocked,"warp arrival fails materialization/settle");
    check(!sampleInsertion(InsertionKind::Warp,warpInsertionDuration).controlsLocked,"warp locks controls forever");
    check(sampleInsertion(InsertionKind::None,0).landed&&!sampleInsertion(InsertionKind::None,0).active,"ordinary opener creates an insertion");
    check(!sampleInsertion(InsertionKind::Drop,std::numeric_limits<float>::quiet_NaN()).active,"invalid replicated clock starts a drop");
    check(!sampleInsertion(InsertionKind(99),1).active,"unknown insertion kind creates visuals");
    check(sampleInsertion(InsertionKind::Drop,-1).altitude==300,"negative insertion clock is not clamped");
    check(insertionKindForOpener(4,false,2.75f)==InsertionKind::Water,"real canal depth does not select water insertion");
    check(insertionKindForOpener(4,false,0)==InsertionKind::None&&insertionKindForOpener(4,true,2.75f)==InsertionKind::None,"dry/indoor entry starts a water overlay");
    check(!sampleInsertion(InsertionKind::Water,0,0).active&&!sampleInsertion(InsertionKind::Water,0,10).active,"invalid water depth starts underwater view");
    for(float depth:{.21f,2.75f,9.9f}){
        auto start=sampleInsertion(InsertionKind::Water,0,depth);
        check(start.altitude==0&&start.landed&&start.submersion>.5f&&start.wetness<.1f,"canal body/initial submersion contract invalid");
        check(12+start.cameraOffsetY>=.05f&&12+start.cameraOffsetY<depth,"water initial camera crosses bed or starts above surface");
        float offset=start.cameraOffsetY;
        bool wetAfterEmergence=false;
        for(int i=0;i<=580;++i){
            auto s=sampleInsertion(InsertionKind::Water,i*.01f,depth);
            check(s.altitude==0&&s.cameraOffsetY>=offset-.00001f&&12+s.cameraOffsetY>=.049f,"water rise moves physical body or camera through the bed");
            check(s.submersion>=0&&s.submersion<=1&&s.wetness>=0&&s.wetness<=1&&s.bubbles>=0&&s.bubbles<=1,"water shader envelope outside bounds");
            if(s.submersion==0&&s.wetness>.25f)wetAfterEmergence=true;
            offset=s.cameraOffsetY;
        }
        check(wetAfterEmergence,"canopy never drains after waterline crossing");
        auto done=sampleInsertion(InsertionKind::Water,waterInsertionDuration,depth);
        check(!done.active&&!done.controlsLocked&&done.cameraOffsetY==0&&done.submersion==0&&done.wetness==0&&done.bubbles==0,"water overlay/offset persists after sequence");
    }
    auto waterAt60=at(60,2.5f,InsertionKind::Water);
    for(unsigned fps:{30u,72u,90u,120u,144u}){
        auto sample=at(fps,2.5f,InsertionKind::Water);
        check(delta(sample.seat,waterAt60.seat)<.00001f&&delta(sample.hull,waterAt60.hull)<.00001f,"water rise depends on headset refresh rate");
    }
    check(std::abs(waterAt60.seat[13]-waterAt60.hull[13])<.00001f,"water camera/hull offsets misalign native glass/instruments");
    auto reference=at(60,2.5f,InsertionKind::Drop);
    for(unsigned fps:{30u,72u,90u,120u,144u}){
        auto sample=at(fps,2.5f,InsertionKind::Drop);
        check(delta(sample.seat,reference.seat)<.00001f&&delta(sample.hull,reference.hull)<.00001f,"cockpit insertion depends on headset refresh rate");
    }
    // Render draws are read-only and receive one shared rigid transform. Neither
    // eye advances phase or changes the native 64mm binocular separation.
    const auto state=reference.insertion;const auto seat=reference.seat;
    auto body=multiply(translation(31,100,-29),rotation(.8f));
    auto left=multiply(body,translation(-.032f,0,0)),right=multiply(body,translation(.032f,0,0));
    auto motion=multiply(body,multiply(reference.seat,inverseRigid(body)));
    check(delta(multiply(inverseRigid(left),right),multiply(inverseRigid(multiply(motion,left)),multiply(motion,right)))<.00002f,"insertion motion changes stereo separation");
    check(reference.insertion.time==state.time&&reference.seat==seat,"eye reads mutated presentation state");
    auto waterMotion=multiply(body,multiply(waterAt60.seat,inverseRigid(body)));
    check(delta(multiply(inverseRigid(left),right),multiply(inverseRigid(multiply(waterMotion,left)),multiply(waterMotion,right)))<.00002f,"water emergence changes stereo separation");
    World w;w.rpgPrototype=true;w.rpgInsertion=1;w.rpgInsertionTime=3.8f;
    CockpitEffects fx;fx.update(w,.016f,true);
    check(delta(fx.seat,identity)>.08f,"heavy landing has no cockpit impulse");
    fx.update(w,.016f,false);check(fx.seat==identity&&fx.hull==identity,"paused insertion adds camera motion");
    w.lifeSpent=true;fx.update(w,.016f,true);check(!fx.insertion.active&&fx.seat==identity,"ejection inherits insertion pose");
    check(!insertionReadout(begin).empty()&&insertionReadout(done).empty(),"insertion HUD lifecycle invalid");
    std::cout<<"High entry/warp, valid-bed canal submersion/emergence/draining, no persistent overlay, fallback, 30-144Hz timing and stereo rigidity passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
