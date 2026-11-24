#pragma once
#include "cockpit_glass.hpp"

namespace iron::rpg {
enum class VoidmawPose {
  Default, Pursuit, Dodge, Coil, Tackle, Cling, Recover,
  Lumber, Brace, Discharge, SiegeRecover
};

// Signed placement mirrors the bite on the left or right front pane.
inline Vec3 voidmawCanopyContact(float side){return cockpitFrontGlass(side<0?-.47f:.47f,.22f);}
inline constexpr float voidmawHangYaw=.56f,voidmawHangPitch=.57f,voidmawHangRoll=.42f;
inline constexpr float voidmawBiteWindup=.34f,voidmawBiteFollow=.46f;

// Time is authoritative world simulation time.
struct VoidmawPoseCue {
  VoidmawPose phase=VoidmawPose::Default;
  float started=0;
  float duration=0; // Zero holds the pose until the AI changes phase.
  float side=1;
};
}
