#include "terrain_art.hpp"
#include "rpg/world_generation.hpp"
#include "rpg/generation.hpp"
#include "rpg/session.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
namespace iron::rpg {
namespace {
constexpr float roomClearance=48,doorClearance=32,doorHalf=24,wallThickness=2;
float floorMinimum(const World &world,std::array<float,4> box){
  float value=std::numeric_limits<float>::infinity();
  for(float x:{box[0],(box[0]+box[2])/2,box[2]})for(float z:{box[1],(box[1]+box[3])/2,box[3]})
    value=std::min(value,world.surfaceHeight(x,z));
  return value;
}
float floorMaximum(const World &world,std::array<float,4> box){
  float value=-std::numeric_limits<float>::infinity();
  for(float x:{box[0],(box[0]+box[2])/2,box[2]})for(float z:{box[1],(box[1]+box[3])/2,box[3]})
    value=std::max(value,world.surfaceHeight(x,z));
  return value;
}
unsigned site(World &world,Vec3 p){p.y=world.surfaceHeight(p.x,p.z);world.interiorNavNodes.push_back(p);return unsigned(world.interiorNavNodes.size()-1);}
void connect(World &world,unsigned a,unsigned b){if(a!=b)world.interiorNavLinks.push_back({a,b});}
void wall(World &world,const char *model,bool horizontal,float boundary,float a,float b,float top,
          std::vector<std::array<float,2>> gaps={}){
  if(b<=a)return;
  float low=horizontal?floorMinimum(world,{a,boundary,b,boundary}):floorMinimum(world,{boundary,a,boundary,b});
  low-=4;
  std::sort(gaps.begin(),gaps.end(),[](auto x,auto y){return x[0]<y[0];});
  std::vector<std::array<float,2>> merged;
  for(auto gap:gaps){gap[0]=std::max(a,gap[0]);gap[1]=std::min(b,gap[1]);if(gap[1]<=gap[0])continue;
    if(!merged.empty()&&gap[0]<=merged.back()[1]+2)merged.back()[1]=std::max(merged.back()[1],gap[1]);else merged.push_back(gap);}
  auto segment=[&](float first,float last){if(last-first<.5f)return;
    Vec3 p=horizontal?Vec3{(first+last)/2,(low+top)/2,boundary}:Vec3{boundary,(low+top)/2,(first+last)/2};
    Vec3 half=horizontal?Vec3{(last-first)/2,(top-low)/2,wallThickness}:Vec3{wallThickness,(top-low)/2,(last-first)/2};
    addStructuralPiece(world,model,p,half);
  };
  float cursor=a;
  for(auto gap:merged){segment(cursor,gap[0]);cursor=gap[1];
    float middle=(gap[0]+gap[1])/2;
    float floor=horizontal?world.surfaceHeight(middle,boundary):world.surfaceHeight(boundary,middle);
    // A tall mech overlaps the lintel before its feet reach the wall.
    // Fit the opening to the highest floor across that approach envelope.
    std::array<float,4> threshold=horizontal?std::array<float,4>{gap[0],boundary-20,gap[1],boundary+20}:
                                                                std::array<float,4>{boundary-20,gap[0],boundary+20,gap[1]};
    float bottom=floorMaximum(world,threshold)+doorClearance;
    if(top>bottom+.1f){Vec3 p=horizontal?Vec3{middle,(bottom+top)/2,boundary}:Vec3{boundary,(bottom+top)/2,middle};
      Vec3 half=horizontal?Vec3{(gap[1]-gap[0])/2,(top-bottom)/2,wallThickness}:Vec3{wallThickness,(top-bottom)/2,(gap[1]-gap[0])/2};
      addStructuralPiece(world,"door_header",p,half);
    }
    std::array<float,4> strip=horizontal?std::array<float,4>{gap[0],boundary-5,gap[1],boundary+5}:
                                                          std::array<float,4>{boundary-5,gap[0],boundary+5,gap[1]};
    world.interiorRegions.push_back({strip,floor,bottom});
  }
  segment(cursor,b);
}
bool openRoute(const World &world,Vec3 a,Vec3 b,float radius=18){
  int steps=std::max(1,int(std::ceil(std::hypot(a.x-b.x,a.z-b.z)/5)));
  for(int i=0;i<=steps;++i){Vec3 p=a+(b-a)*(float(i)/steps);p.y=world.surfaceHeight(p.x,p.z);
    if(!world.groundPassable(p,radius))return false;
    for(const auto &t:world.targets)if(t.active&&t.health>0&&t.invulnerable&&t.spawnType<0&&
      std::abs(p.x-t.center.x)<t.half.x+radius&&std::abs(p.z-t.center.z)<t.half.z+radius&&
      p.y+28>t.center.y-t.half.y&&p.y<t.center.y+t.half.y)return false;
  }
  return true;
}
bool newCoverClearsRoutes(const World &world,size_t firstSolid,size_t firstRegion) {
  for(auto edge:world.interiorNavLinks){
    const auto a=world.interiorNavNodes[edge[0]],b=world.interiorNavNodes[edge[1]];
    const int samples=std::max(1,int(std::ceil(std::hypot(b.x-a.x,b.z-a.z)/4)));
    for(int i=0;i<=samples;++i){
      auto p=a+(b-a)*(float(i)/samples);
      p.y=world.surfaceHeight(p.x,p.z);
      for(size_t n=firstSolid;n<world.targets.size();++n){
        const auto &target=world.targets[n];
        if(target.center.y+target.half.y<=p.y+.01f||target.center.y-target.half.y>=p.y+28)continue;
        const float dx=std::max(0.f,std::abs(p.x-target.center.x)-target.half.x);
        const float dz=std::max(0.f,std::abs(p.z-target.center.z)-target.half.z);
        if(std::hypot(dx,dz)<=18.5f)return false;
      }
      for(size_t n=firstRegion;n<world.interiorRegions.size();++n){
        const auto &region=world.interiorRegions[n];
        if(p.x+18>=region.bounds[0]&&p.x-18<=region.bounds[2]&&
           p.z+18>=region.bounds[1]&&p.z-18<=region.bounds[3]&&
           p.y<region.ceiling&&p.y+28>region.ceiling)return false;
      }
    }
  }
  return true;
}
bool culvert(World &world,Vec3 centre,bool alongX,float halfLength) {
  constexpr float halfWidth=30,halfOpening=25;
  const auto along=alongX?Vec3{1,0,0}:Vec3{0,0,1};
  const auto side=alongX?Vec3{0,0,1}:Vec3{1,0,0};
  const auto a=centre-along*halfLength,b=centre+along*halfLength;
  const std::array<float,4> bounds{std::min(a.x,b.x)-halfWidth*std::abs(side.x),
      std::min(a.z,b.z)-halfWidth*std::abs(side.z),
      std::max(a.x,b.x)+halfWidth*std::abs(side.x),
      std::max(a.z,b.z)+halfWidth*std::abs(side.z)};
  if(!openRoute(world,a,b,halfOpening-4))return false;
  const float low=floorMinimum(world,bounds),high=floorMaximum(world,bounds);
  if(high-low>18||world.interiorCeilingAt({centre.x,high+1,centre.z})<high+43)return false;
  for(float sign:{-1.f,1.f}){
    auto foot=centre+side*(sign*(halfWidth+halfOpening)*.5f);foot.y=low;
    if(!openRoute(world,foot-along*halfLength,foot+along*halfLength,5))return false;
  }
  constexpr float headroom=38;
  const float level=high+headroom;
  const size_t firstSolid=world.targets.size(),firstRegion=world.interiorRegions.size();
  auto solid=[&](const char *id,Vec3 p,Vec3 size){
    addStructuralPiece(world,id,p,size);
    auto &piece=world.targets.back();piece.yaw=alongX?pi/2:0;
    world.updateEnemyBounds(piece);
  };
  for(float sign:{-1.f,1.f}){
    auto foot=centre+side*(sign*(halfWidth+halfOpening)*.5f);
    foot.y=(low+level)*.5f;
    solid("cover_pipe_haunch",foot,{(halfWidth-halfOpening)*.5f,(level-low)*.5f,halfLength});
  }
  solid("cover_pipe_crown",{centre.x,level+4,centre.z},{halfWidth,4,halfLength});
  world.interiorRegions.push_back({bounds,low,level});
  if(!newCoverClearsRoutes(world,firstSolid,firstRegion)){
    world.targets.resize(firstSolid);world.interiorRegions.resize(firstRegion);
    return false;
  }
  return true;
}
} // namespace

void addMissionDeck(World &world,const World::InteriorDeck &deck,Vec3 color){
  world.interiorDecks.push_back(deck);
  const auto &b=deck.bounds;
  const std::array<Vec3,4> corners{{
    {b[0],deck.height(b[0],b[1]),b[1]}, {b[2],deck.height(b[2],b[1]),b[1]},
    {b[2],deck.height(b[2],b[3]),b[3]}, {b[0],deck.height(b[0],b[3]),b[3]}
  }};
  const Vec3 thickness{0,1.2f,0};
  auto &deckMesh=world.editGeometry().interiorDeckMesh;
  auto triangle=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 shade,bool tread=false){
    for(auto position:{a,b,c}) {
      WorldVertex vertex{position,shade};
      if(tread) {
        vertex.textured=terrainArt::textureMode;
        vertex.oneSided=terrainArt::InteriorDeck;
        vertex.u=position.x/32.f;
        vertex.v=position.z/32.f;
      }
      deckMesh.push_back(vertex);
    }
  };
  const bool alongX=std::abs(deck.end.x-deck.start.x)>std::abs(deck.end.z-deck.start.z);
  const auto a=alongX?corners[0]:corners[3],b0=alongX?corners[1]:corners[0];
  const auto c=alongX?corners[3]:corners[2],d=alongX?corners[2]:corners[1];
  constexpr std::array<float,6> widths{0.f,.035f,.061f,.939f,.965f,1.f};
  for(size_t lane=0;lane+1<widths.size();++lane){
    const auto p=a+(c-a)*widths[lane], q=b0+(d-b0)*widths[lane];
    const auto r=a+(c-a)*widths[lane+1],s=b0+(d-b0)*widths[lane+1];
    const Vec3 paint=lane%2?Vec3{.76f,.60f,.25f}:lane==2?color:color*.58f;
    triangle(p,r,q,paint,lane==2);triangle(r,s,q,paint,lane==2);
  }
  for(int n:{1,2})
    triangle(corners[0]-thickness,corners[n]-thickness,corners[n+1]-thickness,color*.64f);
  for(size_t n=0;n<corners.size();++n){
    const auto a=corners[n],b=corners[(n+1)%corners.size()];
    triangle(a,b,a-thickness,color*.75f);
    triangle(b,b-thickness,a-thickness,color*.75f);
  }
}

