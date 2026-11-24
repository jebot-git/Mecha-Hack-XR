#include "rpg/mission_board.hpp"
#include "rpg/manual_missions.hpp"
#include "rpg/campaign_story.hpp"
#include <stdexcept>
namespace iron::rpg {
uint32_t missionBoardSeed(uint32_t seed,uint32_t revision) {
  if(!revision)return seed;
  uint32_t value=revision*0x9e3779b9u;
  value=(value^(value>>16))*0x85ebca6bu;
  value=(value^(value>>13))*0xc2b2ae35u;
  return seed^(value^(value>>16));
}
void randomiseEnemies(GameSettings &settings) {
  settings.enemy=settings.theme=settings.tactic=-1;
}
MissionBoard generateMissionBoard(uint32_t seed, const GameSettings &settings,int level,uint32_t revision,int partySize) {
  seed=missionBoardSeed(seed,revision);
  MissionBoard result;
  for (int slot = 0; slot < missionBoardSize; ++slot)
    result[slot] = generateMission(seed, unsigned(slot), settings, level,partySize);
  return result;
}
std::vector<MissionCard> missionCards(const MissionBoard &board) {
  std::vector<MissionCard> result;
  for (const auto &recipe : board) {
    MissionCard card;
    card.art=std::string("briefing-")+missionLocations()[recipe.location].id+"-"+std::to_string(recipe.seed&1);
    card.title = manualMissionName(recipe);
    card.summary = std::string(missionLocations()[recipe.location].name);
    EnvironmentCycle cycle;
    cycle.seed = recipe.seed;
    cycle.startHour = recipe.opener == 1 ? 22 : 8;
    card.briefing = briefing(recipe,&cycle);
    if(recipe.storyArc>=0)card.summary=storyMissionBrief(recipe);
    else card.summary += " / "+std::string(std::array<const char*,3>{"PATROL","FRONTLINE","DANGEROUS"}[recipe.difficulty])+" / "+std::to_string(recipe.enemies.size())+" CONTACTS / "+
                         std::to_string(recipe.rewardCredits)+" CR / "+std::to_string(recipe.maps)+" SECTORS";
    result.push_back(std::move(card));
  }
  return result;
}
Json missionSelectionCommand(int slot) {
  if (slot < 0 || slot >= missionBoardSize)
    throw std::out_of_range("Mission slot must be 0..8");
  return {{"op", "select_mission"}, {"slot", slot}};
}
} // namespace iron::rpg
