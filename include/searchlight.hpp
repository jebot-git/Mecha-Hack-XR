#pragma once
#include "vector.hpp"
namespace iron {
struct World;struct Pilot;
struct VisibilityRanges {float base=250,lit=250;};
struct SearchlightBeam {Vec3 origin{},direction{},end{},normal{0,1,0};float length=0;bool hit=false;};
VisibilityRanges visibilityRanges(const World&);
Vec3 searchlightOrigin(const Pilot&);
Vec3 searchlightDirection(const Pilot&);
bool searchlightActive(const Pilot&);
bool searchlightCovers(const World&,const Pilot&,Vec3);
bool visibilityInRange(const World&,const Pilot&,Vec3,bool revealed=false);
SearchlightBeam traceSearchlight(const World&,const Pilot&);
}