namespace {
bool fitInteriorTier(World &world,bool underground,bool extended){
  constexpr float rise=38.f, run=138.f, landing=73.f, width=56.f;
  constexpr float half=width*.5f;
  const auto roomCount=world.interiorRegions.size();
  for(size_t room=0;room<roomCount;++room){
    const auto region=world.interiorRegions[room];
    const auto bounds=region.bounds;
    for(bool alongX:{false,true}){
      const int a=alongX?0:1,b=1-a;
      if(bounds[a+2]-bounds[a]<run+landing+32||bounds[b+2]-bounds[b]<width+28)continue;
      for(float across:{.20f,.35f,.65f,.80f,.50f,.05f,.95f,0.f,1.f})for(float sign:{1.f,-1.f}){
        const float fixed=std::lerp(bounds[b]+half+7,bounds[b+2]-half-7,across);
        const float start=sign>0?bounds[a]+16:bounds[a+2]-16;
        auto axis=[&](float distance,float y=0){const float at=start+sign*distance;return alongX?Vec3{at,y,fixed}:Vec3{fixed,y,at};};
        auto low=axis(0);
        low.y=world.surfaceHeight(low.x,low.z);
        if(underground&&low.y+rise>-1)continue;
        const auto high=axis(run,low.y+rise),end=axis(run+landing,high.y);
        auto box=[&](Vec3 one,Vec3 two){
          return std::array<float,4>{std::min(one.x,two.x)-(alongX?0:half),
            std::min(one.z,two.z)-(alongX?half:0),std::max(one.x,two.x)+(alongX?0:half),
            std::max(one.z,two.z)+(alongX?half:0)};
        };
        World::InteriorDeck ramp{box(low,high),low,high},deck{box(high,end),high,end};
        if(region.ceiling-end.y<34.f)continue;
        const auto whole=box(low,end);
        bool clear=true;
        for(const auto &target:world.targets){
          if(!target.active||!target.invulnerable||
             target.center.y-target.half.y>=end.y+30||
             target.center.y+target.half.y<=low.y+.2f)continue;
          if(target.center.x+target.half.x>whole[0]&&target.center.x-target.half.x<whole[2]&&
             target.center.z+target.half.z>whole[1]&&target.center.z-target.half.z<whole[3]){
            clear=false;break;
          }
        }
        // The approach needs to meet the original floor across the full width.
        const Vec3 lateral=alongX?Vec3{0,0,1}:Vec3{1,0,0};
        for(float d:{0.f,3.f,12.f,28.f,50.f,90.f,125.f,138.f,175.f,205.f})
          for(float x:{-half+2,0.f,half-2}){
            const auto p=axis(d)+lateral*x;
            const float ground=world.surfaceHeight(p.x,p.z);
            const float top=low.y+rise*std::min(1.f,d/run);
            if((d<=3&&std::abs(ground-low.y)>.45f)||ground>top+.25f||
               world.waterDepthAt({p.x,ground,p.z},3.5f)>.1f)clear=false;
          }
        if(!clear)continue;
        // Keep the low part of the ramp out of existing travel lanes so the
        // lower storey remains passable after the overlay is added.
        for(auto edge:world.interiorNavLinks){
          const auto a=world.interiorNavNodes[edge[0]],b=world.interiorNavNodes[edge[1]];
          const int n=std::max(1,int(std::ceil(length(b-a)/5.f)));
          for(int i=0;i<=n;++i){
            const auto p=a+(b-a)*(float(i)/n);
            if(p.x<ramp.bounds[0]-6||p.x>ramp.bounds[2]+6||p.z<ramp.bounds[1]-6||p.z>ramp.bounds[3]+6)continue;
            const float clearance=ramp.height(p.x,p.z)-world.surfaceHeight(p.x,p.z);
            if(clearance>1.f&&clearance<31.f){clear=false;break;}
          }
          if(!clear)break;
        }
        if(!clear)continue;
        const auto crossesRamp=[&](Vec3 from,Vec3 to){
          const int samples=std::max(1,int(std::ceil(length(to-from)/4.f)));
          for(int n=0;n<=samples;++n){
            const auto p=from+(to-from)*(float(n)/samples);
            if(p.x<ramp.bounds[0]-5||p.x>ramp.bounds[2]+5||p.z<ramp.bounds[1]-5||p.z>ramp.bounds[3]+5)continue;
            if(ramp.height(p.x,p.z)-world.surfaceHeight(p.x,p.z)>2.f)return true;
          }
          return false;
        };
        unsigned attach=unsigned(world.interiorNavNodes.size());
        float best=1e9f;
        Vec3 approach{};
        for(auto candidate:{axis(-10,low.y),low+lateral*(half+9),low-lateral*(half+9)}){
          candidate.y=world.surfaceHeight(candidate.x,candidate.z);
          if(crossesRamp(candidate,low)||!openRoute(world,candidate,low,9.f))continue;
          for(unsigned n=0;n<world.interiorNavNodes.size();++n){
            const auto p=world.interiorNavNodes[n];
            const float distance=length(p-candidate);
            if(distance<best&&!crossesRamp(p,candidate)&&openRoute(world,p,candidate,9.f)){
              approach=candidate;attach=n;best=distance;
            }
          }
        }
        if(attach==world.interiorNavNodes.size())continue;
        // Return along the neighbouring bay. The two ramps meet on a broad
        // switchback landing, leaving the first flight open overhead.
        constexpr float returnHalf=20.f;
        constexpr float turn=run+landing*.5f;
        constexpr float turnNode=run+landing*.68f;
        constexpr float crest=turn-run;
        const float top=high.y+rise;
        if(extended && (region.ceiling-top<34.f || (underground && top>=0.f)))continue;
        float offset=0;
        if(extended)for(float side:{1.f,-1.f}){
          const float shift=side*(half+returnHalf);
          const float acrossMin=fixed+shift-returnHalf,acrossMax=fixed+shift+returnHalf;
          if(acrossMin<bounds[b]+3 || acrossMax>bounds[b+2]-3)continue;
          // The whole upper flight is already at least one storey above
          // ground. Only structures reaching into this storey can block it.
          auto footprint=whole;
          footprint[b]=acrossMin;
          footprint[b+2]=acrossMax;
          const bool blocked=std::any_of(world.targets.begin(),world.targets.end(),[&](const Target &t){
            return t.active && t.invulnerable && t.center.y+t.half.y>high.y-1.2f &&
              t.center.y-t.half.y<top+30 && t.center.x+t.half.x>footprint[0] &&
              t.center.z+t.half.z>footprint[1] && t.center.x-t.half.x<footprint[2] &&
              t.center.z-t.half.z<footprint[3];
          });
          if(!blocked){offset=shift;break;}
        }
        if(extended && offset==0)continue;
        auto upperPoint=[&](float d,float y){return axis(d,y)+lateral*offset;};
        auto upperDeck=[&](float d1,float y1,float d2,float y2){
          const Vec3 one=upperPoint(d1,y1),two=upperPoint(d2,y2);
          auto footprint=box(one,two);
          footprint[b]=fixed+offset-returnHalf;
          footprint[b+2]=fixed+offset+returnHalf;
          addMissionDeck(world,{footprint,one,two},{.34f,.43f,.45f});
        };
        addMissionDeck(world,ramp,{.35f,.39f,.40f});
        addMissionDeck(world,deck,{.39f,.43f,.43f});
        if(extended){
          upperDeck(run+landing,high.y,turn,high.y);
          upperDeck(turn,high.y,crest,top);
          upperDeck(crest,top,0,top);
        }
        const auto approachNode=site(world,approach);
        unsigned previous=attach;
        connect(world,previous,approachNode);
        previous=approachNode;
        auto waypoint=[&](Vec3 p){
          const auto n=unsigned(world.interiorNavNodes.size());
          world.interiorNavNodes.push_back(p);
          world.interiorTierNodes.push_back(n);
          connect(world,previous,n);previous=n;
        };
        for(Vec3 p:{low,high,axis(turnNode,high.y)})waypoint(p);
        if(extended){
          for(Vec3 p:{upperPoint(turnNode,high.y),upperPoint(turn,high.y),
                      upperPoint(crest,top),upperPoint(crest*.5f,top)})waypoint(p);
        }
        // Keep the balcony open to the room, with visible low barriers on
        // its outer edges and a clear centerline from the ramp.
        for(float edge:{-half,half}){
          if(edge*offset>0)continue; // Open the passage into the adjacent turning bay.
          auto p=axis(run+landing*.5f,high.y+.8f)+lateral*edge;
          addStructuralPiece(world,"city_barrier",p,alongX?Vec3{landing*.5f,.8f,.7f}:Vec3{.7f,.8f,landing*.5f});
        }
        auto rear=axis(run+landing-.9f,high.y+.8f);
        addStructuralPiece(world,"city_barrier",rear,alongX?Vec3{.7f,.8f,half}:Vec3{half,.8f,.7f});
        if(extended){
          // A short parapet marks the terminal gallery where its deck ends
          // above the lower approach. Keep both sides of the objective clear.
          const auto lip=upperPoint(1.f,top+.8f);
          const Vec3 sizes=alongX?Vec3{.7f,.8f,returnHalf}:Vec3{returnHalf,.8f,.7f};
          addStructuralPiece(world,underground?"cover_conduit_wall":"city_barrier",lip,sizes);
        }
        return true;
      }
    }
  }
  return false;
}

void addInteriorTier(World &world,bool underground){
  // Narrow or obstructed halls can still fit the original single ramp.
  if(!fitInteriorTier(world,underground,true))fitInteriorTier(world,underground,false);
}

void buildPartial(World &world,const GenerationPlan &plan,int location,float extent){
  if(!plan.underground&&!plan.submerged&&!(plan.sectionStyle!=0&&(location==0||location==4||location==6)))return;
  const float x0=-.70f*extent,x1=-.26f*extent,z0=-.42f*extent,z1=.06f*extent;
  const std::array<float,4> bounds{x0,z0,x1,z1};
  const char *material=plan.underground?"cover_conduit_wall":plan.submerged?"ship_bulkhead":location==6?"ship_bulkhead":"hangar_wall";
  const bool tier=extent>=750&&(plan.lowerTier||plan.upperTier);
  float floor=floorMinimum(world,bounds),roof=floorMaximum(world,bounds)+roomClearance+(tier?80.f:0.f);
  // Existing route intersections become real portal gaps. A generated enclosure
  // may refine the road network, but must never turn one of its links into a wall.
  size_t existing=world.interiorNavNodes.size();
  auto originalLinks=world.interiorNavLinks;world.interiorNavLinks.clear();
  std::array<std::vector<std::array<float,2>>,4> gaps;
  std::vector<unsigned> portalSites;
  for(auto edge:originalLinks){Vec3 a=world.interiorNavNodes[edge[0]],b=world.interiorNavNodes[edge[1]],delta=b-a;
    std::vector<std::pair<float,unsigned>> crossings;
    auto cross=[&](bool horizontal,float line,int face){
      float denominator=horizontal?delta.z:delta.x;if(std::abs(denominator)<.001f)return;
      float t=(line-(horizontal?a.z:a.x))/denominator;if(t<=.0001f||t>=.9999f)return;
      Vec3 p=a+delta*t;float across=horizontal?p.x:p.z;
      float first=horizontal?x0:z0,last=horizontal?x1:z1;
      if(across<first+1||across>last-1)return;
      auto id=site(world,p);crossings.push_back({t,id});portalSites.push_back(id);
      gaps[face].push_back({across-32,across+32});
    };
    cross(true,z0,0);cross(true,z1,1);cross(false,x0,2);cross(false,x1,3);
    std::sort(crossings.begin(),crossings.end(),[](auto aa,auto bb){return aa.first<bb.first;});
    unsigned previous=edge[0];for(auto entry:crossings){connect(world,previous,entry.second);previous=entry.second;}connect(world,previous,edge[1]);
  }
  int bays=plan.sectionStyle%2?3:2;
  for(int bay=0;bay<bays;++bay){float a=x0+(x1-x0)*bay/bays,b=x0+(x1-x0)*(bay+1)/bays;
    std::array<float,4> room{a,z0,b,z1};
    world.interiorRegions.push_back({room,floor,roof});
    addStructuralPiece(world,plan.underground?"tunnel_wall":"roof_tile",{(a+b)/2,roof+1,(z0+z1)/2},{(b-a)/2,1,(z1-z0)/2});
  }
  auto centre=site(world,{(x0+x1)/2,0,(z0+z1)/2});
  for(auto p:portalSites)connect(world,centre,p);
  for(size_t i=0;i<existing;++i){const auto p=world.interiorNavNodes[i];
    if(p.x>x0+20&&p.x<x1-20&&p.z>z0+20&&p.z<z1-20)connect(world,centre,unsigned(i));}
  // Two broad independent approaches give the underground or flooded chamber
  // a through-route instead of making it an unreachable decorative basement.
  std::array<Vec3,2> doors{{{(x0+x1)/2,0,z1},{x1,0,(z0+z1)/2}}};
  std::array<Vec3,2> approaches{{{(x0+x1)/2,0,z1+125},{x1+125,0,(z0+z1)/2}}};
  for(int side=0;side<2;++side){auto door=site(world,doors[side]),approach=site(world,approaches[side]);
    connect(world,centre,door);connect(world,door,approach);
    float along=side==0?doors[side].x:doors[side].z;gaps[side==0?1:3].push_back({along-32,along+32});
    float nearest=1e20f;unsigned candidate=unsigned(existing);
    for(size_t i=0;i<existing;++i){auto p=world.interiorNavNodes[i];
      if(p.x>x0&&p.x<x1&&p.z>z0&&p.z<z1)continue;
      float distance=length(p-world.interiorNavNodes[approach]);
      bool outside=true;auto a=world.interiorNavNodes[approach];
      int samples=std::max(1,int(std::ceil(std::hypot(p.x-a.x,p.z-a.z)/5)));
      for(int step=0;step<=samples;++step){auto q=a+(p-a)*(float(step)/samples);
        if(q.x>x0-22&&q.x<x1+22&&q.z>z0-22&&q.z<z1+22){outside=false;break;}}
      if(distance<nearest&&outside&&openRoute(world,world.interiorNavNodes[approach],p)){nearest=distance;candidate=unsigned(i);}}
    if(candidate<existing)connect(world,approach,candidate);
  }
  wall(world,material,true,z0,x0,x1,roof,gaps[0]);wall(world,material,true,z1,x0,x1,roof,gaps[1]);
  wall(world,material,false,x0,z0,z1,roof,gaps[2]);wall(world,material,false,x1,z0,z1,roof,gaps[3]);
  if(plan.underground){
    // Partition the excavation into connected galleries. Size each door gap
    // from the route crossing, including oblique approach angles.
    for(float x:{x0+(x1-x0)/3,x1-(x1-x0)/3}){
      std::vector<std::array<float,2>> passages;
      for(auto edge:world.interiorNavLinks){
        const auto a=world.interiorNavNodes[edge[0]],b=world.interiorNavNodes[edge[1]];
        if(std::min(a.x,b.x)>=x||std::max(a.x,b.x)<=x)continue;
        const auto delta=b-a;
        const float z=a.z+delta.z*(x-a.x)/delta.x;
        if(z<z0||z>z1)continue;
        const float clearance=34.f/std::max(.65f,std::abs(delta.x)/std::hypot(delta.x,delta.z));
        passages.push_back({z-clearance,z+clearance});
      }
      if(!passages.empty()){
        const size_t firstSolid=world.targets.size(),firstRegion=world.interiorRegions.size();
        wall(world,"cover_conduit_wall",false,x,z0,z1,roof,passages);
        if(!newCoverClearsRoutes(world,firstSolid,firstRegion)){
          world.targets.resize(firstSolid);world.interiorRegions.resize(firstRegion);
        }
      }
    }
    // Roof the middle of both grade-separated approaches. Each remains open
    // to the surface at one end and enters the large excavated hall at the other.
    for(size_t i=0;i<doors.size();++i){
      const auto middle=(doors[i]+approaches[i])*.5f;
      if(!culvert(world,middle,i==1,36))culvert(world,middle,i==1,26);
    }
  }
  if(tier)addInteriorTier(world,plan.underground);
}
}
void buildMissionCoverRoutes(World &world,int location,int region,float extent,int biome){
  const auto plan=generationPlan(world.prototypeSeed,biome,location,region);
  if(plan.fullInterior||(!(location==0||location==2||location==4||location==5||plan.submerged)))return;
  const auto edges=world.interiorNavLinks;
  std::vector<Vec3> placed;
  const size_t maximum=extent>=750?3:2;
  if(edges.empty())return;
  for(size_t n=0;n<edges.size()&&placed.size()<maximum;++n){
    const auto edge=edges[(n+world.prototypeSeed%edges.size())%edges.size()];
    const auto a=world.interiorNavNodes[edge[0]],b=world.interiorNavNodes[edge[1]];
    const auto centre=(a+b)*.5f;
    const auto delta=b-a;
    bool alongX=std::abs(delta.x)>std::abs(delta.z);
    if(std::hypot(delta.x,delta.z)<122||std::min(std::abs(delta.x),std::abs(delta.z))>1||
       std::hypot(centre.x-world.missionEntry.x,centre.z-world.missionEntry.z)<160||
       world.indoors({centre.x,world.surfaceHeight(centre.x,centre.z)+8,centre.z})||
       std::any_of(placed.begin(),placed.end(),[&](Vec3 p){return std::hypot(p.x-centre.x,p.z-centre.z)<145;}))continue;
    if(culvert(world,centre,alongX,29))placed.push_back(centre);
  }
}
void buildMissionInterior(World &world,int location,int region,float extent,int biome){
  auto plan=generationPlan(world.prototypeSeed,biome,location,region);
  if(!plan.fullInterior){buildPartial(world,plan,location,extent);return;}
  const char *material=location==7?"ship_bulkhead":region==3?"cover_conduit_wall":"hangar_wall";
  const bool tier=extent>=750&&plan.lowerTier;
  constexpr int columns=3;
  int rows=plan.sectionStyle==1?2:plan.sectionStyle==2?4:3;
  std::array<float,4> x{-.82f*extent,-.27f*extent,.27f*extent,.82f*extent};
  std::vector<float> z;
  for(int row=0;row<=rows;++row)z.push_back((-.82f+1.22f*float(row)/rows)*extent);
  // Retain the original central cross-passage variant as one family.
  if(plan.sectionStyle==0||plan.sectionStyle==3)z={-.82f*extent,-.4f*extent,0,.4f*extent};
  world.interiorNavNodes.clear();world.interiorNavLinks.clear();
  std::vector<std::vector<unsigned>> rooms(rows,std::vector<unsigned>(columns));
  std::vector<std::vector<float>> roofs(rows,std::vector<float>(columns));
  for(int row=0;row<rows;++row)for(int col=0;col<columns;++col){
    std::array<float,4> bounds{x[col],z[row],x[col+1],z[row+1]};
    auto centre=Vec3{(x[col]+x[col+1])/2,0,(z[row]+z[row+1])/2};rooms[row][col]=site(world,centre);
    float low=floorMinimum(world,bounds),top=floorMaximum(world,bounds)+roomClearance+(tier?80.f:0.f);roofs[row][col]=top;
    bool atrium=plan.sectionStyle==3&&!plan.underground&&row==rows/2&&col==1;
    if(!atrium){world.interiorRegions.push_back({bounds,low,top});
      addStructuralPiece(world,"roof_tile",{centre.x,top+1,centre.z},{(x[col+1]-x[col])/2,1,(z[row+1]-z[row])/2});}
  }
  for(int row=0;row<rows;++row){
    wall(world,material,false,x[0],z[row],z[row+1],roofs[row][0]);
    wall(world,material,false,x[3],z[row],z[row+1],roofs[row][2]);
    for(int col=1;col<3;++col){float middle=(z[row]+z[row+1])/2;
      wall(world,material,false,x[col],z[row],z[row+1],std::max(roofs[row][col-1],roofs[row][col]),{{middle-doorHalf,middle+doorHalf}});
      auto portal=site(world,{x[col],0,middle});connect(world,rooms[row][col-1],portal);connect(world,portal,rooms[row][col]);}
  }
  for(int col=0;col<3;++col){float middle=(x[col]+x[col+1])/2;
    wall(world,material,true,z.front(),x[col],x[col+1],roofs[0][col]);
    wall(world,material,true,z.back(),x[col],x[col+1],roofs.back()[col],col==1?std::vector<std::array<float,2>>{{middle-doorHalf,middle+doorHalf}}:std::vector<std::array<float,2>>{});
    for(int row=1;row<rows;++row){
      wall(world,material,true,z[row],x[col],x[col+1],std::max(roofs[row-1][col],roofs[row][col]),{{middle-doorHalf,middle+doorHalf}});
      auto portal=site(world,{middle,0,z[row]});connect(world,rooms[row-1][col],portal);connect(world,portal,rooms[row][col]);}
  }
  int alcoves=plan.sectionStyle==1?1:2;
  for(int row=0;row<rows;++row)for(int col=0;col<3;++col){
    auto centre=world.interiorNavNodes[rooms[row][col]];float halfZ=(z[row+1]-z[row])/2;
    for(int corner=0;corner<alcoves;++corner){
      float side=((world.prototypeSeed+row+col+corner)&1)?1.f:-1.f;
      float a=side<0?x[col]+4:centre.x+42,b=side<0?centre.x-42:x[col+1]-4;
      float passageX=(a+b)/2;
      // Keep the subsidiary room wide enough for the full animated envelope
      // even in the compact four-row facility family.
      float partition=std::min(halfZ*.5f,halfZ-48.f);
      float sideZ=corner?-1.f:1.f;
      float boundary=centre.z+sideZ*partition;
      wall(world,material,true,boundary,a,b,roofs[row][col],{{passageX-doorHalf,passageX+doorHalf}});
      auto approach=site(world,{passageX,0,centre.z}),portal=site(world,{passageX,0,boundary});
      auto alcove=site(world,{passageX,0,centre.z+sideZ*(partition+halfZ)*.5f});
      connect(world,rooms[row][col],approach);connect(world,approach,portal);connect(world,portal,alcove);
    }
  }
  auto entryDoor=site(world,{0,0,z.back()}),entry=site(world,{0,0,extent*.65f});
  connect(world,rooms.back()[1],entryDoor);connect(world,entryDoor,entry);
  auto leftFront=site(world,{-.92f*extent,0,.65f*extent}),rightFront=site(world,{.92f*extent,0,.65f*extent});
  auto leftBack=site(world,{-.92f*extent,0,-.92f*extent}),rightBack=site(world,{.92f*extent,0,-.92f*extent});
  connect(world,entry,leftFront);connect(world,entry,rightFront);connect(world,leftFront,leftBack);connect(world,leftBack,rightBack);connect(world,rightBack,rightFront);
  world.missionEntry=world.interiorNavNodes[entry];world.hasMissionEntry=true;
  if(tier)addInteriorTier(world,true);
}
}
