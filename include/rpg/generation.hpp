#pragma once
#include "rpg/rules.hpp"
namespace iron::rpg {
inline constexpr int generationVersion=14;
struct GenerationPlan {
  uint32_t seed=0;
  int layout=0,height=0,palette=0,sectionStyle=0;
  bool fullInterior=false,underground=false,submerged=false,surfaceFacility=false;
  bool organic=false,city=false,lowerTier=false,upperTier=false;
  float relief=24;
};
GenerationPlan generationPlan(uint32_t seed,int biome,int location,int region);
void shapeMissionLayout(Json &,const GenerationPlan &,float extent);
float missionLayoutFitness(const Json &,float extent);
void optimiseMissionLayout(Json &,const GenerationPlan &,float extent);
void shapeMissionShores(Json &,const GenerationPlan &);
void districtMissionPlots(Json &,const GenerationPlan &,float extent);
}
