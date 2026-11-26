// Sample the same published rig and runtime inputs that drive a live Stalker.
#include "rpg/enemy_animation.hpp"
#include "rpg/original_assets.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace iron;
using namespace iron::rpg;
using Json = nlohmann::json;
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    const std::filesystem::path assets = argv[1];
    setOriginalAssetRoot(assets);
    const std::string id = "kaiju_stalker";
    const auto mesh = originalMesh(id);
    const auto *rig = enemyAnimationRig(assets / "models", id, mesh->size());
    if (!rig) return 1;
    Json inputs, frames = Json::array();
    std::ifstream(argv[2]) >> inputs;
    for (const auto &sample : inputs) {
        float t = sample.at("t"), d = sample.at("distance"), v = sample.at("speed");
        EnemyCombatMotion action;
        if (t >= 3.55f && t < 4.58f) action.play(EnemyAction::MeleeWindup, 3.55f, 1.03f);
        if (t >= 4.58f && t < 5.38f) action.play(EnemyAction::Strike, 4.58f, .80f);
        const auto pose = poseEnemy(*rig, std::fmod(d * (100.f/96),18.f), v, 0, 0, 0, t, action, 0, {0,0,-v});
        Json frame = Json::array();
        for (const auto &part : pose.parts) {
            const auto q=part.orientation;
            const auto p=part.offset;
            frame.push_back({q.w,q.x,q.y,q.z,p.x,p.y,p.z-d});
        }
        frames.push_back(std::move(frame));
    }
    std::ofstream(argv[3]) << frames;
}
