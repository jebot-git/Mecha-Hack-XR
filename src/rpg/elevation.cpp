#include "rpg/world_generation.hpp"
#include "rpg/generation.hpp"
#include "rpg/session.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <queue>
namespace iron::rpg {
namespace {
float ramp(float x,float a,float b){return std::clamp((x-a)/std::max(.001f,b-a),0.f,1.f);}
float outsideRectangle(Vec3 p,std::array<float,4> r){return std::hypot(std::max({r[0]-p.x,p.x-r[2],0.f}),std::max({r[1]-p.z,p.z-r[3],0.f}));}
}
// Every profile is a physical heightfield, with broad slopes and flat site cores.
// The seed selects a landform family as well as its relief, orientation and phase.
void buildMissionElevation(World &world,int location,int region,float extent,int biome,int landform){
  auto plan=generationPlan(world.prototypeSeed,biome,location,region);
  if(landform>=0){static constexpr int profiles[]={2,4,3,5,4,5,1,0,2,4,3,1};plan.height=profiles[unsigned(landform)%12];}
  const float phase=float((world.prototypeSeed>>5)%360)*pi/180;
  const float relief=plan.relief;
  const float axis=plan.layout==1?pi/4:plan.layout==4?phase:0;
  auto exterior=[&](Vec3 p){
    float x=p.x*std::cos(axis)+p.z*std::sin(axis),z=p.z*std::cos(axis)-p.x*std::sin(axis);
    if(plan.organic){float a=x/extent,b=z/extent;x+=extent*.055f*std::sin(b*4+phase);z+=extent*.045f*std::sin(a*3-phase);}
    float u=x/extent,v=z/extent,r=std::hypot(u,v);
    switch(plan.height%6){
    case 0: { // Broad asymmetric terraces, with a continuous central lowland.
      float width=std::max(120.f,relief/.28f);
      return relief*ramp(std::abs(x),extent*.25f-width*.5f,extent*.25f+width*.5f)*(x<0?.65f:1.f);
    }
    case 1: // Curved ridge and open passes, unlike parallel terrace bands.
      return relief*std::max(0.f,1-std::abs(u-.18f*std::sin(v*3+phase))/.52f)*(.68f+.22f*std::cos(v*2));
    case 2: // Caldera rim enclosing a depressed central basin.
      return relief*(.85f*std::max(0.f,1-std::abs(r-.52f)/.34f)-.38f*std::max(0.f,1-r/.45f));
    case 3: // Descending ravine with a winding channel and broad shoulders.
      return relief*(.58f*std::max(0.f,std::min(1.f,(std::abs(u+.22f*std::sin(v*2.5f+phase))-.10f)/.62f))-.40f);
    case 4: // Offset mesas and saddles create nonuniform highland destinations.
      return relief*(.75f*std::max(0.f,1-std::hypot(u+.43f,v+.25f)/.66f)+
                     .65f*std::max(0.f,1-std::hypot(u-.42f,v-.18f)/.62f));
    default: // Coastal shelves slope through several broad usable elevations.
      return relief*(.28f*ramp(v,-.78f,-.38f)+.40f*ramp(v,-.28f,.10f)+
                     .32f*ramp(v,.20f,.62f))*(.72f+.16f*std::sin(u*3+phase));
    }
  };
  const bool partial=!plan.fullInterior&&(plan.underground||plan.submerged||plan.surfaceFacility);
  const std::array<float,4> core{-.70f*extent,-.42f*extent,-.26f*extent,.06f*extent};
  const float surfaceFloor=exterior({-.48f*extent,0,-.18f*extent})+
                           world.surfaceHeight(-.48f*extent,-.18f*extent);
  const float extraDepth = extent>=750 && plan.lowerTier ? 40.f : 0.f;
  const float sectionFloor=plan.submerged?-18.f-2.f*float(plan.sectionStyle):plan.underground?-56.f-extraDepth-6.f*float(plan.sectionStyle):surfaceFloor;
  const int rows=plan.sectionStyle==1?2:plan.sectionStyle==2?4:3;
  auto interior=[&](Vec3 p){
    const float base=plan.underground?-48.f-extraDepth:plan.submerged?sectionFloor:0;
    const float sideA=plan.submerged?3.f:18.f+float(plan.height%3)*6+relief*.12f;
    const float sideB=plan.submerged?5.f:20.f+float((plan.height+1)%3)*6+relief*.18f;
    float width=std::max(100.f,std::max(sideA,sideB)/.27f);
    width=std::min(width,extent*.30f);
    float a=ramp(-p.x,extent*.27f-width*.5f,extent*.27f+width*.5f);
    float b=ramp(p.x,extent*.27f-width*.5f,extent*.27f+width*.5f);
    float rowHeight=0;
    // Constant room cores separated by wide grade transitions. This adds depth
    // variations without making every prop sit on a permanently sloping floor.
    for(int row=1;row<rows;++row){
      float boundary=extent*(-.82f+1.22f*float(row)/rows);
      float step=float(((plan.height+row)%3)-1)*3.f;
      rowHeight+=step*ramp(p.z,boundary-36,boundary+36);
    }
    float height=base+a*(sideA+rowHeight)+b*(sideB-rowHeight);
    float dx=std::max(0.f,std::abs(p.x)-extent*.82f);
    float dz=std::max({0.f,-extent*.82f-p.z,p.z-extent*.4f});
    return height*std::clamp(1-std::max(dx,dz)/125.f,0.f,1.f);
  };
  for(auto &v:world.editGeometry().terrain){
    auto &p=v.position;
    if(plan.fullInterior){
      p.y=interior(p);
      if(std::abs(p.x)<=extent*.82f&&p.z>=-extent*.82f&&p.z<=extent*.4f)
        v.color={.18f+float(plan.palette)*.008f,.22f+float(plan.palette%3)*.01f,.25f};
    }else{
      float entry=ramp(std::hypot(p.x-world.missionEntry.x,p.z-world.missionEntry.z),100,220);
      float dry=1;
      for(const auto &water:world.waterRegions)dry=std::min(dry,ramp(water.shore.empty()?outsideRectangle(p,water.bounds):std::max(0.f,-proceduralWaterEdge(water,p.x,p.z)),0,96));
      p.y+=exterior(p)*entry*dry;
      if(!partial&&(location==2||location==3||location==5)){
        // Shallow, traversable combat trenches have tapered entrances and
        // continuous banks. The grade pass below keeps their edges walkable.
        for(float side:{-1.f,1.f}){
          const float z=p.z+side*extent*.18f;
          const float axis=side*extent*.54f+24.f*std::sin(z*.012f+phase);
          const float depth=(location==2?13.f:10.f)*
              (1-ramp(std::abs(p.x-axis),12,58))*(1-ramp(std::abs(z),extent*.23f,extent*.54f));
          p.y-=depth*entry*dry;
        }
      }
      if(partial){
        // Limit the approach grade against the actual surrounding landform.
        // Blending only by basement depth made high ridges into steep lips.
        float reach=outsideRectangle(p,core)*.24f;
        p.y=std::clamp(p.y,sectionFloor-reach,sectionFloor+reach);
      }
    }
  }
  // A road over a flooded heightfield can otherwise leave a sharp raised
  // edge across a later facility approach. Erode only slopes exceeding the
  // traversal grade, producing actual ramps rather than exempting collision.
  std::map<std::pair<float,float>,size_t> ids;
  std::vector<Vec3> points;std::vector<size_t> vertices;vertices.reserve(world.geometry().terrain.size());
  for(const auto &v:world.geometry().terrain){auto key=std::pair{v.position.x,v.position.z};auto [it,inserted]=ids.emplace(key,points.size());
    if(inserted)points.push_back(v.position);else points[it->second].y=std::min(points[it->second].y,v.position.y);vertices.push_back(it->second);}
  std::vector<std::vector<std::pair<size_t,float>>> neighbours(points.size());
  for(size_t i=0;i+2<vertices.size();i+=3){
    // Thin boundary fans amplify a plain edge-gradient cap. Scale the cap
    // using the best-conditioned pair of triangle edges: the resulting face
    // gradient is at most .28 even when its vertices are nearly collinear.
    float conditioning=0;
    for(int corner=0;corner<3;++corner){auto a=points[vertices[i+corner]],b=points[vertices[i+(corner+1)%3]],c=points[vertices[i+(corner+2)%3]];
      float ux=b.x-a.x,uz=b.z-a.z,vx=c.x-a.x,vz=c.z-a.z;
      float denominator=std::hypot(ux,uz)*std::hypot(vx,vz);if(denominator<.000001f)continue;
      float cosine=std::clamp((ux*vx+uz*vz)/denominator,-1.f,1.f);
      conditioning=std::max(conditioning,std::sqrt(std::max(0.f,(1-std::abs(cosine))*.5f)));}
    if(conditioning<.00001f)continue;
    for(int side=0;side<3;++side){size_t a=vertices[i+side],b=vertices[i+(side+1)%3];
      float rise=.28f*conditioning*std::hypot(points[a].x-points[b].x,points[a].z-points[b].z);
      neighbours[a].push_back({b,rise});neighbours[b].push_back({a,rise});}
  }
  using Item=std::pair<float,size_t>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>> pending;
  for(size_t i=0;i<points.size();++i)pending.push({points[i].y,i});
  while(!pending.empty()){auto [height,id]=pending.top();pending.pop();if(height>points[id].y+.0001f)continue;
    for(auto [next,rise]:neighbours[id])if(points[next].y>height+rise+.0001f){points[next].y=height+rise;pending.push({points[next].y,next});}}
  for(size_t i=0;i<vertices.size();++i)world.editGeometry().terrain[i].position.y=points[vertices[i]].y;
  if(partial&&plan.underground)for(auto &v:world.editGeometry().terrain){auto &p=v.position;
    // Deep excavations must not pull the insertion court below grade during
    // slope limiting. Keep its original zero-height landing pad and a broad
    // continuous approach, outside the reserved underground footprint.
    p.y*=ramp(std::hypot(p.x-world.missionEntry.x,p.z-world.missionEntry.z),80,240);
  }
  for(auto &patch:world.editGeometry().terrainPatches){patch.low.y=1e9f;patch.high.y=-1e9f;
    for(size_t i=patch.first;i<patch.first+patch.count;++i){float y=world.geometry().terrain[i].position.y;
      patch.low.y=std::min(patch.low.y,y);patch.high.y=std::max(patch.high.y,y);}}
  world.indexTerrain();
}
}
