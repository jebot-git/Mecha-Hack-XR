#pragma once
#include "simulation.hpp"
#include <nlohmann/json.hpp>
namespace iron::rpg {
void buildMissionElevation(World &, int location, int region, float extent,int biome=3,int landform=-1);
void buildMissionInterior(World &, int location, int region, float extent,int biome=3);
void addMissionDeck(World &,const World::InteriorDeck &,Vec3 color);
void buildMissionCrossing(World &,int biome,int location,int region,float extent);
void buildMissionCoverRoutes(World &,int location,int region,float extent,int biome);
void buildMissionCanalEntry(World &, int biome, int location, int region, int opener);
inline constexpr size_t missionSceneryTargetLimit = 1152;
inline constexpr int minimumMissionDecorations=320,minimumMissionLargeStructures=48;
struct MissionDensity {int decorations=0,largeStructures=0,skyscrapers=0;};
MissionDensity missionDensity(const World &);
std::span<const char *const> skylineCatalog();
int minimumMissionSkyscrapers(int biome,int location,int region,float extent);
void ensureMissionDensity(World &,int biome,int location,int region,float extent);
float missionRouteDistance(const World &, Vec3);
bool addMissionProp(World &, const char *model, Vec3 position, float yaw,
                    float health = 100, bool wet = false,float scale=1,bool fitTerrain=false,float streetSetback=-1);
void dressMissionExterior(World &,const nlohmann::json &,int biome,int location);
void dressMissionInterior(World &, int location, int region);
void addStructuralPiece(World &, const char *model, Vec3 center, Vec3 half,
                        Vec3 color = {.24f,.27f,.29f});
}
