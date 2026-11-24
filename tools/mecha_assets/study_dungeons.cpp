// Headless structural art review: sample generated maps and write native
// triangle streams for the fixed-camera Blender comparison.
#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include "../../src/rpg/interior_wall_art.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

using namespace iron;
using namespace iron::rpg;
namespace {
struct Sample { int seed, biome, location, region, extent; };

void validate(const WorldMesh &mesh,Vec3 half) {
  if(mesh.empty() || mesh.size()%3 || mesh.size()>204*3)
    throw std::runtime_error("Wall exceeded its prototype geometry budget");
  for(size_t i=0;i<mesh.size();i+=3) {
    const auto &p=mesh[i].position;
    const auto normal=cross(mesh[i+1].position-p,mesh[i+2].position-p);
    const float area=length(normal);
    if(!std::isfinite(area) || area<.000001f) throw std::runtime_error("Degenerate wall triangle");
    const float frontNormal=half.x>=half.z ? normal.z : -normal.x;
    const float signedDepth=half.x>=half.z ? p.z : -p.x;
    if(std::abs(frontNormal)>.99999f*area && frontNormal*signedDepth<=0)
      throw std::runtime_error("Inverted wall elevation");
  }
  for(const auto &v:mesh) {
    const auto &p=v.position;
    if(!std::isfinite(p.x+p.y+p.z+v.u+v.v) || std::abs(p.x)>half.x+.001f ||
       std::abs(p.z)>half.z+.001f || p.y<-.001f || p.y>2*half.y+.001f ||
       v.textured!=3 || v.oneSided<0 || v.oneSided>=64)
      throw std::runtime_error("Invalid structural bounds or atlas coordinates");
  }
}

Json measure(Sample sample) {
  World map;
  std::vector<DungeonSite> sites;
  buildMissionEnvironment(map,sample.seed,sample.biome,sample.extent,{},sample.location,
                          0,sample.region,5,-1,2,&sites);
  std::vector<std::pair<std::string,Vec3>> pieces;
  size_t previous=0,current=0,bytes=0;
  float longest=0;
  for(const auto &t:map.targets) {
    if(!t.invulnerable) continue;
    WorldMesh generated;
    if(!interior_art::buildWall(generated,t.generatorAsset,t.half)) continue;
    validate(generated,t.half);
    // Verify the normal mission assembly actually used the fitted mesh.
    if(!t.mesh || t.mesh->size()!=generated.size()) throw std::runtime_error("Missing fitted wall in mission");
    for(size_t i=0;i<generated.size();++i)
      if(length((*t.mesh)[i].position-generated[i].position)>.001f) throw std::runtime_error("Fitted mesh differs in mission");
    pieces.emplace_back(t.generatorAsset,t.half);
    longest=std::max(longest,2*std::max(t.half.x,t.half.z));
    previous+=originalMesh(t.generatorAsset)->size()/3;
    current+=generated.size()/3;
    bytes+=t.mesh->capacity()*sizeof(WorldVertex);
  }
  if(pieces.empty()) throw std::runtime_error("Sample did not exercise a fitted dungeon wall");
  const auto start=std::chrono::steady_clock::now();
  for(int repeat=0;repeat<20;++repeat) for(const auto &[id,half]:pieces) {
    WorldMesh mesh;
    if(!interior_art::buildWall(mesh,id,half)) throw std::runtime_error("Nondeterministic wall fit");
  }
  const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count()/20;
  return {{"seed",sample.seed},{"biome",sample.biome},{"location",sample.location},{"region",sample.region},
          {"extent_m",sample.extent},{"revised_pieces",pieces.size()},{"longest_span_m",longest},
          {"baseline_triangles",previous},{"prototype_triangles",current},{"cpu_vertex_bytes",bytes},
          {"mean_wall_assembly_us",us}};
}

void dump(std::ostream &out) {
  for(auto name:{"hangar_wall","ship_bulkhead","cover_conduit_wall","tunnel_wall","door_header"})
    for(Vec3 half:{Vec3{24,20,1},Vec3{1,20,48},Vec3{20,4,1},Vec3{5,20,1},Vec3{250,40,2}}) {
      WorldMesh mesh;
      if(!interior_art::buildWall(mesh,name,half)) throw std::runtime_error("Missing sample");
      validate(mesh,half);
      out<<"m "<<name<<' '<<half.x<<' '<<half.y<<' '<<half.z<<' '<<mesh.size()/3<<'\n';
      for(const auto &v:mesh) {
        const auto p=v.position,c=v.color;
        out<<p.x<<' '<<p.y<<' '<<p.z<<' '<<c.x<<' '<<c.y<<' '<<c.z<<' '
           <<v.u<<' '<<v.v<<' '<<int(v.oneSided)<<'\n';
      }
    }
}
}

int main(int argc,char **argv) {
  try {
    if(argc!=4) throw std::runtime_error("Usage: mecha-dungeon-art-study ASSETS dump|audit OUTPUT");
    setOriginalAssetRoot(argv[1]);
    std::ofstream out(argv[3]);
    if(!out) throw std::runtime_error("Could not open output");
    if(std::string_view(argv[2])=="dump") dump(out);
    else if(std::string_view(argv[2])=="audit") {
      Json records=Json::array();
      for(Sample sample:std::array{Sample{4,5,1,3,1000},Sample{5,3,7,2,1000},Sample{4,3,0,0,750},Sample{7,3,4,0,500}})
        records.push_back(measure(sample));
      out<<Json{{"samples",records},{"timing","Mean of 20 CPU wall mesh assembly passes, excluding map generation and GPU work."}}.dump(2)<<'\n';
    } else throw std::runtime_error("Unknown study mode");
    if(!out) throw std::runtime_error("Could not finish output");
    return 0;
  } catch(const std::exception &e) {
    std::cerr<<e.what()<<'\n';
    return 1;
  }
}
