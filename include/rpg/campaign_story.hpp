#pragma once
#include "rpg/campaign.hpp"
#include "rpg/catalog.hpp"
#include <string>
#include <vector>
namespace iron::rpg {
inline constexpr int campaignStoryCount=8;
class Session;
struct GameSettings;
struct MissionRecipe;
const nlohmann::json &campaignStoryCatalog();
int storyDominant(const std::array<int,3> &,int tie=-1);
CampaignContext storyContext(const Session &,int slot,CampaignContext);
GameSettings storySettings(const GameSettings &,const CampaignContext &);
void applyStoryMission(MissionRecipe &,const CampaignContext &,float extent);
void recordStoryVictory(CampaignState &,const MissionRecipe &,int structuresLost);
void inheritCampaignLegacy(CampaignState &,const CampaignState &);
CampaignState restartCampaign(const CampaignState &);
std::string storyMissionTitle(const MissionRecipe &);
std::string storyMissionBrief(const MissionRecipe &);
std::vector<std::string> storyMissionBriefing(const MissionRecipe &);
std::string storyMissionStakes(const MissionRecipe &);
std::string storyContinuity(const MissionRecipe &);
std::string storyEnding(const CampaignState &);
std::vector<std::string> storyDebrief(const Session &);
void preferStoryRoster(MissionRecipe &);
std::string storyEnemyModel(const MissionRecipe *,int spec,const std::string &fallback);
bool validStoryRecipe(const MissionRecipe &);
}
