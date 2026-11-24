#pragma once
#include <array>
#include <vector>
#include <nlohmann/json_fwd.hpp>
namespace iron {
struct World;
struct ProceduralWater {
    std::array<float,4> bounds{};
    float level=-1,depth=6,bankWidth=0;
    // Optional star-shaped shore, counterclockwise around the bounds centre.
    // The empty form retains legacy rectangular water exactly.
    std::vector<std::array<float,2>> shore{};
};
float proceduralWaterEdge(const ProceduralWater&,float x,float z);
// Version-three terrain is a physical height field; water is separate from the
// indexed bed. Optional graded banks support shallow wading; deep water still
// requires a complete dry crossing. Legacy worlds leave maxWadeDepth at zero.
void buildSegmentedTerrain(World&,const nlohmann::json&);
void buildProceduralTerrain(World&,const nlohmann::json&);
void validateProceduralTerrain(const nlohmann::json&);
float proceduralRelief(const nlohmann::json& terrain,float x,float z);
}
