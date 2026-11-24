#pragma once
#include "arms.hpp"
#include <optional>

namespace iron {
struct World;
namespace rpg {
enum class GravityMode {Ground, Attached, Floating, Pulling};
struct SurfaceGravity {
    GravityMode mode=GravityMode::Ground;
    Quaternion frame{};
    Vec3 drift{},destination{},normal{};
    float delay=0;
    bool active() const {return mode!=GravityMode::Ground;}
    Vec3 up() const {return rotate(frame,{0,1,0});}
};
struct GravityContact {
    Vec3 point,normal;
    float fraction=1;
};
Quaternion gravityInverse(Quaternion);
Quaternion alignGravity(Quaternion,Vec3 normal);
std::optional<GravityContact> gravityRay(const World &,Vec3 from,Vec3 to,bool terrain=false);
bool beginGravityPull(const World &,SurfaceGravity &,Vec3 feet,const GravityContact &,float height,float radius);
bool gravityImpact(const World &,SurfaceGravity &,Vec3 feet,Vec3 displacement,float height,float radius);
bool moveOnGravity(const World &,SurfaceGravity &,Vec3 &feet,Vec3 displacement,float height,float radius);
void advanceGravity(const World &,SurfaceGravity &,Vec3 &feet,float height,float radius,float dt);
}
}
