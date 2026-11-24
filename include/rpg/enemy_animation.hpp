#pragma once
#include "animation.hpp"
#include "rpg/voidmaw_pose.hpp"
#include <filesystem>
#include <optional>
#include <string>
namespace iron { struct Target; }
namespace iron::rpg {
enum class EnemyJoint {Rigid,Torso,Head,Hip,Knee,Foot,Arm,Tail,Wing,Rotor,Turret,Barrel,Suspension,Jaw,Scanner};
struct EnemyPart {
  std::string name;
  int parent=-1;
  EnemyJoint joint=EnemyJoint::Rigid;
  Vec3 pivot{},low{},high{};
  float side=0,phase=0;
  bool ground=false;
  std::vector<std::pair<size_t,size_t>> ranges;
  char rotationAxis=0; // Optional authored hinge axis; zero keeps the role default.
};
struct EnemyRig {
  std::string archetype;
  float floor=0;
  size_t vertices=0;
  std::vector<EnemyPart> parts;
  std::vector<std::pair<size_t,size_t>> drawRanges;
  std::string motionProfile="legacy";
  int bladeSide=0; // Long blade/axe baked into a lowered arm; zero for symmetric arms.
};
// Immutable authored rigid-part membership is exported alongside the mesh.
// Movement phase, aim and recoil are the same host-replicated values as combat.
void preloadEnemyAnimationRigs(const std::filesystem::path &root);
const EnemyRig *enemyAnimationRig(const std::filesystem::path &root,
                                  const std::string &asset,size_t vertices);
WalkerPose poseEnemy(const EnemyRig &,float phase,float speed,float aimYaw,
                      float aimPitch,float recoil,float seconds,
                      const EnemyCombatMotion &motion={},unsigned missingArms=0,Vec3 localVelocity={},
                      const VoidmawPoseCue &voidmaw={});
struct EnemySocket {size_t part;Vec3 point,atImpact;};
std::optional<EnemySocket> voidmawBiteSocket(const EnemyRig &,const Target &,float side);
WalkerPose poseLiveEnemy(const EnemyRig &,const Target &,Quaternion bodyOrientation,float seconds);
}
