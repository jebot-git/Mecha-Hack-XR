#include "cockpit_lighting.hpp"
#include "simulation.hpp"
#include <cassert>
#include <iostream>
using namespace iron;
int main(){
 World map;map.skyColor={.02f,.02f,.02f};map.groundColor={.01f,.01f,.01f};map.missionLight={0,1,0};
 auto day=cockpitLighting(map);map.night=true;auto night=cockpitLighting(map);assert(night.night&&!day.night);
 Vec3 material{.8f,.8f,.8f},point{0,0,-1},normal{0,1,0};
 auto daylight=bakeCockpitColour(point,normal,material,0,day),red=bakeCockpitColour(point,normal,material,0,night);
 assert(length(red)<length(daylight)&&red.x>red.y&&red.x>red.z&&red.x<.08f); // faint red night fill, not daylight brightness
 auto emissive=bakeCockpitColour(point,normal,material,1,night);assert(emissive.x==material.x&&emissive.y==material.y&&emissive.z==material.z);
 auto different=day;different.direction={0,-1,0};assert(length(bakeCockpitColour(point,normal,material,0,different)-daylight)>.02f);
 different=day;different.sky={.8f,.1f,.1f};auto warm=bakeCockpitColour(point,normal,material,0,different);assert(warm.x>warm.y);
 std::vector<float> source{0,0,-1,0,1,0,.8f,.8f,.8f,0, 0,0,-1,0,1,0,.2f,.9f,.1f,1};auto original=source;
 auto bakedNight=bakeCockpitVertices(source,night),bakedDay=bakeCockpitVertices(source,day);assert(source==original&&bakedNight!=bakedDay);
 assert(bakedNight[16]==source[16]&&bakedNight[17]==source[17]&&bakedNight[18]==source[18]); // green emissive stays green
 assert(bakeCockpitVertices(source,night)==bakedNight); // repeated/restarted map bakes from original colours
 for(size_t i=0;i<source.size();++i)if(i%10<6||i%10==9)assert(bakedNight[i]==source[i]); // preserve geometry, normals, emission masks
 map.assault=true;map.environment.lighting=Lighting::Day;assert(!cockpitLighting(map).night);map.night=false;map.environment.lighting=Lighting::Night;assert(cockpitLighting(map).night);
 map.environment.weather=Weather::Storm;auto stable=cockpitLighting(map);map.elapsed=120;map.chassisYaw=2;assert(cockpitLighting(map)==stable); // no eye/heading/frame flash rebake
 auto stormBake=bakeCockpitVertices(source,stable);float strike=34-weatherNoise(map.environment.seed,0)*10;
 for(float phase:{-.01f,.01f,.09f,.11f,.19f,.21f}){map.elapsed=strike+phase;
  assert(cockpitLighting(map)==stable&&cockpitLighting(map).night);
  assert(bakeCockpitVertices(source,cockpitLighting(map))==stormBake);
 }
 map.assault=false;assert(cockpitLighting(map).night==map.night); // campaign override ignored
 bool rejected=false;try{bakeCockpitVertices(std::span<const float>(source).first(9),night);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 std::cout<<"Map cockpit bake: faint night red, authored/override lighting, immutable materials and emissive preservation passed\n";
}
