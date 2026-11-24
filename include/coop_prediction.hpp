#pragma once
#include "simulation.hpp"
#include <nlohmann/json.hpp>
namespace iron::coop {
// Presentation anchors only. No World simulation, collision damage or audio events.
class MotionPrediction {
    struct Actor {Vec3 origin,center,velocity;float yaw=0,rate=0,gait=0;bool moving=false;};
    struct Person {Pilot pilot;Vec3 velocity{};std::array<float,4> rates{};};
    std::vector<Actor> actors;
    std::vector<Person> pilots;
    std::vector<Projectile> projectiles;
    std::vector<WeaponEffect> effects;
    uint64_t previousTick=0;
public:
    static constexpr float horizon=.125f,projectileHorizon=.05f;
    void reset();
    void capture(const World&,const nlohmann::json& players,uint64_t tick);
    void present(World&,float age) const;
};
}
