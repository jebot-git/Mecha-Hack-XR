#pragma once
#include "simulation.hpp"
#include <array>
namespace iron {
struct AssaultScenario { const char* name; int game,mission;Climate climate=Climate::Temperate; };
inline constexpr std::array<AssaultScenario,7> assaultScenarios{{
    {"IS1 bridge supports",0,2},{"IS1 factory complex",0,10},
    {"IS2 desert factories",1,0,Climate::Desert},{"IS2 military base",1,14},
    {"IS2 island buildings",1,1,Climate::Coastal},{"IS2 power plants",1,11},
    {"IS2 island factories",1,17,Climate::Coastal}
}};
enum class AssaultUnitRole { Ground, Air, Turret, Naval, Utility };
struct AssaultUnitType { int type; const char* name; AssaultUnitRole role; };
// IS2's actor roster. Generator (4), pickup emitter (21), and attached trailer
// (23) are lifecycle records, rather than independently deployable units.
inline constexpr std::array<AssaultUnitType,21> assaultUnitRoster{{
    {0,"Tank",AssaultUnitRole::Ground},{1,"Helicopter",AssaultUnitRole::Air},
    {2,"Gun turret",AssaultUnitRole::Turret},{3,"Enemy mech",AssaultUnitRole::Ground},
    {5,"Gun emplacement",AssaultUnitRole::Turret},{6,"Transport",AssaultUnitRole::Ground},
    {7,"Bomber",AssaultUnitRole::Air},{8,"Truck",AssaultUnitRole::Ground},
    {9,"Ship",AssaultUnitRole::Naval},{10,"Heavy turret",AssaultUnitRole::Turret},
    {11,"Mobile missile battery",AssaultUnitRole::Ground},{12,"Cargo carrier",AssaultUnitRole::Air},
    {13,"Static actor 13",AssaultUnitRole::Utility},{14,"Satyr",AssaultUnitRole::Ground},
    {15,"Vehicle 15",AssaultUnitRole::Ground},{16,"Static actor 16",AssaultUnitRole::Utility},
    {17,"Car bomb",AssaultUnitRole::Ground},{18,"Fighter",AssaultUnitRole::Air},
    {19,"Cold Iron Gunner",AssaultUnitRole::Ground},{20,"Transport pad",AssaultUnitRole::Utility},
    {22,"Speedboat",AssaultUnitRole::Naval}
}};
bool validAssaultLoadout(const Loadout&);
struct AssaultRules {
    EnvironmentSettings environment;
    unsigned attackerRespawns=12; // shared team pool; initial deployment is free
    float attackerDelay=8,defenderDelay=20,roundSeconds=600;
    unsigned attackerAir=6,attackerGround=2,defenderTurrets=4,defenderGround=4;
    // Optional explicit finite rosters; empty uses mixed defaults above.
    std::vector<int> attackerUnits,defenderUnits;
};
struct AssaultPilot { Pilot pilot; Input input; Loadout loadout{}; float respawnIn=0; bool waiting=false,deathHandled=false; unsigned deaths=0; };
enum class AssaultResult { Playing, AttackersWin, DefendersWin };
// Local authoritative prototype. Eight inputs can be driven by a future transport.
// No campaign progression writes; existing mission callbacks decide objective victory.
class AssaultMatch {
public:
    World world;
    std::array<AssaultPilot,8> pilots;
    AssaultRules rules;
    AssaultResult result=AssaultResult::Playing;
    unsigned respawnsRemaining=0;
    float clock=0;
    void load(const std::filesystem::path& retail,unsigned scenario,const AssaultRules& = {});
    // Also useful for synthetic maps and deterministic rule tests.
    void prepare(World loaded,const AssaultRules& = {});
    void step(float dt);
    void setLoadout(unsigned slot,const Loadout&);
private:
    std::array<Vec3,8> starts{};
    std::array<float,8> yaws{};
    void deploy(size_t);
    void links(size_t current=8);
};
}
