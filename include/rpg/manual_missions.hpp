#pragma once
#include "rpg/mission.hpp"
namespace iron::rpg {
// Concise operational adaptations of the Mission Manual. Human-scale phases
// are omitted; every objective starts and ends in the player's own cockpit.
struct ManualMission {
  const char *id;
  const char *name;
  int page;
  std::array<int,3> objectives;
  std::array<const char *,3> tasks;
  int preferredLocation;
  int countdown;
  int hold;
};
std::span<const ManualMission> manualMissionCatalog();
std::span<const MissionChoice> instantActions();
std::span<const MissionChoice> instantFoci();
std::span<const MissionChoice> manualComplications();
std::span<const MissionChoice> manualOpportunities();
std::span<const MissionChoice> manualBriefings();
std::span<const MissionChoice> planetoidFeatures();
std::span<const MissionChoice> planetoidHazards();
std::span<const MissionChoice> salvageForms();
std::span<const MissionChoice> salvageFeatures();
void expandManualMission(MissionRecipe &,Dice &);
std::string manualMissionName(const MissionRecipe &);
std::string manualMissionTask(const MissionRecipe &);
enum class ManualRng : uint32_t {Mission,Instant,Briefing,Planetoid,Complication,Opportunity,Salvage,Appearance,Chatter,Critical};
uint32_t manualRandom(uint32_t seed,ManualRng,unsigned index=0);
struct ManualAppearance {
  int paint=0,accent=-1,feature=0,extraFeature=-1,look=0,extraLook=-1;
  Vec3 tint{1,1,1};
  std::string nickname;
};
ManualAppearance manualAppearance(uint32_t seed,unsigned index,bool kaiju);
std::string manualTechnobabble(uint32_t seed,unsigned index=0);
std::span<const MissionChoice> mechaPaints();
std::span<const MissionChoice> mechaFeatures();
std::span<const MissionChoice> kaijuLooks();
std::span<const MissionChoice> kaijuFeatures();
std::span<const MissionChoice> kaijuNamePrefixes();
std::span<const MissionChoice> kaijuNameSuffixes();
std::span<const MissionChoice> friendlyChatter();
std::span<const MissionChoice> enemyChatter();
std::span<const MissionChoice> techPrefixes();
std::span<const MissionChoice> techSuffixes();
std::span<const MissionChoice> criticalCockpitEffects();
std::span<const MissionChoice> criticalExteriorEffects();
// Independent helpers also support reproducible headless catalog/audit tools.
float objectiveDuration(const MissionRecipe &,float fallback);
bool validManualRecipe(const MissionRecipe &);
}
