#include "rpg/insertion.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace iron::rpg {
namespace {
float fade(float a, float b, float t) {
  float x=std::clamp((t-a)/(b-a),0.f,1.f);
  return x*x*(3-2*x);
}
}
InsertionSample sampleInsertion(InsertionKind kind, float seconds, float waterDepth) {
  InsertionSample s;
  if (!std::isfinite(seconds) || (kind!=InsertionKind::Drop && kind!=InsertionKind::Warp && kind!=InsertionKind::Water))
    return s;
  if(kind==InsertionKind::Water&&(!std::isfinite(waterDepth)||waterDepth<=.2f||waterDepth>=10))return s;
  s.kind=kind;
  s.time=std::max(0.f,seconds);
  s.duration=kind==InsertionKind::Drop?dropInsertionDuration:kind==InsertionKind::Warp?warpInsertionDuration:waterInsertionDuration;
  s.active=s.time<s.duration;
  s.controlsLocked=s.active;
  float t=s.time;
  if(kind==InsertionKind::Drop) {
    s.landed=t>=dropLandingTime;
    if(t<dropBrakeTime) {
      s.altitude=dropEntryAltitude-42*t-18*t*t;
      s.verticalSpeed=-42-36*t;
    } else if(t<dropLandingTime) {
      constexpr float dt=dropLandingTime-dropBrakeTime;
      constexpr float h=dropEntryAltitude-42*dropBrakeTime-18*dropBrakeTime*dropBrakeTime;
      constexpr float v=-42-36*dropBrakeTime;
      float u=(t-dropBrakeTime)/dt,u2=u*u,u3=u2*u;
      s.altitude=(2*u3-3*u2+1)*h+(u3-2*u2+u)*dt*v;
      s.verticalSpeed=((6*u2-6*u)*h+(3*u2-4*u+1)*dt*v)/dt;
    }
    s.altitude=std::max(0.f,s.altitude);
    s.brake=fade(2.05f,2.65f,t)*(1-fade(dropLandingTime+.1f,4.85f,t));
    s.entry=1-fade(2.6f,3.55f,t);
    if(s.landed&&s.active) {
      float after=t-dropLandingTime;
      s.impact=std::exp(-after*3.7f)*(1-fade(4.8f,dropInsertionDuration,t));
      s.settle=s.impact*std::cos(after*25.f);
    }
  } else if(kind==InsertionKind::Warp) {
    s.landed=t>=warpArrivalTime;
    s.warp=fade(0,.4f,t)*(1-fade(2.05f,warpInsertionDuration,t));
    s.materialization=fade(.65f,warpArrivalTime,t);
    // Quantized energy geometry assembles in a visible 16-step scale sequence.
    s.arrivalScale=std::max(1.f/16,std::ceil(s.materialization*16)/16.f);
    if(t>=warpArrivalTime&&s.active) {
      float after=t-warpArrivalTime;
      s.impact=.28f*std::exp(-after*5)*(1-fade(2.2f,warpInsertionDuration,t));
      s.settle=s.impact*std::cos(after*21);
    }
  }
  if(kind==InsertionKind::Water) {
    // Only presentation descends. The authoritative body remains on a valid
    // canal bed; even this initial eye is above that bed and below the surface.
    s.waterDepth=waterDepth;
    float submergedEye=std::max(.05f,std::min(waterDepth-.08f,waterDepth*.55f));
    s.cameraOffsetY=(submergedEye-12)*(1-fade(waterRiseBegin,waterRiseEnd,t));
    s.waterline=waterDepth-(12+s.cameraOffsetY);
    if(s.active) {
      s.submersion=1-fade(-.15f,.25f,-s.waterline);
      s.wetness=(1-s.submersion)*(1-fade(waterRiseEnd,waterInsertionDuration,t));
      s.bubbles=s.submersion*(1-fade(1.5f,waterRiseEnd,t));
    }
    s.landed=true;
  }
  return s;
}
InsertionKind insertionKindForOpener(int opener,bool enclosed,float waterDepth) {
  return opener==0?(enclosed?InsertionKind::Warp:InsertionKind::Drop)
       : opener==2?InsertionKind::Warp
       : opener==4&&!enclosed&&std::isfinite(waterDepth)&&waterDepth>.2f&&waterDepth<10?InsertionKind::Water:InsertionKind::None;
}
bool insertionLandedBetween(InsertionKind kind,float previous,float current) {
  return std::isfinite(previous)&&std::isfinite(current)&&current>=previous&&
         kind!=InsertionKind::None&&!sampleInsertion(kind,previous).landed&&sampleInsertion(kind,current).landed;
}
const char *insertionPhaseName(const InsertionSample &s) {
  if(!s.active)return "DEPLOYED";
  if(s.kind==InsertionKind::Drop)
    return s.time<2.05f?"HIGH ENTRY":s.time<dropLandingTime?"AIRBRAKING":s.time<4.12f?"HEAVY LANDING":"LANDING SETTLE";
  if(s.kind==InsertionKind::Water)return s.submersion>.5f?"SUBMERGED APPROACH":s.time<waterRiseEnd?"BREAKING SURFACE":"DRAINING CANOPY";
  return s.time<.65f?"SPATIAL LOCK":s.time<warpArrivalTime?"WARP ENVELOPE":s.time<2.2f?"MATERIALIZING":"ARRIVAL STABILIZING";
}
std::string insertionReadout(const InsertionSample &s) {
  if(!s.active)return {};
  char text[128];
  if(s.kind==InsertionKind::Drop)
    std::snprintf(text,sizeof text,"DROP / %s / ALT %d M / %.1f S",insertionPhaseName(s),int(std::lround(s.altitude)),s.duration-s.time);
  else if(s.kind==InsertionKind::Water)
    std::snprintf(text,sizeof text,"CANAL / %s / %.1f S",insertionPhaseName(s),s.duration-s.time);
  else
    std::snprintf(text,sizeof text,"WARP / %s / %d%% / %.1f S",insertionPhaseName(s),int(s.materialization*100),s.duration-s.time);
  return text;
}
} // namespace iron::rpg
