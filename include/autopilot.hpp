#pragma once
#include "simulation.hpp"
namespace iron {
// Input-only pilot for the isolated desktop mission prototype.
struct Autopilot {
    Vec3 anchor{};
    std::vector<bool> clear;
    std::vector<Vec3> path;
    int target=-1;
    float lastElapsed=-1,replan=0;
    Input update(const World&,float dt);
};
}
