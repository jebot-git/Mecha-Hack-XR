#include "rpg/generation.hpp"
#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/world_generation.hpp"
#include "skirmish.hpp"
#include <fstream>
#include <iostream>
#include <queue>
#include <set>
using namespace iron;using namespace iron::rpg;
namespace {
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
Json recipe(uint32_t seed=713){SkirmishSettings settings;settings.seed=seed;settings.cells=7;settings.water="none";
  auto r=generateSkirmishRecipe(settings);r["bounds"]={-500,-500,500,500};r["terrain"]["amplitude"]=0;return r;}
}
int main(int argc,char **argv){try{
  setOriginalAssetRoot(MECHA_ASSETS);Json report={{"generator_version",generationVersion},{"landscapes",Json::array()}};
  std::set<std::string> geometries;size_t diagonals=0,frontages=0,parcels=0;
  for(uint32_t seed=0;seed<20;++seed){auto r=recipe(seed),repeat=r;auto plan=generationPlan(seed,5,1,0);
    shapeMissionLayout(r,plan,500);shapeMissionLayout(repeat,plan,500);require(r==repeat,"Landscape is not deterministic");
    require(missionLayoutFitness(r,500)>0,"Natural routes are disconnected");require(r["districts"].size()>=5,"Too few coherent landscape districts");
    auto points=r.at("nodes").get<std::vector<std::array<float,2>>>();
    for(const auto &road:r["roads"]){auto e=road["nodes"].get<std::array<size_t,2>>();auto a=points[e[0]],b=points[e[1]];
      diagonals+=std::abs(a[0]-b[0])>1&&std::abs(a[1]-b[1])>1;}
    for(const auto &plot:r["plots"]){auto b=plot["bounds"].get<std::array<float,4>>();float x=(b[0]+b[2])/2,z=(b[1]+b[3])/2,best=1e9f;
      for(auto p:points)best=std::min(best,std::hypot(x-p[0],z-p[1]));
      frontages+=best<160;++parcels;require(plot["district"].get<size_t>()<r["districts"].size(),"Parcel refers to missing district");}
    geometries.insert(r["nodes"].dump());
  }
  require(geometries.size()==20&&diagonals>100&&parcels>60&&frontages>parcels/2,"Winding roads/frontage or seed variety missing");
  // Isolated physical fixture: the diagonal must cross the wet centre, without
  // draining its entire AABB, and polygon corners must really remain dry.
  auto r=recipe();r["version"]=4;r["plots"]=Json::array();r["nodes"]={{-210,-210},{210,210}};
  r["roads"]=Json::array({{{"nodes",{0,1}},{"width",64},{"geometry","ribbon"}}});
  r["terrain"]["water"]=Json::array({{{"bounds",{-180,-180,180,180}},{"level",-1},{"depth",12},{"bank_width",32}}});
  GenerationPlan p;p.seed=73;p.organic=true;shapeMissionShores(r,p);validateProceduralTerrain(r);
  World w;w.maxWadeDepth=6;buildProceduralTerrain(w,r);
  require(w.waterRegions.front().shore.size()==24&&w.geometry().waterMesh.size()==72,"Shore mesh is not the shaped contour");
  require(w.waterDepthAt({0,0,0},3.5f)==0&&w.groundPassable({0,0,0},3.5f),"Real road ribbon does not cross the water");
  require(w.waterDepthAt({-75,0,75})>10&&!w.groundPassable({-75,0,75}),"Diagonal AABB incorrectly drains neighboring water");
  require(!w.hasWater(175,175)&&w.waterDepthAt({175,0,175})==0,"Shore bounding box still behaves as water");
  auto malformed=r;malformed["terrain"]["water"][0]["shore"][4]={999,999};bool rejected=false;
  try{validateProceduralTerrain(malformed);}catch(const std::exception&){rejected=true;}require(rejected,"Invalid shore point accepted");
  // Actual mecha and NPC traversal along generated oblique routes, plus
  // deterministic client reconstruction, are covered by the physical audit.
  std::array<int,0> empty;std::set<size_t> footprints;
  for(auto setting:std::vector<std::array<int,2>>{{0,3},{1,5},{3,0},{4,3},{8,2}}){
    World world;std::vector<DungeonSite> sites;buildMissionEnvironment(world,4271,setting[1],500,empty,setting[0],3,0,5,-1,2,&sites);
    require(world.geometry().terrain.size()<=1800000&&world.targets.size()<=maxEncounterTargets,"Landscape exceeds runtime budget");
    for(const auto &site:sites){require(site.rooms.size()>=3,"Unusable keyed site");footprints.insert(site.rooms.size());
      for(const auto &room:site.rooms){Vec3 position{room.position[0],room.position[1],room.position[2]};
        require(world.indoors(position+Vec3{0,12,0})&&world.interiorCeilingAt(position)-position.y>=32,"Generated room loses mecha headroom");}}
    report["landscapes"].push_back({{"location",setting[0]},{"biome",setting[1]},{"sites",sites.size()},{"nodes",world.interiorNavNodes.size()},
      {"triangles",world.geometry().terrain.size()/3},{"targets",world.targets.size()}});
  }
  size_t routeSamples=0;Json corpus=Json::array();
  for(int sample=0;sample<16;++sample){int location=std::array<int,4>{0,1,3,4}[sample%4],biome=location==1?5:location==3?0:3;uint32_t seed=89+unsigned(sample)*103;
    World world;buildMissionEnvironment(world,seed,biome,500,empty,location,3,0,5,-1,2);
    auto density=missionDensity(world);require(density.decorations>=minimumMissionDecorations&&density.largeStructures>=minimumMissionLargeStructures,"Density floors missing on expanded seed corpus");
    std::set<unsigned> seen{0};std::queue<unsigned> queue;queue.push(0);
    while(!queue.empty()){auto n=queue.front();queue.pop();for(auto e:world.interiorNavLinks){unsigned next=e[0]==n?e[1]:e[1]==n?e[0]:unsigned(world.interiorNavNodes.size());
      if(next<world.interiorNavNodes.size()&&seen.insert(next).second)queue.push(next);}}
    require(seen.size()==world.interiorNavNodes.size(),("Disconnected landscape seed "+std::to_string(seed)).c_str());
    for(auto e:world.interiorNavLinks){auto a=world.interiorNavNodes[e[0]],b=world.interiorNavNodes[e[1]];int steps=std::max(1,int(std::ceil(length(a-b)/5)));
      for(int i=0;i<=steps;++i){auto p=a+(b-a)*(float(i)/steps);p.y=world.surfaceHeight(p.x,p.z);++routeSamples;
        if(!world.groundPassable(p,3.5f)){
          float maxRise=0;
          for(float dx:{-3.5f,0.f,3.5f})for(float dz:{-3.5f,0.f,3.5f})
            maxRise=std::max(maxRise,std::abs(world.surfaceHeight(p.x+dx,p.z+dz)-p.y));
          throw std::runtime_error("Unwalkable landscape seed="+std::to_string(seed)+" at="+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z)+
            " rise="+std::to_string(maxRise)+" water="+std::to_string(world.waterDepthAt(p,3.5f))+
            " edge="+std::to_string(a.x)+","+std::to_string(a.z)+" -> "+std::to_string(b.x)+","+std::to_string(b.z));
        }
        for(const auto &t:world.targets)if(t.active&&t.health>0&&t.spawnType<0&&t.center.y+t.half.y>p.y+.01f&&t.center.y-t.half.y<p.y+28){
          float dx=std::max(0.f,std::abs(p.x-t.center.x)-t.half.x),dz=std::max(0.f,std::abs(p.z-t.center.z)-t.half.z);
          // Structural corridors fit the full enemy envelope. Destructible
          // room contents must clear the actual pilot movement capsule.
          if(std::hypot(dx,dz)<(t.invulnerable?18.f:3.5f))throw std::runtime_error("Blocked mecha route seed="+std::to_string(seed)+" asset="+t.generatorAsset);}
      }}
    corpus.push_back({{"seed",seed},{"location",location},{"nodes",world.interiorNavNodes.size()},{"triangles",world.geometry().terrain.size()/3},
      {"decorations",density.decorations},{"large_structures",density.largeStructures}});
  }
  // Unobstructed large parcel: exercise both courtyard and complete bay plans,
  // including actual movement through every logical connection.
  std::set<size_t> bayCounts;
  for(uint32_t seed=0;seed<12;++seed){auto layout=recipe(seed);layout["version"]=4;
    layout["plots"]=Json::array({{{"bounds",{-100,-100,100,100}},{"buildings",0}}});
    layout["nodes"]={{-400,-300},{400,-300},{-400,300},{400,300}};
    layout["roads"]=Json::array({{{"nodes",{0,1}},{"width",32}},{{"nodes",{1,3}},{"width",32}},{{"nodes",{3,2}},{"width",32}},{{"nodes",{2,0}},{"width",32}}});
    World world;world.targets.clear();world.prototypeSeed=seed;world.maxWadeDepth=6;world.missionEntry={0,0,450};buildProceduralTerrain(world,layout);
    for(auto p:layout["nodes"])world.interiorNavNodes.push_back({p[0],0,p[1]});
    for(auto road:layout["roads"])world.interiorNavLinks.push_back(road["nodes"].get<std::array<unsigned,2>>());
    auto sites=buildDungeonSites(world,layout,3,0,0,1);require(sites.size()==1,"Clear courtyard parcel was rejected");auto &site=sites.front();bayCounts.insert(site.rooms.size());
    for(auto e:site.links){auto a=site.rooms[size_t(e[0])].position,b=site.rooms[size_t(e[1])].position;world.position={a[0],a[1],a[2]};Vec3 goal{b[0],b[1],b[2]};
      for(int tick=0;tick<180&&length(world.position-goal)>.1f;++tick)world.movePilot(world.position+unit(goal-world.position)*.5f);
      require(length(world.position-goal)<.1f,"Dungeon link has no traversable physical opening");}
  }
  require(bayCounts.count(5)&&bayCounts.count(6),"Courtyard footprint variation is missing");
  bool belowGrade=false,coexist=false;uint32_t dungeonSeed=0;
  for(uint32_t seed=0;seed<24&&!coexist;++seed){if(!generationPlan(seed,3,0,0).underground)continue;
    World world;std::vector<DungeonSite> sites;buildMissionEnvironment(world,seed,3,500,empty,0,3,0,5,-1,2,&sites);
    bool surface=false,underground=false;
    for(const auto &site:sites)if(site.underground){underground=true;
      for(const auto &room:site.rooms){Vec3 p{room.position[0],room.position[1],room.position[2]};
        require(p.y<-48&&world.interiorCeilingAt(p)<0,"Underground dungeon is not roofed below grade");
        require(world.interiorCeilingAt(p)-p.y>=32,"Underground dungeon loses mecha clearance");}
      belowGrade=true;}
    else surface=true;
    if(surface&&underground){coexist=true;dungeonSeed=seed;}
  }
  require(belowGrade&&coexist,"Surface building and underground dungeon cannot coexist");
  report["underground_and_surface_seed"]=dungeonSeed;
  report["physical_route_samples"]=routeSamples;report["extra_seed_corpus"]=corpus;report["dungeon_bay_counts"]=bayCounts;
  report["passed"]=true;report["unique_layouts"]=geometries.size();report["oblique_edges"]=diagonals;report["frontage_parcels"]=frontages;
  if(argc==2){std::ofstream out(argv[1]);out<<report.dump(2)<<'\n';}
  std::cout<<report.dump(2)<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
