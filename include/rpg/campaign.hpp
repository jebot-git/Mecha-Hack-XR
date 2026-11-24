#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <nlohmann/json.hpp>
namespace iron::rpg {
inline constexpr unsigned campaignMissionMask=511;
inline constexpr int voidmawFaction=3;
using HumanStandings=std::array<int,3>;
int hostilityPressure(const HumanStandings &,int opponent);
struct CampaignContext {
  int stage=1, employer=-1, adversary=-1, pressure=0, intel=0, supply=0;
  int storyArc=-1,storyApproach=-1,storyFront=0,storyCollateral=0;
  std::array<int,3> storyTracks{};
  int legacyArc=-1,legacyEnding=-1,legacyScars=0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CampaignContext,stage,employer,adversary,pressure,intel,supply,storyArc,storyApproach,storyFront,storyCollateral,storyTracks,legacyArc,legacyEnding,legacyScars)
// Record the generator inputs, rather than duplicating nine full maps in saves.
struct CampaignRecord {
  bool complete=false;
  uint32_t boardSeed=0;
  int level=1,partySize=1,structuresLost=0;
  CampaignContext context;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CampaignRecord,complete,boardSeed,level,partySize,structuresLost,context)
struct CampaignState {
  bool active=false,awaitingAdvance=false,victorious=false;
  uint32_t seed=0,revision=0,consequences=0,randomState=1;
  unsigned completed=0;
  HumanStandings standings{};
  int intel=0,supply=0,devastation=0;
  std::array<CampaignRecord,9> history{};
  int storyArc=-1,storyLastApproach=-1,storyCollateral=0;
  std::array<int,3> storyTracks{};
  int legacyArc=-1,legacyEnding=-1,legacyScars=0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CampaignState,active,awaitingAdvance,victorious,seed,revision,consequences,randomState,completed,standings,intel,supply,devastation,history,storyArc,storyLastApproach,storyCollateral,storyTracks,legacyArc,legacyEnding,legacyScars)
int campaignCompleted(const CampaignState &);
int campaignLevel(const CampaignState &);
bool campaignFinale(const CampaignState &);
void validateCampaign(const CampaignState &);
class Session;
struct Sheet;
enum class Chassis;
struct MissionRecipe;
bool canRegenerateCampaign(const Session &);
MissionRecipe campaignMission(const Session &,int slot,int sector=0);
using CampaignAwardKey=std::array<uint32_t,4>;
CampaignAwardKey campaignAwardKey(const CampaignState &);
Sheet claimCampaignAward(const Sheet &,const CampaignState &,std::optional<Chassis> nextChassis={});
}
