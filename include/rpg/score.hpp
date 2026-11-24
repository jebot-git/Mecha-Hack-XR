#pragma once
#include <array>
#include <string_view>

namespace iron::rpg {
enum class ScoreCue {
    Main, Intermission, Aeonic, Terran, Neo, Voidmaw,
    AeonicBrief, TerranBrief, NeoBrief, VoidmawBrief,
    Boss, Finale, Victory, Clear, Defeat, Count
};
inline constexpr std::array<std::string_view, static_cast<int>(ScoreCue::Count)> scoreFiles{
    "main","intermission","aeonic","terran","neo","voidmaw",
    "aeonic-brief","terran-brief","neo-brief","voidmaw-brief",
    "boss","finale","victory","clear","defeat"
};
inline ScoreCue factionScore(int faction, bool briefing) {
    if(faction<0||faction>3)return ScoreCue::Intermission;
    return ScoreCue(int(briefing?ScoreCue::AeonicBrief:ScoreCue::Aeonic)+faction);
}
inline bool loopingScore(ScoreCue cue){return cue<ScoreCue::Victory;}
}
