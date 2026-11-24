#pragma once
#include <nlohmann/json.hpp>
namespace iron::rpg {
// Remove infrequently changing fields from a single sparse snapshot and return
// their reliable baseline. Each remaining frame is independent of earlier frames.
nlohmann::json packRpgSnapshot(nlohmann::json& state);
void unpackRpgSnapshot(nlohmann::json& state,const nlohmann::json& baseline);
}
