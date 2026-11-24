#pragma once
#include "vector.hpp"
#include <span>
#include <vector>
namespace iron {
struct World;
struct CockpitLighting {
    Vec3 sky{},ground{},direction{-.3f,1,.6f};bool night=false;
    bool operator==(const CockpitLighting&)const;
};
// Stable mission illumination: transient storm flashes and head motion do not
// invalidate the bake. Material/emission data remains immutable between bakes.
CockpitLighting cockpitLighting(const World&);
Vec3 bakeCockpitColour(Vec3 position,Vec3 normal,Vec3 albedo,float emission,const CockpitLighting&);
// Native ISCP stream: position, normal, linear material RGB, emission (10 floats).
std::vector<float> bakeCockpitVertices(std::span<const float>,const CockpitLighting&);
}
