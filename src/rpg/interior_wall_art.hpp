// Room-scale visual shells for structures fitted to generated wall dimensions.
#pragma once
#include "simulation.hpp"
#include <string_view>

namespace iron::rpg::interior_art {
inline bool buildWall(WorldMesh &mesh, std::string_view model, Vec3 half) {
  const bool header = model == "door_header";
  const bool hangar = model == "hangar_wall";
  const bool ship = model == "ship_bulkhead";
  const bool tunnel = model == "tunnel_wall" || model == "cover_conduit_wall";
  if (!header && !hangar && !ship && !tunnel) return false;

  const bool spanX = half.x >= half.z;
  const float span = std::max(half.x, half.z) * 2;
  const float height = half.y * 2;
  const float depth = std::min(half.x, half.z);
  // The excavation also uses tunnel_wall for its horizontal roof slab. Very
  // narrow jambs and shallow pieces retain their existing structural templates.
  if (span < 7 || height < 3 || depth > 4) return false;

  const Vec3 frame{.19f,.22f,.23f}, black{.10f,.125f,.13f}, amber{.91f,.59f,.21f};
  const Vec3 backing = ship ? Vec3{.21f,.28f,.30f} :
                       tunnel ? Vec3{.35f,.32f,.28f} : Vec3{.29f,.32f,.35f};
  const Vec3 plate = ship ? Vec3{.51f,.61f,.60f} :
                     tunnel ? Vec3{.59f,.54f,.45f} : Vec3{.58f,.61f,.63f};
  constexpr int bulkhead=8, concrete=9, panel=10, vents=2, steel=5, stripes=7;
  const int skin = ship ? bulkhead : tunnel ? concrete : panel;
  const int bays = std::clamp(int(std::round(span / (header ? 30.f : 40.f))), 1, 4);
  const float width = span / bays;
  mesh.clear();
  // Four double-sided bays are the maximum even on kilometer maps.
  const int trianglesPerBay = header ? 16 : hangar ? 48 : 40;
  mesh.reserve(size_t(12+bays*trianglesPerBay)*3);

  // u runs across the frontage; d runs perpendicular to it. Winding and
  // side-dependent ambient shades make the recessed stamp readable from either room.
  auto vertex = [&](float u,float y,float d,Vec3 color,int tile) {
    const Vec3 p = spanX ? Vec3{u,y,d} : Vec3{-d,y,u};
    // Manifest rows are top-down, while the OpenGL atlas is V-up.
    const int gpuTile = tile%8 + 8*(7-tile/8);
    return WorldVertex{p,color,0,0,3.f,float(gpuTile)};
  };
  Vec3 solidCenter{0,half.y,0};
  auto quad = [&](WorldVertex a,WorldVertex b,WorldVertex c,WorldVertex d) {
    // Orient faces outwards in both elevation directions (including the
    // reverse side of each raised stamp), independent of renderer culling.
    if(dot(cross(b.position-a.position,c.position-a.position),a.position-solidCenter)<0)
      std::swap(b,d);
    const Vec3 normal = cross(b.position-a.position,c.position-a.position);
    const bool horizontal = std::abs(normal.y)>std::max(std::abs(normal.x),std::abs(normal.z));
    for(auto *v : {&a,&b,&c,&d}) {
      v->u = (!horizontal && std::abs(normal.x)>std::abs(normal.z) ? v->position.z : v->position.x)/8;
      v->v = (horizontal ? v->position.z : v->position.y)/8;
    }
    mesh.insert(mesh.end(), {a,b,c,a,c,d});
  };
  const float base = depth * .57f;
  const float edge = span * .5f;
  // Close the return faces and caps around the two detailed elevations.
  for(float sign : {-1.f,1.f}) {
    auto end = [&](float y,float d){return vertex(sign*edge,y,d,frame,steel);};
    quad(end(0,-base),end(height,-base),end(height,base),end(0,base));
    const float y = sign < 0 ? 0 : height;
    auto cap = [&](float u,float d){return vertex(u,y,d,backing,skin);};
    quad(cap(-edge,-base),cap(edge,-base),cap(edge,base),cap(-edge,base));
  }

  for(float face : {-1.f,1.f}) {
    auto wall = [&](float u,float y){return vertex(u,y,face*base,backing,skin);};
    quad(wall(-edge,0),wall(edge,0),wall(edge,height),wall(-edge,height));
    for(int bay=0;bay<bays;++bay) {
      const float start = -edge+width*bay;
      // Layered trapezoids follow the mech-scale bay rather than stretching the
      // source template. The primary rib has return faces for grazing views. The smaller
      // stamps sit close to the backing as two-triangle relief inlays.
      auto band = [&](float bottomLeft,float bottomRight,float topLeft,float topRight,
                      float bottom,float top,float relief,Vec3 color,int tile,bool raised=false) {
        const float front = base+(depth-base)*relief;
        auto point = [&](float x,float y,float d,Vec3 tint) {
          return vertex(start+x*width,y*height,face*d,tint,tile);
        };
        auto a=point(bottomLeft,bottom,front,color), b=point(bottomRight,bottom,front,color);
        auto c=point(topRight,top,front,color), d=point(topLeft,top,front,color);
        solidCenter=point((bottomLeft+bottomRight+topLeft+topRight)*.25f,
                          (bottom+top)*.5f,(base+front)*.5f,color).position;
        quad(a,b,c,d);
        if(!raised) {
          solidCenter={0,half.y,0};
          return;
        }
        auto side = [&](float x0,float y0,float x1,float y1,float light){
          auto tint=color*light;
          quad(point(x0,y0,base,tint),point(x1,y1,base,tint),
               point(x1,y1,front,tint),point(x0,y0,front,tint));
        };
        side(bottomLeft,bottom,topLeft,top,.78f);
        side(bottomRight,bottom,topRight,top,.62f);
        side(topLeft,top,topRight,top,.90f);
        side(bottomLeft,bottom,bottomRight,bottom,.56f);
        solidCenter={0,half.y,0};
      };
      // Paint and inlays sit between structural layers. They need only a front face.
      auto inlay = [&](float l,float r,float y,float t,float relief,Vec3 color,int tile) {
        const float d = face*(base+(depth-base)*relief);
        auto p=[&](float x,float h){return vertex(start+x*width,h*height,d,color,tile);};
        quad(p(l,y),p(r,y),p(r,t),p(l,t));
      };
      if(header) {
        band(.04f,.92f,.13f,.99f,.18f,.94f,.08f,plate,steel);
        inlay(.20f,.81f,.28f,.54f,.12f,black,vents);
        inlay(.22f,.45f,.34f,.45f,.16f,amber,steel);
        inlay(.04f,.14f,.03f,.14f,.10f,amber,stripes);
      } else if(tunnel) {
        // Cable trough behind a tapered concrete spring line and paired pipe markers.
        band(.03f,.23f,.10f,.15f,.02f,.86f,1,plate,concrete,true);
        band(.09f,.17f,.02f,.36f,.83f,.99f,.10f,plate,concrete);
        band(.24f,.98f,.24f,.98f,.31f,.39f,.10f,frame,steel);
        inlay(.26f,.96f,.34f,.365f,.15f,{.68f,.42f,.21f},steel);
        band(.42f,.84f,.39f,.87f,.53f,.74f,.10f,black,vents);
        inlay(.435f,.825f,.69f,.714f,.15f,amber,steel);
      } else if(ship) {
        // Watertight clipped shoulders around a dark inspection port; paired
        // latches are visible on the return trip down the same passage.
        band(.13f,.87f,.06f,.94f,.08f,.48f,.09f,plate,steel);
        band(.06f,.94f,.15f,.85f,.49f,.90f,.09f,plate,steel);
        band(.23f,.77f,.28f,.72f,.56f,.74f,.91f,frame,steel,true);
        inlay(.31f,.69f,.60f,.69f,.97f,{.19f,.36f,.38f},vents);
        for(float x:{.155f,.80f}) inlay(x,x+.04f,.40f,.52f,.16f,amber,steel);
      } else {
        // Splayed gantry leg, upper observation strip and a personnel service
        // hatch below the control band distinguish a vehicle hall at cockpit height.
        band(.05f,.24f,.14f,.21f,0,.76f,1,plate,steel,true);
        band(.14f,.21f,.20f,.46f,.76f,.98f,.10f,plate,steel);
        band(.31f,.95f,.29f,.97f,.58f,.75f,.10f,black,vents);
        for(int i=0;i<3;++i) {
          const float x=.34f+.20f*i;
          inlay(x,x+.16f,.625f,.70f,.18f,{.43f,.60f,.61f},steel);
        }
        const float hatchHeight = std::min(2.6f/height,.20f);
        const float hatchWidth = std::min(1.5f/width,.23f);
        band(.68f,.68f+hatchWidth,.68f,.68f+hatchWidth,.015f,.015f+hatchHeight,.10f,frame,steel);
        inlay(.70f,.84f,.23f,.25f,.10f,amber,steel);
      }
    }
  }
  return true;
}
} // namespace iron::rpg::interior_art
