#include "rpg/story_world.hpp"
#include "rpg/world_generation.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace iron::rpg {
namespace {
struct Profile {
  const char *name,*landmark,*fallback,*trim;
  Vec3 color;
  int bays,barriers;
  bool raised,roofed;
};
constexpr Profile profiles[]={
  {"FREIGHT REPAIR YARD","story_manifest_relay","is_relay","cargo_container",{.28f,.31f,.22f},3,6,false,false},
  {"SHIELD OATH COURT","story_oath_beacon","beacon","hangar_wall",{.31f,.27f,.20f},4,8,true,false},
  {"DISCONNECTED ARCHIVE","story_aeon_archive","service_console","ship_bulkhead",{.18f,.28f,.34f},3,5,true,false},
  {"QUARANTINE BREACH LINE","story_ice_cradle","ship_reactor","tunnel_wall",{.27f,.30f,.29f},2,10,false,false},
  {"DOME AIR RESERVE","story_air_plant","is_water_tower","hangar_wall",{.30f,.29f,.21f},4,6,false,false},
  {"MECHA BOARDING GALLERY","story_docking_clamp","dock_crane","ship_bulkhead",{.22f,.27f,.31f},3,8,false,true},
  {"BROADCAST ARENA","story_broadcast_pylon","is_relay","hangar_wall",{.34f,.25f,.20f},6,12,false,false},
  {"RIFT COMPARISON LAB","story_rift_anchor","service_console","ship_bulkhead",{.20f,.29f,.31f},3,7,true,true}
};
float planar(Vec3 p){return std::hypot(p.x,p.z);}
float segmentDistance(Vec3 p,Vec3 a,Vec3 b){
  auto d=b-a;d.y=0;auto q=p-a;q.y=0;
  float t=std::clamp(dot(q,d)/std::max(.001f,dot(d,d)),0.f,1.f);
  return planar(q-d*t);
}
bool terrainNode(const World &world, Vec3 node) {
  return std::abs(node.y-world.surfaceHeight(node.x,node.z)) < 1.f;
}
bool safePoint(const World &w,Vec3 p,float radius,float extent){
  return insideArea(p,extent*.94f-radius)&&planar(p-w.missionEntry)>110+radius&&
         planar(p-Vec3{0,0,-extent*.65f})>80+radius;
}
bool passage(const World &w,Vec3 a,Vec3 b,float radius=18){
  const int samples=std::max(1,int(std::ceil(planar(b-a)/4)));
  float previous=w.surfaceHeight(a.x,a.z);
  for(int n=0;n<=samples;++n){auto p=a+(b-a)*(float(n)/samples);p.y=w.surfaceHeight(p.x,p.z);
    if(!w.groundPassable(p,3.5f)||w.waterDepthAt(p,3.5f)>w.waterTraversalLimitAt(p,3.5f)+.001f||
       p.y+32>w.interiorCeilingAt(p,radius)||std::abs(p.y-previous)>.6f+.6f*planar(b-a)/samples)return false;
    previous=p.y;
    for(const auto &t:w.targets)if(t.active&&t.health>0&&
      std::abs(p.x-t.center.x)<t.half.x+radius&&std::abs(p.z-t.center.z)<t.half.z+radius&&
      p.y+28>t.center.y-t.half.y&&p.y<t.center.y+t.half.y)return false;
  }
  return true;
}
unsigned node(World &w,Vec3 p){p.y=w.surfaceHeight(p.x,p.z);w.interiorNavNodes.push_back(p);return unsigned(w.interiorNavNodes.size()-1);}
void connect(World &w,unsigned a,unsigned b){
  if(a==b)return;
  for(auto e:w.interiorNavLinks)if((e[0]==a&&e[1]==b)||(e[0]==b&&e[1]==a))return;
  w.interiorNavLinks.push_back({a,b});
}
bool clearSolid(const World &w,Vec3 center,Vec3 half,float extent){
  if(!safePoint(w,center,std::hypot(half.x,half.z),extent))return false;
  for(auto e:w.interiorNavLinks){auto a=w.interiorNavNodes[e[0]],b=w.interiorNavNodes[e[1]];
    if(segmentDistance(center,a,b)<std::hypot(half.x,half.z)+22&&center.y-half.y<std::max(a.y,b.y)+42)return false;}
  for(const auto &t:w.targets)if(t.active&&t.health>0&&
    std::abs(center.x-t.center.x)<half.x+t.half.x+4&&std::abs(center.z-t.center.z)<half.z+t.half.z+4&&
    center.y+half.y>t.center.y-t.half.y&&center.y-half.y<t.center.y+t.half.y)return false;
  return true;
}
// The generic prop helper permits a14m indoor skirt. Story routes reserve
// a full18m mech radius; sample actual height along every affected edge so
// a curved highland route is also protected below an overhead piece.
bool finalRouteClearance(const World &w,const Target &t){
  for(auto e:w.interiorNavLinks){auto a=w.interiorNavNodes[e[0]],b=w.interiorNavNodes[e[1]];
    if(segmentDistance(t.center,a,b)>std::hypot(t.half.x,t.half.z)+24)continue;
    const int steps=std::max(1,int(std::ceil(planar(b-a)/4)));
    for(int n=0;n<=steps;++n){auto p=a+(b-a)*(float(n)/steps);p.y=w.walkableHeight(p);
      if(p.y+32<=t.center.y-t.half.y||p.y>=t.center.y+t.half.y)continue;
      float dx=std::max(0.f,std::abs(p.x-t.center.x)-t.half.x),dz=std::max(0.f,std::abs(p.z-t.center.z)-t.half.z);
      if(std::hypot(dx,dz)<22)return false;
    }
  }
  return true;
}
bool prop(World &w,const char *model,Vec3 p,float yaw,float health,bool wet,float scale=1){
  if(!addMissionProp(w,model,p,yaw,health,wet,scale,true))return false;
  if(finalRouteClearance(w,w.targets.back()))return true;
  w.targets.pop_back();return false;
}
bool piece(World &w,const char *model,Vec3 p,Vec3 half,Vec3 color,float extent,size_t cap){
  if(w.targets.size()>=cap)return false;
  if(!clearSolid(w,p,half,extent))return false;
  addStructuralPiece(w,model,p,half,color);
  // Scene additions are breakable local cover, not mandatory objective actors.
  auto &t=w.targets.back();t.rpgDecoration=true;t.invulnerable=false;t.health=160;
  if(finalRouteClearance(w,t))return true;
  w.targets.pop_back();return false;
}
// A low, broad physical observation court adds useful height without creating
// an unsupported second walkable floor at the same X/Z. Old terrain is retained.
bool raisedCourt(World &w,unsigned anchor,const Profile &profile,float extent){
  const auto origin=w.interiorNavNodes[anchor];
  for(float sign:{1.f,-1.f}){
    Vec3 center=origin+Vec3{sign*90,0,0};
    if(!safePoint(w,center,112,extent))continue;
    // The six-metre terrain court must not bury a ramp foot or alter the
    // underpass clearance. Its full rectangular footprint is known up front.
    const bool overlapsDeck = std::any_of(w.interiorDecks.begin(), w.interiorDecks.end(), [&](const auto &deck) {
      return center.x+90 >= deck.bounds[0] && center.x-90 <= deck.bounds[2] &&
             center.z+55 >= deck.bounds[1] && center.z-55 <= deck.bounds[3];
    });
    if(overlapsDeck) continue;
    bool free=true;
    for(const auto &t:w.targets)if(t.active&&t.health>0&&std::abs(t.center.x-center.x)<92+t.half.x&&
      std::abs(t.center.z-center.z)<57+t.half.z){free=false;break;}
    for(float x:{-90.f,-45.f,0.f,45.f,90.f})for(float z:{-55.f,0.f,55.f}){
      auto p=center+Vec3{x,0,z};p.y=w.surfaceHeight(p.x,p.z);
      free&=w.waterDepthAt(p,18)==0&&w.groundPassable(p,18)&&w.interiorCeilingAt(p,18)>p.y+44;
    }
    if(!free)continue;
    Vec3 west=center+Vec3{-90,0,0},east=center+Vec3{90,0,0};
    int other=-1;float best=220;
    for(size_t j=0;j<w.interiorNavNodes.size();++j)if(j!=anchor){float distance=planar(w.interiorNavNodes[j]-east);
      if(distance<best&&terrainNode(w,w.interiorNavNodes[j])&&passage(w,east,w.interiorNavNodes[j])){other=int(j);best=distance;}}
    if(other<0||!passage(w,origin,west)||!passage(w,west,east))continue;
    const size_t oldTerrain=w.geometry().terrain.size(),oldPatches=w.geometry().terrainPatches.size();
    constexpr int columns=18,rows=11;std::array<std::array<Vec3,columns+1>,rows+1> grid;
    for(int iz=0;iz<=rows;++iz)for(int ix=0;ix<=columns;++ix){float x=-90+10.f*ix,z=-55+10.f*iz;
      float rise=6*std::min(std::clamp((90-std::abs(x))/60.f,0.f,1.f),std::clamp((55-std::abs(z))/32.f,0.f,1.f));
      auto p=center+Vec3{x,0,z};p.y=w.surfaceHeight(p.x,p.z)+rise;grid[iz][ix]=p;}
    Vec3 low{center.x-90,1e9f,center.z-55},high{center.x+90,-1e9f,center.z+55};
    for(int iz=0;iz<rows;++iz)for(int ix=0;ix<columns;++ix)
      for(auto p:{grid[iz][ix],grid[iz+1][ix+1],grid[iz][ix+1],grid[iz][ix],grid[iz+1][ix],grid[iz+1][ix+1]}){
        w.editGeometry().terrain.push_back({p,profile.color});low.y=std::min(low.y,p.y);high.y=std::max(high.y,p.y);}
    w.editGeometry().terrainPatches.push_back({low,high,oldTerrain,w.geometry().terrain.size()-oldTerrain});w.indexTerrain();
    if(!passage(w,origin,west)||!passage(w,west,center)||!passage(w,center,east)||!passage(w,east,w.interiorNavNodes[size_t(other)])){
      w.editGeometry().terrain.resize(oldTerrain);w.editGeometry().terrainPatches.resize(oldPatches);w.indexTerrain();continue;}
    for(auto &p:w.interiorNavNodes)
      if(std::abs(p.x-center.x)<=90 && std::abs(p.z-center.z)<=55)
        p.y=w.surfaceHeight(p.x,p.z);
    auto a=node(w,west),b=node(w,center),c=node(w,east);connect(w,anchor,a);connect(w,a,b);connect(w,b,c);connect(w,c,unsigned(other));
    return true;
  }
  return false;
}
void landmark(World &w,const Profile &profile,float extent,int wanted){
  Target example;const char *model=profile.landmark;
  try{applyOriginalModel(example,model);}catch(const std::runtime_error &e){
    if(std::string(e.what()).find("Missing original Mecha Hack mesh:")!=0)throw;
    model=profile.fallback;applyOriginalModel(example,model);
  }
  const float footprint=std::hypot(example.half.x,example.half.z);
  auto edges=w.interiorNavLinks;
  for(size_t k=0;k<edges.size()&&wanted>0;++k){auto e=edges[(k+w.prototypeSeed%std::max(size_t(1),edges.size()))%edges.size()];
    auto a=w.interiorNavNodes[e[0]],b=w.interiorNavNodes[e[1]],d=b-a;d.y=0;if(planar(d)<20)continue;
    auto side=unit(cross(d,{0,1,0}));
    for(float sign:{1.f,-1.f}){auto p=(a+b)*.5f+side*(sign*(footprint+42));
      if(!safePoint(w,p,footprint+4,extent))continue;
      if(prop(w,model,p,std::atan2(-side.x,-side.z),180,w.waterDepthAt(p)>0)){--wanted;break;}}
  }
}
}
const char *campaignStoryWorldName(int storyArc){return storyArc>=0&&storyArc<int(std::size(profiles))?profiles[storyArc].name:"PROCEDURAL CONTRACT";}
void dressCampaignStory(World &w,const MissionRecipe &r,float extent){
  if(r.storyArc<0||r.storyArc>=int(std::size(profiles))||!std::isfinite(extent)||extent<250||
     w.interiorNavNodes.empty()||w.targets.size()>missionSceneryTargetLimit-12)return;
  const auto &profile=profiles[r.storyArc];const size_t originalNodes=w.interiorNavNodes.size();
  const size_t cap=std::min(missionSceneryTargetLimit,w.targets.size()+size_t(42));
  std::vector<unsigned> anchors;
  Vec3 desired{(r.storyArc%3-1)*extent*.18f,0,-extent*.12f};
  std::vector<unsigned> order;for(size_t i=0;i<originalNodes;++i)order.push_back(unsigned(i));
  std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return planar(w.interiorNavNodes[a]-desired)<planar(w.interiorNavNodes[b]-desired);});
  const int desiredBays=profile.bays+(r.storyApproach==0?1:0);
  for(unsigned id:order){auto p=w.interiorNavNodes[id];
    if(!terrainNode(w,p)||!safePoint(w,p,24,extent)||!w.groundPassable(p,3.5f)||
       std::any_of(anchors.begin(),anchors.end(),[&](unsigned other){return planar(w.interiorNavNodes[other]-p)<65;}))continue;
    anchors.push_back(id);if(int(anchors.size())>=desiredBays)break;
  }
  // Add collision-checked service loops. Cross-links turn surviving side bays
  // into alternate approaches instead of mere disconnected decoration.
  for(size_t k=0;k<anchors.size();++k){const auto anchor=anchors[k];auto base=w.interiorNavNodes[anchor];
    float phase=float((w.prototypeSeed>>5)%8)*pi/4+float(k)*pi/3;
    for(int attempt=0;attempt<8;++attempt){float angle=phase+float(attempt)*pi/4;
      auto p=base+Vec3{std::cos(angle)*52,0,std::sin(angle)*52};
      if(!safePoint(w,p,18,extent)||!passage(w,base,p))continue;
      unsigned other=anchor;float best=180;
      for(size_t n=0;n<originalNodes;++n)if(n!=anchor){float d=planar(w.interiorNavNodes[n]-p);
        if(d<best&&terrainNode(w,w.interiorNavNodes[n])&&passage(w,p,w.interiorNavNodes[n])){other=unsigned(n);best=d;}}
      auto here=node(w,p);connect(w,anchor,here);if(other!=anchor)connect(w,here,other);break;
    }
  }
  if(profile.raised&&r.storyApproach!=2)for(unsigned id:anchors)if(raisedCourt(w,id,profile,extent))break;
  // Each profile lays out a different edge/court rhythm. Only safe pieces are
  // accepted; the existing route graph, insertion and future exit stay open.
  const int amount=profile.barriers+std::min(4,std::max(0,r.campaignStage-1)/2);
  for(int n=0;n<amount&&w.targets.size()<cap&&!anchors.empty();++n){auto p=w.interiorNavNodes[anchors[size_t(n)%anchors.size()]];
    float angle=float(n)*2*pi/std::max(1,amount)+float(r.storyArc)*pi/8;
    Vec3 offset{std::cos(angle)*(r.storyArc==6?110.f:65.f),0,std::sin(angle)*(r.storyArc==6?110.f:65.f)};
    auto center=p+offset;center.y=w.surfaceHeight(center.x,center.z);
    bool horizontal=std::abs(offset.z)>std::abs(offset.x);
    float height=r.storyApproach==0?5.f:r.storyArc==3?18.f:10.f;
    Vec3 half=horizontal?Vec3{r.storyArc==0?18.f:24.f,height*.5f,2.f}:Vec3{2.f,height*.5f,r.storyArc==0?18.f:24.f};
    piece(w,profile.trim,center+Vec3{0,height*.5f,0},half,profile.color,extent,cap);
    // Small utilities give cockpit-height scale clues around the giant assets.
    if(n%3==0&&w.targets.size()<cap){auto detail=center+unit(offset)*22;
      if(safePoint(w,detail,6,extent))prop(w,"service_console",detail,angle,40,w.waterDepthAt(detail)>0,.65f);}
  }
  if(profile.roofed)for(unsigned id:anchors){auto p=w.interiorNavNodes[id];float floor=w.surfaceHeight(p.x,p.z);
    if(w.interiorCeilingAt(p,42)<floor+54||w.targets.size()+3>cap)continue;
    // 84m open span and48m headroom fit a mecha, not an on-foot corridor.
    bool left=piece(w,profile.trim,p+Vec3{-45,24,0},{3,24,4},profile.color,extent,cap);
    bool right=piece(w,profile.trim,p+Vec3{45,24,0},{3,24,4},profile.color,extent,cap);
    if(left&&right)piece(w,"door_header",p+Vec3{0,52,0},{48,4,4},profile.color,extent,cap);
  }
  landmark(w,profile,extent,1+(r.campaignStage>=6));
}
}
