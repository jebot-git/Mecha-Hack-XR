#pragma once
#include "vector.hpp"
#include <cstdint>

namespace iron::rpg {
enum class VoidmawAssaultPhase { Idle, Pursuit, Dodge, Coil, Tackle, Recover };

// Host movement commitment. A target stepping out of the charge lane is never
// reacquired during Coil or Tackle.
struct VoidmawAssaultState {
  VoidmawAssaultPhase phase=VoidmawAssaultPhase::Idle;
  Vec3 course{};
  uint32_t target=0;
  unsigned passes=0;
  float remaining=0, side=1, lastUpdate=-1, readyAt=0;
};
}
