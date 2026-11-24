#pragma once
#include <nlohmann/json.hpp>
#include <span>
#include <vector>
namespace iron::coop {
inline constexpr size_t maxStateBytes=1024*1024;
// Each call owns its coder; workers never share ENet host/compressor state.
std::vector<uint8_t> encodeSnapshotPacket(const nlohmann::json&);
nlohmann::json decodeStatePacket(std::span<const uint8_t>,size_t limit=maxStateBytes,bool allowFramed=true);
}
