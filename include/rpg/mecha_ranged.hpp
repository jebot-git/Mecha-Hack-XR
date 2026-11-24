#pragma once
#include "rpg/catalog.hpp"

namespace iron::rpg {
struct TacticalPlan;
enum class MechaRangedRole { None, Skirmisher, MissileCarrier };
MechaRangedRole mechaRangedRole(const EnemySpec &);
void steerRangedMecha(MechaRangedRole,const EnemySpec &,const Target &,Vec3 victim,float now,
                      size_t index,TacticalPlan &,Vec3 &destination);
inline constexpr float mechaWaveRadius=.85f;
inline constexpr float mechaWaveSpacing=4.8f;
inline constexpr int mechaWaveLayers=3, mechaWaveColumns=7, mechaRackSalvos=4, mechaRackTubes=3;
inline constexpr float mechaWaveCadence=.74f,mechaRackCadence=.26f;
}
