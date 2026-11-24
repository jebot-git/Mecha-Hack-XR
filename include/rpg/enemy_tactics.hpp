#pragma once
#include "rpg/catalog.hpp"
#include <cstdint>
#include <vector>
namespace iron::rpg {
class Session;
struct Member;
// Host-only derived intent. Snapshot simulation already replicates actual poses
// and projectiles; these indices never become part of the wire protocol.
struct EnemyTacticalDecision {
  int memberIndex=-1;
  uint32_t targetId=0;
  Vec3 destination{};
  bool positioning=false,coordinated=false,fireLaneClear=true;
  int partner=-1;
};
bool enemyUnitsCompatible(const EnemySpec &,const EnemySpec &);
bool enemyCanSeeMember(const Session &,size_t,const Member &);
// Uses the actual muzzle and ignores only the shooter. Allied actors and
// scenery both block shots, including members of a different cooperation group.
bool enemyFiringLaneClear(const Session &,size_t,Vec3 destination,int ignoredTarget=-1);
// Returns a short lateral drive while a mobile gunner's weapons recover.
// The caller passes this direction through normal terrain and actor navigation.
Vec3 enemyRecoveryStrafe(const Session &,size_t actor,Vec3 target);
std::vector<EnemyTacticalDecision> planEnemyTactics(
    const Session &,std::span<const uint32_t> previousTargets={});
}
