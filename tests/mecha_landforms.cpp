#include "rpg/generation.hpp"
#include "rpg/session.hpp"
#include "rpg/world_generation.hpp"
#include "skirmish.hpp"
#include <cassert>
#include <iostream>
#include <queue>
#include <set>
using namespace iron;using namespace iron::rpg;
int main(){
  std::set<std::string> forms;
  for(int family=0;family<5;++family){
    SkirmishSettings settings;settings.cells=7;settings.seed=713;settings.water="none";
    auto recipe=generateSkirmishRecipe(settings),repeat=recipe;
    GenerationPlan plan;plan.seed=713;plan.layout=family;plan.surfaceFacility=true;
    shapeMissionLayout(recipe,plan,500);shapeMissionLayout(repeat,plan,500);
    assert(recipe==repeat&&recipe["version"]==4);
    auto nodes=recipe.at("nodes").get<std::vector<std::array<float,2>>>();
    std::vector<std::vector<size_t>> graph(nodes.size());
    for(const auto &road:recipe.at("roads")){
      auto ends=road.at("nodes").get<std::array<size_t,2>>();assert(ends[0]<nodes.size()&&ends[1]<nodes.size());
      auto a=nodes[ends[0]],b=nodes[ends[1]];
      if(std::abs(a[0]-b[0])>=.001f&&std::abs(a[1]-b[1])>=.001f)
        throw std::runtime_error("Rectangular road is diagonal in family "+std::to_string(family)+": "+Json(a).dump()+" -> "+Json(b).dump());
      graph[ends[0]].push_back(ends[1]);graph[ends[1]].push_back(ends[0]);
    }
    std::set<size_t> seen{0};std::queue<size_t> queue;queue.push(0);
    while(!queue.empty()){size_t here=queue.front();queue.pop();for(auto next:graph[here])if(seen.insert(next).second)queue.push(next);}
    assert(seen.size()==nodes.size()&&recipe["roads"].size()>nodes.size());
    assert(recipe["plots"].size()>=5);
    forms.insert(recipe["nodes"].dump()+recipe["roads"].dump());
    World world;world.prototypeSeed=713;world.movementBounds={-500,-500,500,500};
    buildProceduralTerrain(world,recipe);
    assert(!world.geometry().terrainPatches.empty()&&world.geometry().terrain.size()<1800000);
  }
  assert(forms.size()==5);
  std::set<std::string> profiles;std::set<int> families;
  // Sample real triangulated terrain rather than accepting profile labels.
  for(uint32_t seed=0;seed<24;++seed){
    SkirmishSettings settings;settings.cells=4;settings.seed=seed;settings.water="none";
    auto recipe=generateSkirmishRecipe(settings);recipe["bounds"]={-500,-500,500,500};
    recipe["version"]=4;recipe["terrain"]["amplitude"]=0;
    World world;world.prototypeSeed=seed;world.movementBounds={-500,-500,500,500};
    world.missionEntry={0,0,325};buildProceduralTerrain(world,recipe);buildMissionElevation(world,3,0,500,0);
    auto plan=generationPlan(seed,0,3,0);families.insert(plan.height);
    Json heights=Json::array();float low=1e9f,high=-1e9f;
    for(int z=-350;z<=350;z+=70)for(int x=-350;x<=350;x+=70){float y=world.surfaceHeight(x,z);assert(std::isfinite(y));heights.push_back(int(std::lround(y*100)));low=std::min(low,y);high=std::max(high,y);}
    assert(high-low>8);profiles.insert(heights.dump());
    assert(std::abs(world.surfaceHeight(0,325))<.01f);
    World walker=world;walker.position={0,world.surfaceHeight(0,325),325};
    for(int step=0;step<100;++step)walker.movePilot(walker.position+Vec3{0,0,-.5f});
    assert(walker.position.z<280); // A real mech can leave the flat insertion court.
  }
  assert(families.size()==6&&profiles.size()>=20);
  std::cout<<"Five connected spatial families, six physical height profiles and graded insertion traversal verified\n";
}
