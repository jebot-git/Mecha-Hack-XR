#include "rpg/world_generation.hpp"
#include "interior_wall_art.hpp"
#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include <algorithm>
#include <limits>
#include <queue>
#include <stdexcept>
namespace iron::rpg {
namespace {
void cuboid(WorldMesh &mesh,Vec3 half,Vec3 color) {
  constexpr int triangles[]={0,1,2,0,2,3,5,4,7,5,7,6,4,0,3,4,3,7,1,5,6,1,6,2,3,2,6,3,6,7,4,5,1,4,1,0};
  Vec3 points[8];
  for(int i=0;i<8;++i)points[i]={(i==1||i==2||i==5||i==6)?half.x:-half.x,
    (i==2||i==3||i==6||i==7)?half.y*2:0,i>=4?-half.z:half.z};
  for(int i:triangles)mesh.push_back({points[i],color});
}
bool passage(const World &world,Vec3 a,Vec3 b,float radius) {
  int samples=std::max(1,int(std::ceil(std::hypot(a.x-b.x,a.z-b.z)/4)));
  float floor=world.walkableHeight(a);
  for(int i=0;i<=samples;++i){auto p=a+(b-a)*(float(i)/samples);p.y=floor;
    p.y=world.walkableHeight(p,1.25f);
    if(!world.groundPassable(p,radius)||
       (!world.interiorDecks.empty()&&(std::abs(p.y-floor)>1.4f||
          world.deckContact(p+Vec3{0,.15f,0},p+Vec3{0,15.f,0})<=1.f)))return false;
    floor=p.y;
    for(const auto &t:world.targets)if(t.active&&t.health>0&&t.invulnerable&&t.spawnType<0&&
       std::abs(p.x-t.center.x)<t.half.x+radius&&std::abs(p.z-t.center.z)<t.half.z+radius&&
       p.y+15>t.center.y-t.half.y&&p.y<t.center.y+t.half.y)return false;
  }
  return world.interiorDecks.empty()||std::abs(floor-world.walkableHeight(b))<1.5f;
}
}
void addStructuralPiece(World &world,const char *id,Vec3 center,Vec3 half,Vec3 color) {
  Target target;target.origin=center-Vec3{0,half.y,0};
  try {
    applyOriginalModel(target,id);
    auto mesh=std::make_shared<WorldMesh>();
    if(!interior_art::buildWall(*mesh,id,half)) {
      *mesh=*target.mesh;
      for(auto &v:*mesh) {
        Vec3 local=v.position-target.ai.localCenter;
        v.position={local.x*half.x/std::max(.001f,target.ai.localHalf.x),
          local.y*half.y/std::max(.001f,target.ai.localHalf.y)+half.y,
          local.z*half.z/std::max(.001f,target.ai.localHalf.z)};
      }
      // Preserve the source atlas tile but repeat its trim at world scale;
      // stretching a 12m template across a 500m wall must not stretch texels.
      for(size_t i=0;i+2<mesh->size();i+=3) {
        Vec3 normal=cross((*mesh)[i+1].position-(*mesh)[i].position,(*mesh)[i+2].position-(*mesh)[i].position);
        int axis=std::abs(normal.x)>=std::abs(normal.y)&&std::abs(normal.x)>=std::abs(normal.z)?0:
                 std::abs(normal.y)>=std::abs(normal.z)?1:2;
        float repeat=half.y>4?8.f:16.f;
        for(size_t n=i;n<i+3;++n){auto &v=(*mesh)[n];if(v.textured!=2&&v.textured!=3)continue;
          if(v.textured==2){
            int column=std::clamp(int(std::floor(v.u*8)),0,7),row=std::clamp(int(std::floor(v.v*8)),0,7);
            v.oneSided=float(column+8*row);v.textured=3;
          }
          Vec3 p=v.position+target.origin;
          v.u=(axis==0?p.z:p.x)/repeat;v.v=(axis==1?p.z:p.y)/repeat;
        }
      }
    }
    target.mesh=mesh;target.wreck=mesh;
  } catch(const std::runtime_error &error) {
    if(std::string(error.what()).find("Missing original Mecha Hack mesh:")!=0)throw;
    target.mesh=std::make_shared<WorldMesh>();cuboid(*target.mesh,half,color);target.wreck=target.mesh;
  }
  target.center=center;target.half=half;target.ai.localCenter={0,half.y,0};
  target.ai.localHalf=half;target.ai.boundsReady=true;target.spawnType=-1;
  target.health=100000;target.invulnerable=true;world.targets.push_back(std::move(target));
}
float missionRouteDistance(const World &world,Vec3 p) {
  float distance=std::numeric_limits<float>::infinity();
  for(auto edge:world.interiorNavLinks) {
    Vec3 a=world.interiorNavNodes[edge[0]],b=world.interiorNavNodes[edge[1]],delta=b-a;delta.y=0;
    Vec3 offset=p-a;offset.y=0;
    float t=std::clamp(dot(offset,delta)/std::max(.001f,dot(delta,delta)),0.f,1.f);
    Vec3 nearest=a+delta*t;distance=std::min(distance,std::hypot(p.x-nearest.x,p.z-nearest.z));
  }
  return distance;
}
bool addMissionProp(World &world,const char *model,Vec3 p,float yaw,float health,bool wet,float scale,bool fitTerrain,float streetSetback) {
  if(world.targets.size()>=missionSceneryTargetLimit)return false;
  if(std::hypot(p.x-world.missionEntry.x,p.z-world.missionEntry.z)<90)return false;
  p.y=world.surfaceHeight(p.x,p.z);
  Target target;target.origin=p;target.yaw=wrap(yaw);
  uint32_t variant=world.prototypeSeed^uint32_t(world.targets.size()*0x9e3779b9u)^uint32_t(std::abs(p.x)*31+std::abs(p.z)*17);
  try{applyOriginalModel(target,originalVariant(model,variant));}catch(const std::runtime_error &error){
    if(std::string(error.what()).find("Missing original Mecha Hack mesh:")==0)return false;
    throw;
  }
  if(!std::isfinite(scale)||scale<.1f||scale>8)return false;
  const float footprint=std::hypot(target.half.x,target.half.z)*scale+3;
  const float clearance=world.indoors(p+Vec3{0,8,0})?20.f:streetSetback>=0?streetSetback:30.f;
  if((!wet&&world.waterDepthAt(p,footprint+16)>0)||
     missionRouteDistance(world,target.center)<footprint+clearance)return false;
  if(std::abs(scale-1.f)>.0001f){
    auto mesh=std::make_shared<WorldMesh>(*target.mesh);
    auto wreck=std::make_shared<WorldMesh>(*target.wreck);
    auto resize=[scale](WorldMesh &surface){
      for(auto &v:surface){v.position=v.position*scale;if(v.textured==3){v.u*=scale;v.v*=scale;}}
    };
    resize(*mesh);resize(*wreck);
    target.mesh=mesh;target.wreck=wreck;
    target.ai.localCenter=target.ai.localCenter*scale;
    target.ai.localHalf=target.ai.localHalf*scale;
    world.updateEnemyBounds(target);
  }
  if(fitTerrain){
    // Fit the lowest vertex ring to the physical ground. This supplies a
    // continuous footing without flattening large chains of nearby parcels or
    // changing a protected road/ramp after navigation has been constructed.
    float bottom=target.ai.localCenter.y-target.ai.localHalf.y,top=target.ai.localCenter.y+target.ai.localHalf.y;
    float high=p.y,low=p.y;
    for(float x:{-target.half.x,0.f,target.half.x})for(float z:{-target.half.z,0.f,target.half.z}){
      float floor=world.surfaceHeight(p.x+x,p.z+z);high=std::max(high,floor);low=std::min(low,floor);}
    for(const auto &mesh:{target.mesh,target.wreck})for(const auto &v:*mesh)if(v.position.y<=bottom+.05f){auto offset=rotate(angles(target.yaw),v.position);
      float floor=world.surfaceHeight(p.x+offset.x,p.z+offset.z);high=std::max(high,floor);low=std::min(low,floor);}
    target.origin.y=high-bottom;
    auto fit=[&](const std::shared_ptr<WorldMesh> &source){auto mesh=std::make_shared<WorldMesh>(*source);
      for(auto &v:*mesh)if(v.position.y<=bottom+.05f){auto offset=rotate(angles(target.yaw),v.position);
        v.position.y=std::min(bottom,world.surfaceHeight(p.x+offset.x,p.z+offset.z)-target.origin.y);}
      return mesh;};
    target.mesh=fit(target.mesh);target.wreck=fit(target.wreck);
    low=std::min(bottom,low-target.origin.y);target.ai.localCenter.y=(low+top)/2;target.ai.localHalf.y=(top-low)/2;
    world.updateEnemyBounds(target);
  }
  // 40m clear route plus room for a softly blended local foundation apron.
  if(!clearPosition(world,p,footprint))return false;
  if(target.center.y+target.half.y>world.interiorCeilingAt(p,std::max(target.half.x,target.half.z))-2)return false;
  bool sheltered=world.indoors(p);
  if(!fitTerrain&&(wet||sheltered)) {
    for(float x:{target.center.x-target.half.x,target.center.x+target.half.x})
      for(float z:{target.center.z-target.half.z,target.center.z+target.half.z})
        if(std::abs(world.surfaceHeight(x,z)-p.y)>(sheltered?.05f:.25f))return false;
  }
  target.spawnType=-1;target.health=health;target.invulnerable=false;target.rpgDecoration=true;
  world.targets.push_back(std::move(target));return true;
}
void buildMissionCanalEntry(World &world,int biome,int location,int region,int opener) {
  if(opener!=4||region>=2||location==3||location==6||location==7||location==9||
     (biome!=2&&biome!=3&&biome!=5))return;
  Vec3 entry=world.missionEntry;
  std::array<float,4> canal{entry.x-60,entry.z-70,entry.x+60,entry.z+70};
  constexpr float level=-1,depth=2.75f,bank=24;
  for(auto &v:world.editGeometry().terrain) {
    auto &p=v.position;
    if(p.x<canal[0]||p.x>canal[2]||p.z<canal[1]||p.z>canal[3])continue;
    float edge=std::min({p.x-canal[0],canal[2]-p.x,p.z-canal[1],canal[3]-p.z});
    float weight=std::clamp(edge/bank,0.f,1.f);
    p.y=p.y*(1-weight)+(level-depth)*weight;
  }
  // Roads crossing this entry are flooded fords. Retain their graph edges,
  // removing only dry bridge support over the stamped canal itself.
  std::vector<std::array<float,4>> crossings;
  auto keep=[&](std::array<float,4> r){if(r[2]-r[0]>.001f&&r[3]-r[1]>.001f)crossings.push_back(r);};
  for(auto r:world.waterCrossings) {
    if(r[2]<=canal[0]||r[0]>=canal[2]||r[3]<=canal[1]||r[1]>=canal[3]){keep(r);continue;}
    keep({r[0],r[1],std::min(r[2],canal[0]),r[3]});
    keep({std::max(r[0],canal[2]),r[1],r[2],r[3]});
    float left=std::max(r[0],canal[0]),right=std::min(r[2],canal[2]);
    keep({left,r[1],right,std::min(r[3],canal[1])});
    keep({left,std::max(r[1],canal[3]),right,r[3]});
  }
  world.waterCrossings=std::move(crossings);
  world.waterRegions.push_back({canal,level,depth,bank});
  Vec3 a{canal[0],level,canal[1]},b{canal[2],level,canal[1]},c{canal[2],level,canal[3]},d{canal[0],level,canal[3]};
  for(auto p:{a,c,b,a,d,c})world.editGeometry().waterMesh.push_back({p,{.035f,.24f,.42f}});
  for(auto &patch:world.editGeometry().terrainPatches) {
    patch.low.y=1e9f;patch.high.y=-1e9f;
    for(size_t i=patch.first;i<patch.first+patch.count;++i){auto p=world.geometry().terrain[i].position;
      patch.low.y=std::min(patch.low.y,p.y);patch.high.y=std::max(patch.high.y,p.y);}
  }
  world.indexTerrain();world.missionEntry.y=world.surfaceHeight(entry.x,entry.z);
}
Vec3 missionEntryPosition(const World &world,unsigned slot) {
  Vec3 p=world.hasMissionEntry?world.missionEntry:Vec3{0,0,world.movementBounds[3]*.65f};
  p.x+=(float(slot)-1.5f)*12;p.y=world.surfaceHeight(p.x,p.z);
  return missionNavigablePosition(world,p,3.5f);
}
Vec3 missionNavigablePosition(const World &world,Vec3 desired,float radius,int ignore,float maximumDepth) {
  auto valid=[&](Vec3 p){return clearPosition(world,p,radius,ignore)&&
    (maximumDepth<0||world.waterDepthAt(p,radius)<=maximumDepth+.001f);};
  desired.y=world.walkableHeight(desired);
  if(valid(desired))return desired;
  for(int ring=1;ring<=20;++ring)for(int i=0;i<16;++i) {
    float angle=i*2*pi/16;auto p=desired+Vec3{std::sin(angle)*ring*8,0,std::cos(angle)*ring*8};
    p.y=world.walkableHeight(p);if(valid(p))return p;
  }
  std::vector<Vec3> nodes=world.interiorNavNodes;
  std::stable_sort(nodes.begin(),nodes.end(),[&](auto a,auto b){return length(a-desired)<length(b-desired);});
  for(auto p:nodes){p.y=world.walkableHeight(p);if(valid(p))return p;}
  throw std::runtime_error("No connected mission floor position available");
}
namespace {
constexpr float escortRadius=5.8f;
constexpr float escortSpacing=16.f;

float horizontalRange(Vec3 a,Vec3 b) { return std::hypot(a.x-b.x,a.z-b.z); }

bool escortLeg(const World &world,Vec3 a,Vec3 b,size_t enemyCount) {
  const int steps=std::max(1,int(std::ceil(horizontalRange(a,b)/3.5f)));
  float floor=world.walkableHeight(a,1.25f);
  for(int i=0;i<=steps;++i) {
    auto p=a+(b-a)*(float(i)/steps);
    p.y=floor;
    p.y=world.walkableHeight(p,1.25f);
    if(std::abs(p.y-floor)>2.05f||!world.groundPassable(p,escortRadius)||
       world.deckContact(p+Vec3{0,.18f,0},p+Vec3{0,5.5f,0})<=1.f)return false;
    for(size_t j=enemyCount;j<world.targets.size();++j) {
      const auto &t=world.targets[j];
      if(t.active&&t.health>0&&!t.protectedTarget&&t.spawnType<0&&
         std::abs(p.x-t.center.x)<t.half.x+escortRadius&&
         std::abs(p.z-t.center.z)<t.half.z+escortRadius&&
         p.y+5.5f>t.center.y-t.half.y&&p.y<t.center.y+t.half.y)return false;
    }
    floor=p.y;
  }
  return std::abs(floor-world.walkableHeight(b,1.25f))<1.5f;
}

std::vector<Vec3> escortPath(const World &world,Vec3 from,Vec3 exit,size_t enemyCount,
                             bool connectEndpoints) {
  std::vector<Vec3> nodes=world.interiorNavNodes;
  using Edge=std::pair<float,size_t>;
  std::vector<std::array<size_t,2>> lanes;
  for(const auto &edge:world.interiorNavLinks) {
    const size_t a=edge[0],b=edge[1];
    const Vec3 origin=nodes[a],delta=nodes[b]-origin;
    const float square=delta.x*delta.x+delta.z*delta.z;
    if(square<.01f)continue;
    std::vector<Edge> split{{0,a},{1,b}};
    for(const auto &anchor:{from,exit}) {
      const auto offset=anchor-origin;
      const float t=(offset.x*delta.x+offset.z*delta.z)/square;
      auto join=origin+delta*t;
      if(t<=.01f||t>=.99f||horizontalRange(join,anchor)>48.f||
         std::any_of(split.begin(),split.end(),[&](const Edge &e){return std::abs(e.first-t)<.01f;}))
        continue;
      join.y=world.walkableHeight(join,1.25f);
      split.push_back({t,nodes.size()});
      nodes.push_back(join);
    }
    std::sort(split.begin(),split.end());
    for(size_t i=1;i<split.size();++i)
      lanes.push_back({split[i-1].second,split[i].second});
  }
  const auto roadNodes=nodes.size();
  const size_t start=nodes.size(),finish=start+1;
  nodes.push_back(from);
  nodes.push_back(exit);
  std::vector<std::vector<Edge>> links(nodes.size());
  for(const auto &edge:lanes) {
    const auto a=edge[0],b=edge[1];
    const float distance=horizontalRange(nodes[a],nodes[b]);
    if(distance>.01f&&escortLeg(world,nodes[a],nodes[b],enemyCount)) {
      // Roads, deck corridors and freight tracks already share this graph.
      links[a].push_back({distance*.48f,b});
      links[b].push_back({distance*.48f,a});
    }
  }
  const float straight=horizontalRange(from,exit);
  for(size_t i=0;i<roadNodes;++i) {
    if(!connectEndpoints&&links[i].empty())continue;
    const float entryDistance=horizontalRange(nodes[i],from);
    const float exitDistance=horizontalRange(nodes[i],exit);
    const float approach=std::min(200.f,straight*.45f);
    if(entryDistance<approach&&(!connectEndpoints||escortLeg(world,from,nodes[i],enemyCount)))
      links[start].push_back({entryDistance*1.4f,i});
    if(exitDistance<approach&&(!connectEndpoints||escortLeg(world,nodes[i],exit,enemyCount)))
      links[i].push_back({exitDistance*1.4f,finish});
  }
  if(connectEndpoints&&escortLeg(world,from,exit,enemyCount))
    links[start].push_back({straight,finish});
  std::vector<float> distance(nodes.size(),std::numeric_limits<float>::infinity());
  std::vector<size_t> previous(nodes.size(),nodes.size());
  std::priority_queue<Edge,std::vector<Edge>,std::greater<Edge>> queue;
  queue.push({0,start});distance[start]=0;
  while(!queue.empty()) {
    auto [cost,here]=queue.top();queue.pop();
    if(here==finish)break;
    if(cost>distance[here])continue;
    for(auto [leg,next]:links[here])if(cost+leg<distance[next]) {
      distance[next]=cost+leg;previous[next]=here;
      queue.push({distance[next],next});
    }
  }
  if(previous[finish]==nodes.size())return {};
  std::vector<Vec3> path;
  for(size_t n=finish;n!=start;n=previous[n])
    if(connectEndpoints||n!=finish)path.push_back(nodes[n]);
  if(connectEndpoints)path.push_back(from);
  std::reverse(path.begin(),path.end());
  return path;
}
}

std::vector<Vec3> missionEscortRoute(const World &world,Vec3 from,Vec3 exit,size_t enemyCount) {
  from.y=world.walkableHeight(from,1.25f);
  auto path=escortPath(world,from,exit,enemyCount,true);
  // If loose cover blocks a lay-by, place the formation and extraction on
  // the nearest usable stretch of the connected lane itself.
  if(path.empty())path=escortPath(world,from,exit,enemyCount,false);
  if(path.size()<2)throw std::runtime_error("No continuous convoy route to extraction");
  std::vector<Vec3> rounded{path.front()};
  for(size_t i=1;i+1<path.size();++i) {
    const Vec3 corner=path[i];
    const float approach=horizontalRange(path[i-1],corner),departure=horizontalRange(path[i+1],corner);
    const float cut=std::min({6.f,approach*.27f,departure*.27f});
    if(cut<1.f){rounded.push_back(corner);continue;}
    const Vec3 enter=corner+(path[i-1]-corner)*(cut/approach);
    const Vec3 leave=corner+(path[i+1]-corner)*(cut/departure);
    std::array<Vec3,5> arc;
    for(size_t j=0;j<arc.size();++j) {
      const float t=float(j)/(arc.size()-1),u=1-t;
      arc[j]=enter*(u*u)+corner*(2*u*t)+leave*(t*t);
    }
    bool fit=true;
    for(size_t j=1;j<arc.size()&&fit;++j)fit=escortLeg(world,arc[j-1],arc[j],enemyCount);
    if(fit)rounded.insert(rounded.end(),arc.begin(),arc.end());
    else rounded.push_back(corner);
  }
  rounded.push_back(path.back());
  path=std::move(rounded);
  std::vector<Vec3> sampled{path.front()};
  for(size_t k=1;k<path.size();++k) {
    Vec3 a=path[k-1],b=path[k];
    const float length=horizontalRange(a,b);
    if(length<.01f)continue;
    const int steps=int(std::ceil(length/3.5f));
    for(int i=1;i<=steps;++i) {
      auto p=a+(b-a)*(float(i)/steps);
      p.y=sampled.back().y;
      p.y=world.walkableHeight(p,1.25f);
      sampled.push_back(p);
    }
  }
  // Road junctions and off-road connectors can briefly double back. Trim
  // a short reversal only when the actual vehicle clears the shortcut.
  std::vector<Vec3> route{sampled.front()};
  for(size_t i=0;i+1<sampled.size();) {
    size_t next=i+1;
    float along=0;
    for(size_t j=i+1;j<sampled.size();++j) {
      along+=horizontalRange(sampled[j-1],sampled[j]);
      if(along>82.f)break;
      const float chord=horizontalRange(sampled[i],sampled[j]);
      if(along>=escortSpacing-1&&chord<11.f&&chord<along*.63f&&
         std::abs(sampled[i].y-sampled[j].y)<1.4f&&
         escortLeg(world,sampled[i],sampled[j],enemyCount))next=j;
    }
    if(horizontalRange(route.back(),sampled[next])<.015f)route.back()=sampled[next];
    else route.push_back(sampled[next]);
    i=next;
  }
  return route;
}

Vec3 Session::EscortRoute::at(float travelled) const {
  if(travelled<=0)return points.front();
  if(travelled>=distance.back())return points.back();
  const size_t next=size_t(std::upper_bound(distance.begin(),distance.end(),travelled)-distance.begin());
  const float ratio=(travelled-distance[next-1])/(distance[next]-distance[next-1]);
  return points[next-1]+(points[next]-points[next-1])*ratio;
}

void Session::prepareEscort(Vec3 from,Vec3 exit) {
  escort.points=missionEscortRoute(world,from,exit,mission.recipe.enemies.size());
  escort.distance.push_back(0);
  for(size_t i=1;i<escort.points.size();++i)
    escort.distance.push_back(escort.distance.back()+horizontalRange(escort.points[i-1],escort.points[i]));
  const float span=escortSpacing*float(mission.assets.size()-1);
  if(escort.distance.back()<span+8)throw std::runtime_error("Convoy route too short for its formation");
  const auto arrival=escort.points.back();
  mission.exit={arrival.x,arrival.y,arrival.z};
  positionEscort(0);
}

void Session::positionEscort(float dt) {
  for(size_t i=0;i<mission.assets.size();++i) {
    auto &vehicle=world.targets[size_t(mission.assets[i])];
    const float along=escort.covered+escortSpacing*float(mission.assets.size()-1-i);
    const auto previous=vehicle.origin;
    const auto heading=escort.at(along+3)-escort.at(along-3);
    vehicle.origin=escort.at(along);
    vehicle.origin.y=world.walkableHeight(vehicle.origin);
    vehicle.yaw=wrap(std::atan2(-heading.x,-heading.z));
    vehicle.ai.velocity=dt>0?(vehicle.origin-previous)*(1.f/dt):Vec3{};
    world.updateEnemyBounds(vehicle);
  }
}

bool Session::stepEscort(float dt,bool accompanied) {
  const float span=escortSpacing*float(mission.assets.size()-1);
  const float travel=escort.distance.back()-span;
  float desired=0;
  if(accompanied) {
    float turn=1;
    for(size_t i=0;i<mission.assets.size();++i) {
      const float along=escort.covered+escortSpacing*float(i);
      const auto p=escort.at(along);
      const float alignment=dot(unit(p-escort.at(along-7)),unit(escort.at(along+7)-p));
      turn=std::min(turn,std::clamp(.72f+.28f*alignment,.52f,1.f));
    }
    desired=std::min(11.f*turn,3.f+travel-escort.covered);
  }
  escort.speed=accompanied?std::clamp(desired,escort.speed-7*dt,escort.speed+4.5f*dt):0;
  escort.covered=std::min(travel,escort.covered+escort.speed*dt);
  positionEscort(dt);
  mission.progress=escort.covered/travel;
  if(escort.covered<travel)return false;
  for(int index:mission.assets)world.targets[size_t(index)].ai.velocity={};
  return true;
}

Vec3 missionNavigationWaypoint(const World &world,Vec3 from,Vec3 destination,float radius) {
  destination.y=world.walkableHeight(destination);
  if(passage(world,from,destination,radius)||world.interiorNavNodes.empty())return destination;
  auto &nodes=world.interiorNavNodes;size_t start=nodes.size(),finish=nodes.size();float a=1e20f,b=1e20f;
  for(size_t i=0;i<nodes.size();++i) {
    float da=length(nodes[i]-from),db=length(nodes[i]-destination);
    if(da<a&&passage(world,from,nodes[i],radius)){start=i;a=da;}
    if(db<b&&passage(world,nodes[i],destination,radius)){finish=i;b=db;}
  }
  if(start==nodes.size()||finish==nodes.size())return destination;
  std::vector<int> previous(nodes.size(),-1);std::queue<size_t> queue;queue.push(start);previous[start]=int(start);
  while(!queue.empty()&&previous[finish]<0){auto here=queue.front();queue.pop();for(auto edge:world.interiorNavLinks){
    size_t next=edge[0]==here?edge[1]:edge[1]==here?edge[0]:nodes.size();
    if(next<nodes.size()&&previous[next]<0){previous[next]=int(here);queue.push(next);}
  }}
  if(previous[finish]<0)return destination;
  size_t next=finish;while(size_t(previous[next])!=start&&next!=start)next=size_t(previous[next]);
  return length(nodes[start]-from)>8?nodes[start]:nodes[next];
}
}
