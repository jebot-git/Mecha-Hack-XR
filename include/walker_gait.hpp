#pragma once
#include "arms.hpp"

namespace iron {
struct StepSample {
  float stride; // Positive is toward the rear of the chassis.
  float lift;
};

inline StepSample sampleStep(float cycle,float contact){
  const float unitCycle=cycle/(2*pi);
  const float progress=unitCycle-std::floor(unitCycle);
  if(progress<contact)return {2*progress/contact-1,0};
  const float swing=(progress-contact)/(1-contact);
  // Match the stance velocity at both ends of swing so the toe never snaps
  // into a different speed at touchdown or at the wrap around the cycle.
  const float tangent=2*(1-contact)/contact;
  const float eased=swing*swing*(3-2*swing);
  const float arc=std::sin(pi*swing);
  return {1-2*eased+tangent*swing*(1-swing)*(1-2*swing),arc*arc};
}

struct LegAngles { float hip=0,knee=0; };

inline LegAngles reachAnkle(Vec3 upper,Vec3 lower,float stride,float lift){
  // Both hinges turn around X. Solve in the YZ plane and leave the authored X
  // offsets intact. Keep the bind pose's bend direction for reverse joints.
  const float thigh=std::hypot(upper.y,upper.z),shin=std::hypot(lower.y,lower.z);
  if(std::min(thigh,shin)<.001f)return {};
  const float femurAngle=std::atan2(upper.z,upper.y);
  const float bindBend=wrap(std::atan2(lower.z,lower.y)-femurAngle);
  const float y=upper.y+lower.y+lift,z=upper.z+lower.z+stride;
  const float reach=std::clamp(std::hypot(y,z),std::abs(thigh-shin)+.0001f,thigh+shin);
  const float cosine=std::clamp((reach*reach-thigh*thigh-shin*shin)/(2*thigh*shin),-1.f,1.f);
  const float bend=std::copysign(std::acos(cosine),std::abs(bindBend)>.002f?bindBend:-1.f);
  const float hip=std::atan2(z,y)-std::atan2(shin*std::sin(bend),thigh+shin*std::cos(bend));
  return {wrap(hip-femurAngle),wrap(bend-bindBend)};
}
}
