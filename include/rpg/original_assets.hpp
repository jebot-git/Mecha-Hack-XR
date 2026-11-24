#pragma once
#include "simulation.hpp"
#include <string_view>
namespace iron::rpg {
// Assets are native metres, Y up, facing -Z, origin at ground/grip.
std::filesystem::path gameAssetPath(const std::filesystem::path &path);
void setOriginalAssetRoot(const std::filesystem::path &root);
std::filesystem::path originalAssetRoot();
ArmRig originalArmRig();
std::string originalVariant(std::string_view base,uint32_t choice);
std::shared_ptr<WorldMesh> originalMesh(std::string_view id);
std::shared_ptr<WorldMesh> originalEnemyMesh(int catalogIndex);
void applyOriginalModel(Target &, std::string_view id);
void applyOriginalEnemy(Target &, int catalogIndex);
}
