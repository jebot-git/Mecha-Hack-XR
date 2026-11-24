#pragma once
#include "procedural.hpp"
#include "environment.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <nlohmann/json_fwd.hpp>
namespace iron {
inline constexpr std::array<std::string_view,5> skirmishTerrainOptions{"auto","urban","grassland","desert","hills"};
inline constexpr std::array<std::string_view,4> skirmishWaterOptions{"auto","none","lake","canal"};
inline constexpr int skirmishMinCells=2,skirmishMaxCells=8;
inline constexpr int skirmishMinDensity=1,skirmishMaxDensity=16;
inline constexpr int skirmishMinGroundPerZone=1,skirmishMaxGroundPerZone=12;
inline constexpr int skirmishMinAirUnits=0,skirmishMaxAirUnits=8;
struct SkirmishSettings {
    uint32_t seed=1;
    ProceduralGoalKind kind=ProceduralGoalKind::Eliminate;
    std::string terrain="auto",water="auto";
    int cells=4,density=9,groundPerZone=6,airUnits=3;
    bool extraction=true;
    EnvironmentSettings environment;
    bool environmentCycle=true;
    int reinforcementsPerZone=3;float reinforcementInterval=20;
};
Climate skirmishClimate(const SkirmishSettings&);
// Native generation needs no Python interpreter, recipe sidecar.
// The ordered xorshift32-v3 stream is reproducible across standard libraries.
nlohmann::json generateSkirmishRecipe(const SkirmishSettings&);
}
