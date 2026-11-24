#include "rpg/story_world.hpp"
#include "rpg/world_generation.hpp"
#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include <iostream>
#include <algorithm>
#include <queue>
#include <set>
#include <stdexcept>
using namespace iron;using namespace iron::rpg;
namespace {
std::string context;
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
float planar(Vec3 p){return std::hypot(p.x,p.z);}
void connected(const World &w){
  std::set<unsigned> seen;std::queue<unsigned> queue;seen.insert(0);queue.push(0);
  while(!queue.empty()){auto n=queue.front();queue.pop();for(auto e:w.interiorNavLinks){unsigned next=e[0]==n?e[1]:e[1]==n?e[0]:unsigned(w.interiorNavNodes.size());
    if(next<w.interiorNavNodes.size()&&seen.insert(next).second)queue.push(next);}}
  require(seen.size()==w.interiorNavNodes.size(),"Story bays disconnected from original route graph");
}
void clearRoute(const World &w,Vec3 a,Vec3 b,size_t firstSolid=0){
  const int samples=std::max(1,int(std::ceil(planar(b-a)/3)));
  for(int n=0;n<=samples;++n){auto p=a+(b-a)*(float(n)/samples);p.y=w.surfaceHeight(p.x,p.z);
    require(w.groundPassable(p,3.5f),"Story modifier makes a mission route physically unwalkable");
    require(p.y+28<=w.interiorCeilingAt(p,18),"Story route loses full-mecha headroom");
    for(size_t k=firstSolid;k<w.targets.size();++k){const auto &t=w.targets[k];if(!t.active||t.health<=0)continue;
      if(p.y+28<=t.center.y-t.half.y||p.y>=t.center.y+t.half.y)continue;
      float dx=std::max(0.f,std::abs(p.x-t.center.x)-t.half.x),dz=std::max(0.f,std::abs(p.z-t.center.z)-t.half.z);
      if(std::hypot(dx,dz)<18)throw std::runtime_error("Story structure obstructs route "+context+" target="+std::to_string(k)+" id="+t.generatorAsset+" point="+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z)+" center="+std::to_string(t.center.x)+","+std::to_string(t.center.y)+","+std::to_string(t.center.z)+" half="+std::to_string(t.half.x)+","+std::to_string(t.half.y)+","+std::to_string(t.half.z)+" edge="+std::to_string(a.x)+","+std::to_string(a.z)+"->"+std::to_string(b.x)+","+std::to_string(b.z));}
  }
}
World flat(uint32_t seed=713){
  World w;w.targets.clear();w.editGeometry().terrain.clear();w.editGeometry().terrainPatches.clear();w.waterRegions.clear();w.interiorRegions.clear();
  w.proceduralTerrain=true;w.rpgPrototype=true;w.prototypeSeed=seed;w.movementBounds={-500,-500,500,500};
  w.missionEntry={0,0,325};w.hasMissionEntry=true;
  for(auto p:std::array<Vec3,6>{{{-500,0,-500},{500,0,500},{500,0,-500},{-500,0,-500},{-500,0,500},{500,0,500}}})w.editGeometry().terrain.push_back({p,{.2f,.2f,.2f}});
  w.editGeometry().terrainPatches.push_back({{-500,0,-500},{500,0,500},0,6});w.indexTerrain();
  w.interiorNavNodes={{0,0,325},{0,0,0},{0,0,-325},{-180,0,0},{180,0,0},{-180,0,-180},{180,0,-180},{-180,0,180},{180,0,180}};
  w.interiorNavLinks={{{0,1}},{{1,2}},{{1,3}},{{1,4}},{{3,5}},{{3,7}},{{4,6}},{{4,8}},{{5,6}},{{7,8}}};return w;
}
std::string fingerprint(const World &w){Json output=Json::array();for(const auto &t:w.targets)if(t.active&&t.health>0)output.push_back({t.generatorAsset,t.center.x,t.center.y,t.center.z,t.half.x,t.half.y,t.half.z});return output.dump();}
}
int main(){try{
  setOriginalAssetRoot(MECHA_ASSETS);
  std::set<std::string> profiles;size_t newRoutes=0,marks=0;
  for(int arc=0;arc<8;++arc){context="flat arc="+std::to_string(arc);auto a=flat(),b=a;MissionRecipe r;r.storyArc=arc;r.storyApproach=1;r.campaignStage=7;
    const auto oldLinks=a.interiorNavLinks;const auto oldNodes=a.interiorNavNodes.size();auto entry=a.missionEntry;const auto oldRegions=a.interiorRegions.size();
    dressCampaignStory(a,r,500);dressCampaignStory(b,r,500);
    require(fingerprint(a)==fingerprint(b)&&a.interiorNavLinks==b.interiorNavLinks,"Story layout differs on identical input");
    require(a.interiorNavNodes.size()>oldNodes,"Story modifier adds no connected alternative approach");
    require(a.targets.size()<=missionSceneryTargetLimit,"Story set exceeds scenery/scene budget");
    require(length(a.missionEntry-entry)==0&&a.surfaceHeight(entry.x,entry.z)==0,"Story set changed the insertion court");
    require(a.surfaceHeight(0,-325)==0,"Story set changed the next-sector exit approach");
    for(auto e:oldLinks)require(std::find(a.interiorNavLinks.begin(),a.interiorNavLinks.end(),e)!=a.interiorNavLinks.end(),"Story modifier deleted an existing route");
    connected(a);for(auto e:a.interiorNavLinks)clearRoute(a,a.interiorNavNodes[e[0]],a.interiorNavNodes[e[1]]);
    if(arc==4)require(a.interiorRegions.size()==oldRegions,"Slipstar airplant introduces a tunnel or portal room");
    if(arc==1){require(a.geometry().terrain.size()>6,"Oath court has no actual walkable elevated approach");
      auto start=a.interiorNavNodes[a.interiorNavNodes.size()-3],goal=a.interiorNavNodes[a.interiorNavNodes.size()-2];World walker=a;walker.position=start;
      for(int n=0;n<500&&length(walker.position-goal)>.6f;++n){auto delta=goal-walker.position;delta.y=0;walker.movePilot(walker.position+unit(delta)*.4f);}
      require(length(walker.position-goal)<1&&walker.position.y>4,"Native pilot did not climb the actual story court ramp");
    }
    profiles.insert(fingerprint(a));newRoutes+=a.interiorNavLinks.size()-oldLinks.size();marks+=a.targets.size();
  }
  require(profiles.size()==8&&newRoutes>20&&marks>30,"Story sets repeat one decorative arrangement");
  // Full generated maps retain canonical enemy-prefix and reserved tail slots.
  // Parent calls the hook before tail creation; here remove/reappend those
  // placeholders to exercise the identical ordering contract independently.
  size_t generated=0;
  for(uint32_t seed:{17u,713u})for(int arc=0;arc<8;++arc){
    context="generated seed="+std::to_string(seed)+" arc="+std::to_string(arc);
    World w;const int biome=arc==4?7:arc==5?1:3,location=arc==4?6:arc==5?7:arc==6?2:0;
    std::array<int,2> enemies{3,5};buildMissionEnvironment(w,seed,biome,500,enemies,location,0,0,5);
    const auto first=w.targets[0],second=w.targets[1];
    std::vector<Target> tail(w.targets.end()-8,w.targets.end());w.targets.resize(w.targets.size()-8);
    const auto targets=w.targets.size(),nodes=w.interiorNavNodes.size(),links=w.interiorNavLinks.size();
    MissionRecipe r;r.storyArc=arc;r.storyApproach=int(seed%3);r.campaignStage=6;
    dressCampaignStory(w,r,500);
    require(w.targets[0].generatorAsset==first.generatorAsset&&w.targets[1].generatorAsset==second.generatorAsset&&length(w.targets[0].origin-first.origin)==0&&length(w.targets[1].origin-second.origin)==0,"Story dressing changed canonical enemy prefix or spawn poses");
    require(w.targets.size()<=missionSceneryTargetLimit&&w.interiorNavNodes.size()>=nodes,"Generated story set violates capacity or erases graph");
    for(size_t n=0;n<links;++n){auto e=w.interiorNavLinks[n];clearRoute(w,w.interiorNavNodes[e[0]],w.interiorNavNodes[e[1]],targets);}
    for(size_t n=links;n<w.interiorNavLinks.size();++n){auto e=w.interiorNavLinks[n];clearRoute(w,w.interiorNavNodes[e[0]],w.interiorNavNodes[e[1]]);}
    for(auto &t:tail)w.targets.push_back(std::move(t));require(w.targets.size()<=maxEncounterTargets,"Story scene exceeds runtime target capacity");
    for(size_t n=w.targets.size()-8;n<w.targets.size();++n)require(!w.targets[n].active&&w.targets[n].health==0,"Story modifier consumed a reserved deployable slot");
    ++generated;
  }
  // The story court regrades its own ground nodes after stamping. An existing
  // node on a distant deck must retain its floor even when it shares X/Z with
  // a ground-level node.
  {
    auto world=flat();
    const Vec3 upper{180,38,180};
    const auto index=world.interiorNavNodes.size();
    world.interiorDecks.push_back({{160,160,200,200},upper,upper});
    world.interiorNavNodes.push_back(upper);
    MissionRecipe recipe;
    recipe.storyArc=1;
    recipe.storyApproach=0;
    dressCampaignStory(world,recipe,500.f);
    require(world.geometry().terrain.size()>6,"Story court fixture did not regrade the ground");
    require(length(world.interiorNavNodes[index]-upper)<.001f,"Story court moved elevated navigation onto the ground");
  }
  auto untouched=flat();MissionRecipe noStory;const auto old=fingerprint(untouched);auto links=untouched.interiorNavLinks;dressCampaignStory(untouched,noStory,500);
  require(fingerprint(untouched)==old&&untouched.interiorNavLinks==links,"Legacy procedural mission received a story layout");
  std::cout<<"Eight distinct story sets, native raised-court traversal and "<<generated<<" generated-map clearance/slot cases verified\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
