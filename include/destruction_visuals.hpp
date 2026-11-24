#pragma once
#include "vector.hpp"
#include "rpg/enemy_effects.hpp"
#include <algorithm>
#include <cmath>
namespace iron {
enum class DestructionMaterial : unsigned {Metal,Concrete,Organic,Wood,Rock,Glass};
inline constexpr unsigned rpgDebrisBase=0x70000000u,rpgDust=0x70000100u,rpgFire=0x70000101u,rpgSplash=0x70000102u,rpgScorch=0x70000103u;
inline constexpr unsigned rpgLandingWave=0x70000104u;
inline float landingWaveRadius(float impact){return impact>=1.7f?std::clamp(3.f+impact*.28f,3.5f,10.f):0.f;}
inline bool rpgDebris(unsigned id){return id>=rpgDebrisBase&&id<rpgDebrisBase+6;}
inline Vec3 rpgFragmentHalf(unsigned id){
  constexpr Vec3 half[]={{1,.16f,.55f},{.75f,.48f,.65f},{.55f,.3f,.5f},{.16f,.17f,1.15f},{.6f,.55f,.7f},{.7f,.035f,.5f}};
  return half[(id-rpgDebrisBase)%6];
}
inline Vec3 rpgFragmentColor(unsigned id){
  constexpr Vec3 colors[]={{.42f,.49f,.53f},{.42f,.38f,.31f},{.33f,.27f,.14f},{.32f,.19f,.09f},{.34f,.31f,.27f},{.27f,.53f,.58f}};
  return colors[(id-rpgDebrisBase)%6];
}
inline float destructionEffectLife(unsigned model,int kind){
  if(float life=rpg::enemyEffectLife(model);life>0)return life;
  if(rpgDebris(model))return 5;
  if(model==rpgLandingWave)return .55f;
  if(model==rpgDust)return 2.2f;
  if(model==rpgFire)return .7f;
  if(model==rpgSplash)return 1.2f;
  if(model==rpgScorch)return 20;
  return kind==4?3.f:kind==2||kind==5?1.f:kind==0?.12f:.5f;
}
inline float destructionSpin(float age,unsigned model){return rpgDebris(model)?std::min(age,1.3f):age;}
}
