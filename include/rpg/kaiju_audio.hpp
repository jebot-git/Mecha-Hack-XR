#pragma once

#include "rpg/weapon_audio.hpp"

namespace iron::rpg {
struct EnemySpec;
struct EnemyAttack;

CombatCue kaijuCall(const EnemySpec &spec);
CombatCue kaijuAttackCue(const EnemyAttack &attack);
CombatCue kaijuWindupCue(const EnemyAttack &attack);
void emitKaijuSound(World &world, CombatCue cue, Vec3 origin);
}
