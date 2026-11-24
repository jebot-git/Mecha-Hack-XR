#pragma once
#include "rpg/catalog.hpp"
namespace iron::rpg {
// Original encounter designs use the existing authored rigs and triangle caps.
struct CampaignBoss {
  EnemySpec spec;
  const char *base;
  int faction,tactic;
  const char *counterplay;
  const char *reinforcement;
  float windup=1.4f;
};
std::span<const CampaignBoss> campaignBosses();
const CampaignBoss &campaignBoss(int);
int bossBaseEnemy(int);
class Session;
void validateBossState(const Session &);
}
