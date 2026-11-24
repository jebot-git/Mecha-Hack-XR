#pragma once
#include "simulation.hpp"
namespace iron::rpg {
// Deterministic shared planning quota. Local collision safety is never deferred.
struct EnemyNavigationBudget {int edgeChecks=32, terrainSamples=256;};
// Host-only, disposable route cache. No hidden player data or RNG is required.
struct EnemyNavigationState {
  Vec3 waypoint{}, destination{};
  float remaining=0, recoveryClock=0;
  std::vector<Vec3> path;
  size_t cursor=0;
  int side=0;
  bool searching=false, searchFallback=false;
  int searchCurrent=-1, searchEdgeSample=0;
  size_t searchCursor=0;
  std::vector<Vec3> searchNodes;
  std::vector<float> searchCosts;
  std::vector<int> searchPrevious;
  std::vector<bool> searchClosed;
  std::vector<std::pair<float,size_t>> searchOpen;
  std::vector<size_t> searchNeighbors;
};
// destination is the tactical position (cover/flank/range), not necessarily the
// victim. A zero desired direction means hold; navigation must not override it.
Vec3 enemyNavigationDirection(const World &,size_t actor,Vec3 desiredDirection,
                              Vec3 destination,float speed,float dt,bool airborne,
                              EnemyNavigationState &,EnemyNavigationBudget *budget=nullptr);
// Uses the same compressed collision footprint as World::moveEnemy. Dynamic
// actors are considered for local steering, but do not permanently sever routes.
bool enemyNavigationPassage(const World &,size_t actor,Vec3 from,Vec3 to,
                            bool airborne,bool dynamicActors=false);
}
