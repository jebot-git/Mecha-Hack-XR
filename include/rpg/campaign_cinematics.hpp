#pragma once
#include "foldout.hpp"
#include <optional>
#include <set>
#include <string>

namespace iron::rpg {
class Session;
struct CampaignCinematic {
    std::string key;
    FoldoutFrame frame;
};
// Builds an earned presentation from authoritative state. Intermediate sectors,
// failure, custom missions and unreconciled snapshots cannot invent an outcome.
std::optional<CampaignCinematic> campaignCinematic(const Session &);
// Local viewing state only. Neither finishing nor dismissing issues a gameplay
// command. Keys survive ready/unready/retry transitions until reset().
class CampaignCinematics {
    std::set<std::string> presented;
    std::string key;
    FoldoutFrame current;
public:
    FoldoutFrame update(const Session &,float dt,bool focused,bool dismissPressed);
    bool active()const{return current.active;}
    void cancel(){key.clear();current=FoldoutFrame{};}
    void reset(){presented.clear();key.clear();current=FoldoutFrame{};}
};
} // namespace iron::rpg
