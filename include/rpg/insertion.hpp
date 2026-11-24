#pragma once
#include <string>
namespace iron::rpg {
enum class InsertionKind { None = 0, Drop = 1, Warp = 2, Water = 3 };
inline constexpr float dropEntryAltitude = 300.f;
inline constexpr float dropBrakeTime = 2.6f;
inline constexpr float dropLandingTime = 3.8f;
inline constexpr float dropInsertionDuration = 5.4f;
inline constexpr float warpArrivalTime = 1.55f;
inline constexpr float warpInsertionDuration = 2.8f;
inline constexpr float waterRiseBegin = 1.f;
inline constexpr float waterRiseEnd = 3.2f;
inline constexpr float waterInsertionDuration = 5.2f;
struct InsertionSample {
  InsertionKind kind = InsertionKind::None;
  float time = 0, duration = 0;
  float altitude = 0, verticalSpeed = 0;
  float brake = 0, entry = 0, impact = 0, settle = 0;
  float warp = 0, arrivalScale = 1, materialization = 1;
  float waterDepth = 0, cameraOffsetY = 0, submersion = 0;
  float waterline = 0, wetness = 0, bubbles = 0;
  bool active = false, landed = true, controlsLocked = false;
};
// Shared deterministic timeline: physics samples it at fixed simulation steps;
// local presentation samples the replicated opener clock once per client frame.
InsertionSample sampleInsertion(InsertionKind, float seconds, float waterDepth = 2.75f);
InsertionKind insertionKindForOpener(int opener, bool enclosed = false, float waterDepth = 0);
bool insertionLandedBetween(InsertionKind, float previous, float current);
const char *insertionPhaseName(const InsertionSample &);
std::string insertionReadout(const InsertionSample &);
} // namespace iron::rpg
