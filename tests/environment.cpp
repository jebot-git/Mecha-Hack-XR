#include "environment.hpp"
#include "simulation.hpp"
#include "skirmish.hpp"
#include "presentation.hpp"
#include "jaguar_cry.hpp"
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
using namespace iron;
int main(){
 for(auto l:lightingNames)for(auto w:weatherNames){auto settings=readEnvironment({{"lighting",l},{"weather",w},{"seed",UINT32_MAX}});assert(environmentWire(settings)["lighting"]==l);
  auto a=environmentVisuals(settings,false,{.2f,.3f,.4f},{.1f,.2f,.1f},10),b=environmentVisuals(settings,false,{.2f,.3f,.4f},{.1f,.2f,.1f},10);
  assert(std::isfinite(length(a.sky))&&a.density>=0&&a.solidGain>0&&length(a.sky-b.sky)==0);
  assert(a.night==(l=="night"));if(w=="clear")assert(a.density==0&&a.precipitation==0);else assert(a.density>0);
 }
 EnvironmentSettings night;night.lighting=Lighting::Night;auto v=environmentVisuals(night,false,{},{},0);assert(v.night&&v.solidGain<1);
 night.lighting=Lighting::Day;v=environmentVisuals(night,true,{},{},0);assert(!v.night&&v.solidGain>1);
 assert(length(jaguarCRY(0))==0&&length(jaguarCRY(0x6857))>0);
 EnvironmentSettings particles;particles.weather=Weather::Sandstorm;auto dust=weatherSprites(particles,{0,12,0},1.25f);assert(dust.size()==128);
 unsigned grains=0,sheets=0,veils=0;auto moved=weatherSprites(particles,{.1f,12,.1f},1.25f),later=weatherSprites(particles,{0,12,0},1.25f+1.f/120);
 for(size_t i=0;i<dust.size();++i){const auto& d=dust[i];
  assert(d.opacity>=0&&d.opacity<=1&&d.frame<weatherSpriteFrames);
  float extent=d.layer==WeatherLayer::SandGrain?44:d.layer==WeatherLayer::SandSheet?96:68;
  if(std::abs(d.position.x)<extent*.5f-1&&std::abs(d.position.z)<extent*.5f-1)assert(length(d.position-moved[i].position)<.001f); // walking does not drag nearby dust
  if(d.layer==WeatherLayer::SandGrain){++grains;assert(d.width>d.height*5);auto delta=later[i].position-d.position;
   if(std::abs(delta.x)<2&&std::abs(delta.z)<2){assert(delta.x>0&&delta.z<0);assert(std::abs(delta.z+.38f*delta.x)<.001f);assert(std::abs(delta.y)<.01f);}
  }else if(d.layer==WeatherLayer::SandSheet){++sheets;assert(d.width>=9&&d.height<3);}
  else if(d.layer==WeatherLayer::SandVeil){++veils;assert(d.width>=15&&d.height>=5&&d.opacity<.2f);}
 }
 assert(grains==96&&sheets==16&&veils==16);
 auto repeated=weatherSprites(particles,{0,12,0},1.25f);assert(repeated[0].opacity==dust[0].opacity&&length(repeated[0].position-dust[0].position)==0);
 particles.weather=Weather::Rain;auto rain=weatherSprites(particles,{},1.25f);assert(rain.size()==96&&rain[0].width<rain[0].height);
 for(const auto& sprite:rain)assert(sprite.frame<weatherSpriteFrames&&std::isfinite(length(sprite.position)));
 auto again=weatherSprites(particles,{},1.25f);assert(again[0].frame==rain[0].frame&&length(again[0].position-rain[0].position)==0);
 particles.weather=Weather::Clear;assert(weatherSprites(particles,{},1).empty());
 assert(weatherNoise(42,99)==weatherNoise(42,99)&&weatherNoise(42,99)!=weatherNoise(42,100));
 for(auto invalid:{nlohmann::json{{"lighting","dusk"}},nlohmann::json{{"weather","snow"}},nlohmann::json{{"seed",-1}},nlohmann::json{{"seed",4294967296ull}}}){bool rejected=false;try{readEnvironment(invalid);}catch(const std::exception&){rejected=true;}assert(rejected);}
 SkirmishSettings settings;settings.environment=readEnvironment({{"lighting","night"},{"weather","storm"},{"seed",42}});auto recipe=generateSkirmishRecipe(settings);
 assert(recipe["environment"]["weather"]=="storm"&&recipe["reinforcements"]["per_zone"]==3);validateProceduralRecipe(recipe);
 for(auto key:{"per_zone","interval"}){auto invalid=recipe;invalid["reinforcements"][key]=-1;bool bad=false;try{validateProceduralRecipe(invalid);}catch(const std::exception&){bad=true;}assert(bad);}
 Target ally;ally.team=Team::Defenders;assert(length(radarColor(ally,0)-teamColour(ally.team))==0);
 assert(length(teamColour(Team::Attackers)-teamColour(Team::Defenders))>1);
 std::cout<<"Environment profiles, lighting overrides, stateless weather and team palette passed\n";
}
