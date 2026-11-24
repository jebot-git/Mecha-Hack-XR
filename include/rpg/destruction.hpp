#pragma once
#include "simulation.hpp"
#include "rpg/enemy_animation.hpp"
#include "destruction_visuals.hpp"
namespace iron::rpg {
DestructionMaterial destructionMaterial(std::string_view);
bool volatileScenery(std::string_view);
std::shared_ptr<WorldMesh> destructionWreck(const WorldMesh &,std::string_view,size_t budget);
WalkerPose poseEnemyDeath(const EnemyRig &,float phase,float aimYaw,float aimPitch,float age,unsigned seed);
struct CollapsePose {Quaternion rotation{};Vec3 scale{1,1,1};bool intact=false;};
CollapsePose collapsePose(float age,unsigned seed);
}
