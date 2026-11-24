#include "rpg/world_generation.hpp"
#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
using namespace iron;
using namespace iron::rpg;
namespace {
World fixture(uint32_t seed){
  World world;world.targets.clear();world.prototypeSeed=seed;world.missionEntry={0,0,980};
  world.interiorNavNodes={{-950,0,-950},{-950,0,950}};world.interiorNavLinks={{0,1}};
  return world;
}
Json plots(){
  Json result={{"plots",Json::array()}};
  for(int z=0;z<6;++z)for(int x=0;x<6;++x){float xx=(x-2.5f)*280,zz=(z-2.5f)*280;
    result["plots"].push_back({{"id",z*6+x},{"bounds",{xx-95,zz-95,xx+95,zz+95}}});}
  return result;
}
Json signature(const World &world){
  Json result=Json::array();for(const auto &t:world.targets)
    result.push_back({t.origin.x,t.origin.y,t.origin.z,t.yaw,t.half.x,t.half.y,t.half.z,t.mesh->size()});
  return result;
}
void geometry(const World &world){
  assert(world.targets.size()<=missionSceneryTargetLimit);
  for(size_t i=0;i<world.targets.size();++i){const auto &a=world.targets[i];
    assert(a.mesh&&!a.invulnerable&&a.health>0);
    assert(std::hypot(a.origin.x-world.missionEntry.x,a.origin.z-world.missionEntry.z)>=90);
    assert(missionRouteDistance(world,a.center)>std::hypot(a.half.x,a.half.z)+3+14);
    for(size_t j=0;j<i;++j){const auto &b=world.targets[j];
      assert(std::abs(a.center.x-b.center.x)>=a.half.x+b.half.x||
             std::abs(a.center.z-b.center.z)>=a.half.z+b.half.z||
             std::abs(a.center.y-b.center.y)>=a.half.y+b.half.y);
    }
  }
}
}
int main(){
  setOriginalAssetRoot(MECHA_ASSETS);const auto recipe=plots();
  std::set<std::string> allAssets;bool smallScale=false,largeScale=false;
  for(int location=0;location<10;++location){
    std::set<std::string> layouts;std::set<std::string> models;
    for(uint32_t seed:{731u,2871u,9137u}){
      auto world=fixture(seed),repeat=fixture(seed);dressMissionExterior(world,recipe,3,location);
      dressMissionExterior(repeat,recipe,3,location);assert(signature(world)==signature(repeat));geometry(world);
      assert(world.targets.size()>=20);layouts.insert(signature(world).dump());
      for(const auto &t:world.targets){models.insert(t.generatorAsset);allAssets.insert(t.generatorAsset);float span=2*std::max({t.half.x,t.half.y,t.half.z});smallScale|=span<8;largeScale|=span>40;}
    }
    assert(layouts.size()==3&&models.size()>=9);
  }
  assert(allAssets.size()>=40&&smallScale&&largeScale);
  // Dressing follows variable broad rooms; doorway shelter strips receive no
  // furniture, and more than the old hard-coded nine rooms can participate.
  auto inside=fixture(731);inside.interiorNavNodes.clear();inside.interiorNavLinks.clear();
  for(int z=0;z<4;++z)for(int x=0;x<3;++x){float xx=(x-1)*560.f,zz=(z-1.5f)*420.f;
    inside.interiorRegions.push_back({{xx-160,zz-150,xx+160,zz+150},0,48});
  }
  dressMissionInterior(inside,7,0);geometry(inside);assert(inside.targets.size()>=15);
  std::set<int> furnished;
  for(const auto &t:inside.targets)for(size_t i=0;i<inside.interiorRegions.size();++i){auto r=inside.interiorRegions[i].bounds;
    if(t.origin.x>=r[0]&&t.origin.x<=r[2]&&t.origin.z>=r[1]&&t.origin.z<=r[3])furnished.insert(int(i));}
  assert(furnished.size()>9);
  std::ifstream kitFile(std::filesystem::path(MECHA_ASSETS)/"cover-manifest.json");
  Json kit;kitFile>>kit;
  assert(kit.at("assets").size()==14);
  for(const auto &record:kit.at("assets")){
    auto mesh=originalMesh(record.at("id").get<std::string>());
    assert(mesh->size()/3==record.at("triangles").get<size_t>());
    assert(mesh->size()/3<=record.at("budget").get<size_t>());
    assert(std::all_of(mesh->begin(),mesh->end(),[](const auto &v){return v.textured==3;}));
  }
  auto pipe=fixture(5);
  pipe.interiorNavNodes={{0,0,-90},{0,0,85}};pipe.interiorNavLinks={{0,1}};
  buildMissionCoverRoutes(pipe,0,0,500,3);
  assert(pipe.targets.size()==3);
  assert(std::isfinite(pipe.interiorCeilingAt({0,10,0})));
  pipe.position={0,0,-40};pipe.mission=0;
  for(int i=0;i<170;++i)pipe.movePilot(pipe.position+Vec3{0,0,.5f});
  assert(pipe.position.z>40);
  assert(gravityRay(pipe,{0,12,0},{0,50,0}));
  std::cout<<"Seeded scenery purposes, broad asset use, reproducibility, variable rooms and physical spacing verified\n";
}
