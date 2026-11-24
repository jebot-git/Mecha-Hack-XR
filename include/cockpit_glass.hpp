#pragma once
#include "vector.hpp"
#include <array>
#include <vector>
namespace iron {
// Coordinates follow the exported canopy-spine and overhead-arch centrelines
// in tools/build_cockpit.py (Blender X/Z/-Y -> client X/Y/Z).
struct GlassVertex {Vec3 position,normal;std::array<float,2> uv,metres;};
struct GlassPane {unsigned first,count;};
struct CockpitGlazing {std::vector<GlassVertex> vertices;std::vector<GlassPane> panes;};
CockpitGlazing cockpitGlazing();
// Project damage onto the actual triangulated front surface, slightly inside.
Vec3 cockpitFrontGlass(float x,float y);
}
