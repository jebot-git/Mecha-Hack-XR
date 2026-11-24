#pragma once
#include "simulation.hpp"
#include <nlohmann/json.hpp>
namespace iron::coop {
struct SnapshotBaseline {
    nlohmann::json targets=nlohmann::json::array();
    uint64_t hash=0;
    nlohmann::json wire() const;
};
SnapshotBaseline captureBaseline(const World&);
SnapshotBaseline readBaseline(const nlohmann::json&);
nlohmann::json compactWorld(const World&,const SnapshotBaseline* baseline=nullptr);
void compactRead(const nlohmann::json&,World&,const SnapshotBaseline* baseline=nullptr);
}
