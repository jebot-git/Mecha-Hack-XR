#pragma once
#include "simulation.hpp"

namespace iron::rpg {
enum class WeaponSoundFamily { Auto, Cannon, Missile, Energy, Melee, Count };
constexpr WeaponSoundFamily weaponSoundFamily(Weapon weapon) {
    switch(weapon) {
    case Weapon::Rifle: case Weapon::Gatling: return WeaponSoundFamily::Auto;
    case Weapon::HeavyMachinegun: case Weapon::Shotgun: case Weapon::Grenade:
    case Weapon::AutoGrenade: return WeaponSoundFamily::Cannon;
    case Weapon::Rocket: case Weapon::Homing: case Weapon::Cruise: return WeaponSoundFamily::Missile;
    case Weapon::Rail: return WeaponSoundFamily::Energy;
    case Weapon::Cutter: return WeaponSoundFamily::Melee;
    default: return WeaponSoundFamily::Count;
    }
}
enum class CombatCue { Rifle, Cannon, Beam, Rocket, Charge, Arc, Pulse, Shield,
                       Boost, MetalHit, Melee, Explosion, HeavyExplosion, Step,
                       Motor, Repair, Ready, Failed, EnemyStep, EnemyAlert, EnemyGrowl, EnemyThrust, KaijuShriek, KaijuRoar, KaijuSonar, ArrivalWarp, ArrivalDropship, ArrivalBurrow,
                       KaijuMandibles, KaijuHiss, KaijuBite, KaijuSwipe, KaijuImpact, KaijuDischarge,
                       KaijuSpray, KaijuBurrow, KaijuSkitter, KaijuTread, KaijuSpines, Count };
}
