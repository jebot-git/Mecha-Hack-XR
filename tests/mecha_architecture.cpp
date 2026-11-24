#include "rpg/world_generation.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include <iostream>
#include <set>
using namespace iron;using namespace iron::rpg;
namespace {void require(bool ok,const char *reason){if(!ok)throw std::runtime_error(reason);}}
int main(){try{
  setOriginalAssetRoot(MECHA_ASSETS);require(skylineCatalog().size()==14,"Skyline library lost required silhouettes");
  std::set<std::string> geometry;size_t triangles=0;
  for(const auto *name:skylineCatalog()){
    Target t;applyOriginalModel(t,name);require(t.mesh&&t.mesh->size()/3<=124,"Skyscraper exceeds original building budget");
    require(t.half.y*2>=60&&t.half.y*2<=112.01f,"Authored skyscraper scale is wrong");
    require(!t.invulnerable,"Tower cannot use normal building destruction");
    require(std::abs(t.center.y-t.half.y)<.001f,"Skyscraper pivot is not on the ground");
    for(const auto &v:*t.mesh){require(v.textured==3&&v.oneSided>=0&&v.oneSided<64,"Facade is not using repeated atlas trims");
      require(std::isfinite(v.u)&&std::isfinite(v.v),"Invalid repeated facade coordinate");}
    for(size_t i=0;i<t.mesh->size();i+=3)require(length(cross((*t.mesh)[i+1].position-(*t.mesh)[i].position,(*t.mesh)[i+2].position-(*t.mesh)[i].position))>1e-6f,"Degenerate tower triangle");
    geometry.insert(std::to_string(t.half.x)+":"+std::to_string(t.half.y)+":"+std::to_string(t.half.z)+":"+std::to_string(t.mesh->size()));triangles+=t.mesh->size()/3;
  }
  require(geometry.size()>=12,"Skyline silhouettes collapse to too few distinct shapes");
  std::array<int,0> empty;
  int worlds=0;
  for(int location:{0,4,5,6,7})for(int region:{0,2})for(float extent:{500.f,1000.f}){
    if(extent>500&&(location!=0||region!=0))continue;
    World w;buildMissionEnvironment(w,4271,3,extent,empty,location,3,region,5,-1,2);++worlds;
    auto density=missionDensity(w);require(density.decorations>=256&&density.largeStructures>=32,"Skyline replaces existing density floors");
    require(density.skyscrapers>=minimumMissionSkyscrapers(3,location,region,extent),"Skyline quota missing");
    std::set<std::string> seen;
    for(const auto &t:w.targets)if(t.active&&t.rpgDecoration&&t.generatorAsset.starts_with("city_tower_")){
      seen.insert(t.generatorAsset);require(!std::isfinite(w.interiorCeilingAt(t.origin,std::max(t.half.x,t.half.z))),"Skyscraper intersects an indoor roof");
      require(t.ai.localHalf.y*2>50,"Placement shrank the tower to generic decoration size");
      require(w.waterDepthAt(t.origin,std::hypot(t.half.x,t.half.z)+19)==0,"Exterior skyscraper stands in a natural water basin");
      require(missionRouteDistance(w,t.center)>std::hypot(t.half.x,t.half.z)+30,"Skyscraper blocks a protected road");
      for(const auto &v:*t.mesh)if(v.position.y<=.05f){auto p=t.origin+rotate(angles(t.yaw),v.position);require(std::abs(p.y-w.surfaceHeight(p.x,p.z))<.1f,"Tower base floats above terrain");}
    }
    require(seen.size()==std::min(size_t(density.skyscrapers),skylineCatalog().size()),"Skyline deck repeats silhouettes before exhausting its catalog");
    if(region>=2||location==7)require(density.skyscrapers==0,"Exterior skyscrapers appear inside a facility");
  }
  std::cout<<"14 skyline models / "<<triangles<<" triangles / "<<worlds<<" contextual worlds: source budgets, repeating facades, full-size placement (including 16 towers on a large city), grounded bases and protected routes pass\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
