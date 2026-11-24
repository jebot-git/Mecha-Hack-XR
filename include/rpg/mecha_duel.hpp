#pragma once
#include "animation.hpp"
#include <cstdint>

namespace iron::rpg {
struct EnemyAttack;
struct EnemySpec;
enum class DuelPhase { Idle, Rush, Windup, FollowThrough, Guard, Recover };
struct MechaDuel {
  DuelPhase phase=DuelPhase::Idle;
  MechaTechnique cut=MechaTechnique::None;
  uint32_t pilot=0;
  int npc=-1,beat=0;
  unsigned sequence=0;
  float until=0,lastStep=-1;
  Vec3 origin{},ray{},guardAxis{},guardPoint{};
};
const EnemyAttack *duelWeapon(const EnemySpec &);
}
