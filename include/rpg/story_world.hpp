#pragma once
#include "rpg/mission.hpp"
namespace iron::rpg {
// Refine a freshly built scene before mission assets/deployables are appended.
// Existing actor/scene indices and authored routes remain stable.
void dressCampaignStory(World &,const MissionRecipe &,float extent);
const char *campaignStoryWorldName(int storyArc);
}
