#pragma once
#include "vector.hpp"
#include <cstddef>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace iron {
struct World;
struct Target;
struct ProceduralSpawnSite;
enum class ProceduralSpawnProfile { Armour, Emplacement, Walker, AirPatrol };
enum class ProceduralGoalKind { Eliminate, Strike, Recon };
struct ProceduralObjective {
    ProceduralGoalKind kind=ProceduralGoalKind::Eliminate;
    Vec3 position{};
    float radius=8,holdSeconds=4,progress=0;
    std::vector<size_t> targets;
    int segment=0;
    unsigned kills=0,killQuota=0;
    bool complete=false;
};
struct ProceduralMission {
    std::vector<ProceduralObjective> objectives;
    Vec3 extraction{};
    float extractionRadius=8;
    int segmentCount=1;
    bool orderedSegments=false;
    bool requireExtraction=false,extracting=false;
};
// Recheck recipes in native code: the CLI does not require the Python validator.
void validateProceduralReinforcements(const nlohmann::json&);
void prepareContinuousProceduralReinforcements(World&,const nlohmann::json&,const std::vector<ProceduralSpawnSite>&);
void stepContinuousProceduralReinforcements(World&,float dt);
void validateProceduralSegments(const nlohmann::json&);
void validateProceduralRecipe(const nlohmann::json&);
void updateProceduralMission(World&,float dt);
void prepareProceduralReinforcements(World&,unsigned perZone,float interval);
void populateProceduralEncounters(World&,const nlohmann::json&,const std::vector<Target>&);
}
