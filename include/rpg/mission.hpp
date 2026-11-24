#pragma once
#include "rpg/catalog.hpp"
#include "rpg/rules.hpp"
#include "rpg/encounters.hpp"
namespace iron::rpg {
struct MissionChoice {
  const char *id;
  const char *name;
};
std::span<const MissionChoice> enemyThemes();
std::span<const MissionChoice> missionObjectives();
std::span<const MissionChoice> missionLocations();
std::span<const MissionChoice> missionOpeners();
std::span<const MissionChoice> enemyTactics();
struct MissionRecipe {
  uint32_t seed = 0;
  int theme = 0, objective = 0, location = 0, opener = 0, tactic = 0,
      environment = 3;
  std::vector<int> enemies;
  std::vector<int> tactics;
  int maps=1;
  float radius=0; // Zero uses the configured extent in older snapshots.
  int depotCount=-1; // Legacy missions put one depot in the final sector.
  int rewardCredits=0;
  int partySize=1,level=1,difficulty=1,threatBudget=0,siteCount=0;
  int manualMission=-1,manualVariant=0,complication=-1,opportunity=-1,
      briefingStyle=-1,planetoidFeature=-1,planetoidHazard=-1,
      instantAction=-1,instantFocus=-1,salvageForm=-1,salvageFeature=-1,
      objectiveCount=0;
  float objectiveSeconds=0,countdownSeconds=0,rechargeSeconds=0;
  int campaignStage=1,employer=-1,adversary=-1,factionPressure=0,campaignIntel=0,campaignSupply=0;
  int storyArc=-1,storyApproach=-1,storyFront=0,storyCollateral=0;
  std::array<int,3> storyTracks{};
  int legacyArc=-1,legacyEnding=-1,legacyScars=0;
  int boss=-1;
  bool finale=false,finaleBattle=false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MissionRecipe, seed, theme, objective,
                                   location, opener, tactic, environment,
                                   enemies,tactics,maps,radius,depotCount,rewardCredits,partySize,level,difficulty,threatBudget,siteCount,manualMission,manualVariant,complication,opportunity,briefingStyle,planetoidFeature,planetoidHazard,instantAction,instantFocus,salvageForm,salvageFeature,objectiveCount,objectiveSeconds,countdownSeconds,rechargeSeconds,campaignStage,employer,adversary,factionPressure,campaignIntel,campaignSupply,boss,finale,finaleBattle,storyArc,storyApproach,storyFront,storyCollateral,storyTracks,legacyArc,legacyEnding,legacyScars)
struct SalvageDepot {
  int target=-1;
  bool opened=false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SalvageDepot,target,opened)
struct MissionState {
  MissionRecipe recipe;
  float progress = 0, openerTime = 0;
  int stage = 0;
  bool recovered = false;
  int mapIndex=0,mapCount=1;
  int contractReward=0,encounterCredits=0;
  std::vector<SalvageDepot> depots;
  bool exitOpen=false,recoveryOpen=false;
  float transitTime=0;
  std::array<float,3> exit{};
  std::vector<int> assets;
  std::vector<bool> visited;
  float manualElapsed=0,manualPulse=0;
  bool manualOpportunity=false;
  int structuresLost=0;
  unsigned offendedNeutral=0,withdrawnAllies=0,lostAllies=0,supportedFactions=0;
  std::array<int,4> factionKills{};
  std::vector<EncounterGroup> groups;
  std::vector<DungeonSite> sites;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MissionState, recipe, progress, openerTime,
                                   stage, recovered, assets, visited,mapIndex,mapCount,exitOpen,recoveryOpen,transitTime,exit,contractReward,encounterCredits,depots,manualElapsed,manualPulse,manualOpportunity,groups,sites,structuresLost,offendedNeutral,withdrawnAllies,lostAllies,supportedFactions,factionKills)
void fitMissionPacing(MissionRecipe &,const GameSettings &);
MissionRecipe generateMission(uint32_t seed, unsigned number,
                              const GameSettings &, int level,int partySize=1);
int contractCredits(const MissionRecipe &);
std::vector<std::string> briefing(const MissionRecipe &, const EnvironmentCycle *cycle=nullptr);
struct TacticalPlan {
  Vec3 direction{};
  float speed = 1, spread = 1;
  bool fire = true, preferMelee = false;
};
TacticalPlan tacticalPlan(int tactic, Vec3 position, Vec3 victim, float optimal,
                          float fireClock, float clock, unsigned index,
                          Vec3 anchor, bool ambushed);
void addMissionAssets(World &, MissionState &, float extent);
} // namespace iron::rpg
