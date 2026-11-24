#pragma once
#include <array>

namespace iron::rpg {
// Stable asset IDs in saved chassis order. Shared by the game and renderer.
inline constexpr std::array<const char*,8> chassisIds{
    "brawler", "scout", "striker", "titan", "auxiliary", "colossus", "hybrid", "vanguard"
};
}
