#pragma once

#include "rpg/catalog.hpp"

namespace iron::rpg {

struct TacticalPlan;

enum class SiegeStyle { None, Sweep, Axial, Spit, Web, Spines, Elbow };

struct SiegeAttack {
  SiegeStyle style = SiegeStyle::None;
  float windup = 0, cadence = 0, recovery = 0, damageFraction = 0;
  int pulses = 0, lanes = 0;
  float projectileSpeed = 0;
  explicit operator bool() const { return style != SiegeStyle::None; }
  bool beam() const { return style == SiegeStyle::Sweep || style == SiegeStyle::Axial; }
};

SiegeAttack siegeAttack(const EnemySpec &, const EnemyAttack &);
Vec3 siegeEmitter(const Target &, const SiegeAttack &, float now, Vec3 fallback);
float siegeLaneOffset(const SiegeAttack &, int pulse, int lane, bool reverse);
void steerSiegeVoidmaw(const EnemySpec &, Target &, Vec3 pilot, float now, size_t index,
                       TacticalPlan &, Vec3 &destination);

} // namespace iron::rpg
