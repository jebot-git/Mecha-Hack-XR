#include "simulation.hpp"
#include <algorithm>
#include <limits>
namespace iron {
namespace {
bool within(std::array<float,4> bounds,Vec3 p,float radius=0) {
  return p.x+radius>=bounds[0]&&p.x-radius<=bounds[2]&&
         p.z+radius>=bounds[1]&&p.z-radius<=bounds[3];
}
}
float World::InteriorDeck::height(float x,float z) const {
  const Vec3 run{end.x-start.x,0,end.z-start.z};
  const float square=dot(run,run);
  const float progress=square>0?std::clamp(dot(Vec3{x-start.x,0,z-start.z},run)/square,0.f,1.f):0.f;
  return start.y+(end.y-start.y)*progress;
}
float World::walkableHeight(Vec3 feet,float step) const {
  float floor=surfaceHeight(feet.x,feet.z);
  for(const auto &deck:interiorDecks)if(within(deck.bounds,feet)){
    const float y=deck.height(feet.x,feet.z);
    if(y<=feet.y+step)floor=std::max(floor,y);
  }
  return floor;
}
float World::pilotSupportHeight(Vec3 feet,float step) const {
  const float floor=walkableHeight(feet,step);
  // A roof can support a pilot as long as its live collision mesh exists.
  // At terrain/deck height the walking solver already has an exact support.
  if(feet.y-floor>.02f){
    auto roof=rpg::gravityRay(*this,feet+Vec3{0,step+.01f,0},{feet.x,floor,feet.z});
    if(roof&&roof->normal.y>=.68f)return std::max(floor,roof->point.y);
  }
  return floor;
}
float World::waterLevelAt(Vec3 p) const {
  float level=-std::numeric_limits<float>::infinity();
  for(const auto &water:waterRegions)
    if(within(water.bounds,p)&&proceduralWaterEdge(water,p.x,p.z)>=0)
      level=std::max(level,water.level);
  return level;
}
float stepMechaDescent(Vec3 &feet,float floor,float &downSpeed,float waterLevel,float dt){
  if(feet.y<=floor+.0001f){feet.y=floor;downSpeed=0;return 0;}
  constexpr float acceleration=19.6f, terminal=50.f, sinkAcceleration=1.5f, sinkLimit=2.6f;
  auto advance=[&](float limit,float gravity,float boundary,float &time){
    downSpeed=std::min(downSpeed,limit);
    const float distance=feet.y-boundary;
    const float finalSpeed=std::min(limit,std::sqrt(downSpeed*downSpeed+2*gravity*distance));
    const float accelerated=(finalSpeed-downSpeed)/gravity;
    const float acceleratingDistance=.5f*(downSpeed+finalSpeed)*accelerated;
    const float travelTime=accelerated+std::max(0.f,distance-acceleratingDistance)/finalSpeed;
    if(time>=travelTime){feet.y=boundary;downSpeed=finalSpeed;time-=travelTime;return true;}
    const float acceleratingTime=std::min(time,(limit-downSpeed)/gravity);
    feet.y-=downSpeed*time+gravity*acceleratingTime*(time-.5f*acceleratingTime);
    downSpeed+=gravity*acceleratingTime;
    time=0;
    return false;
  };
  if(feet.y>waterLevel){
    const float boundary=std::max(waterLevel,floor);
    if(!advance(terminal,acceleration,boundary,dt))return 0;
  }
  if(feet.y>floor&&!advance(sinkLimit,sinkAcceleration,floor,dt))return 0;
  const float impact=downSpeed;
  downSpeed=0;
  return impact;
}
float World::deckContact(Vec3 from,Vec3 to) const {
  float first=2.f;
  for(const auto &d:interiorDecks){
    const Vec3 run{d.end.x-d.start.x,0,d.end.z-d.start.z};
    const float sq=dot(run,run);
    const auto gap=[&](Vec3 p){
      const float t=sq>0?dot(Vec3{p.x-d.start.x,0,p.z-d.start.z},run)/sq:0.f;
      return p.y-(d.start.y+(d.end.y-d.start.y)*t);
    };
    float near=0.f,far=1.f;
    const auto clip=[&](float a,float b,float minimum,float maximum){
      const float d=b-a;
      if(std::abs(d)<.00001f)return a>=minimum&&a<=maximum;
      float start=(minimum-a)/d,finish=(maximum-a)/d;
      if(start>finish)std::swap(start,finish);
      near=std::max(near,start);far=std::min(far,finish);
      return near<=far;
    };
    // Clip against the sloped slab in both directions, including its thin edges.
    if(clip(from.x,to.x,d.bounds[0],d.bounds[2])&&
       clip(from.z,to.z,d.bounds[1],d.bounds[3])&&
       clip(gap(from),gap(to),-1.2f,0.f)&&far>.00001f)
      first=std::min(first,near);
  }
  return first;
}
namespace {
float sampledWaterDepth(const World &world, Vec3 position, float radius, bool useDecks) {
    auto floorAt = [&](float x, float z) {
        return useDecks ? world.walkableHeight({x,position.y,z},.65f*radius+.65f)
                        : world.surfaceHeight(x,z);
    };
  if(!std::isfinite(radius)||radius<0||!std::isfinite(position.x)||!std::isfinite(position.z))
    return std::numeric_limits<float>::infinity();
  float depth=0;
  for(const auto &water:world.waterRegions) {
    const auto &b=water.bounds;
    if(!within(b,position,radius))continue;
    if(radius==0){if(proceduralWaterEdge(water,position.x,position.z)>=0)depth=std::max(depth,water.level-floorAt(position.x,position.z));continue;}
    float x0=std::max(position.x-radius,b[0]),x1=std::min(position.x+radius,b[2]);
    float z0=std::max(position.z-radius,b[1]),z1=std::min(position.z+radius,b[3]);
    // Sample the wet intersection, including its point nearest the basin's
    // deepest centre. A thin deep channel must not disappear between feet.
    for(float x:{x0,std::clamp((b[0]+b[2])/2,x0,x1),x1})
      for(float z:{z0,std::clamp((b[1]+b[3])/2,z0,z1),z1})
        if(proceduralWaterEdge(water,x,z)>=0)depth=std::max(depth,water.level-floorAt(x,z));
  }
  return std::max(0.f,depth);
}
} // namespace
float World::waterDepthAt(Vec3 position,float radius) const {
    return sampledWaterDepth(*this,position,radius,false);
}
float World::wadingDepthAt(Vec3 feet,float radius) const {
    return sampledWaterDepth(*this,feet,radius,!interiorDecks.empty());
}
float World::waterTraversalLimitAt(Vec3 position,float radius) const {
  for(auto b:submergedRoutes)
    if(position.x-radius>=b[0]&&position.x+radius<=b[2]&&
       position.z-radius>=b[1]&&position.z+radius<=b[3])return std::max(36.f,maxWadeDepth);
  return maxWadeDepth;
}
float World::waterMovementScale(Vec3 position,float radius) const {
  if(maxWadeDepth<=0)return 1;
  float depth=wadingDepthAt(position,radius);
  return 1-.35f*std::clamp(depth/std::max(1.f,waterTraversalLimitAt(position,radius)),0.f,1.f);
}
bool World::indoors(Vec3 p) const {
  return std::any_of(interiorRegions.begin(),interiorRegions.end(),[&](const auto &r){
    return within(r.bounds,p)&&p.y>=r.floor-1&&p.y<r.ceiling;
  });
}
float World::interiorCeilingAt(Vec3 p,float radius) const {
  float ceiling=std::numeric_limits<float>::infinity();
  for(const auto &r:interiorRegions)if(within(r.bounds,p,radius)&&p.y<r.ceiling)
    ceiling=std::min(ceiling,r.ceiling);
  for(const auto &deck:interiorDecks)if(within(deck.bounds,p,radius)){
    const auto &b=deck.bounds;
    const float x=std::clamp(p.x,b[0],b[2]),z=std::clamp(p.z,b[1],b[3]);
    const float underside=deck.height(x,z)-1.2f;
    if(p.y<underside)ceiling=std::min(ceiling,underside);
  }
  return ceiling;
}
bool World::exposedToPrecipitation(Vec3 point) const {
  return !std::isfinite(interiorCeilingAt(point))&&point.y>=waterLevelAt(point);
}
EnvironmentSettings World::outdoorEnvironment() const {
  return environmentEnabled()?
      (rpgPrototype&&weatherHazard(environment.weather)?environment:restrictEnvironment(environment,climate)):EnvironmentSettings{};
}
EnvironmentSettings World::environmentAt(Vec3 p) const {
  auto settings=outdoorEnvironment();
  if(indoors(p)){settings.weather=Weather::Clear;settings.authoredSky=true;}
  return settings;
}
}
