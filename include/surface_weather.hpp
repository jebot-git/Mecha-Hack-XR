#pragma once
#include "environment.hpp"
#include <algorithm>
#include <array>

namespace iron {
// Fractions of exposed surface carrying windblown sand, snow, ash, scorched
// soil, spore residue and moisture. Fronts last at least thirty seconds.
using SurfaceWeather = std::array<float,6>;
inline SurfaceWeather surfaceWeather(Weather previous,Weather current,float seconds) {
    auto deposits=[](Weather kind) -> SurfaceWeather {
        switch(kind){
        case Weather::Sandstorm: return {1,0,0,0,0,0};
        case Weather::Snowstorm: return {0,1,0,0,0,0};
        case Weather::AshCloud: return {0,0,.92f,0,0,0};
        case Weather::FireStorm: return {0,0,.28f,.72f,0,0};
        case Weather::CorruptionSporeBurst: return {0,0,0,0,.82f,0};
        case Weather::Storm: return {0,0,0,0,0,.94f};
        case Weather::Rain: return {0,0,0,0,0,.72f};
        default: return {};
        }
    };
    auto start=deposits(previous);
    const auto end=deposits(current);
    const float t=std::clamp(seconds/30.f,0.f,1.f);
    const float blend=t*t*(3.f-2.f*t);
    for(size_t i=0;i<start.size();++i)start[i]=std::lerp(start[i],end[i],blend);
    return start;
}
}
