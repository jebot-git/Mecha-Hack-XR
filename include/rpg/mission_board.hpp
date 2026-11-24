#pragma once
#include "frontend.hpp"
#include "rpg/session.hpp"
namespace iron::rpg {
inline constexpr int missionBoardSize = 9;
using MissionBoard = std::array<MissionRecipe, missionBoardSize>;
// Host revision selects a fresh deterministic set; clients never roll independently.
uint32_t missionBoardSeed(uint32_t seed,uint32_t revision);
MissionBoard generateMissionBoard(uint32_t seed, const GameSettings &,int level=1,uint32_t revision=0,int partySize=1);
MissionBoard generateMissionBoard(const Session &);
std::vector<MissionCard> campaignCards(const Session &);
void randomiseEnemies(GameSettings &);
std::vector<MissionCard> missionCards(const MissionBoard &);
Json missionSelectionCommand(int slot);
} // namespace iron::rpg
