#pragma once
#include "arms.hpp"

namespace iron::rpg {
// Cosmetic IDs carried by the existing WeaponEffect packet. They do not
// participate in collision, damage, aiming or enemy decisions.
inline constexpr unsigned rpgEnemyCharge = 0x70000200u;
inline constexpr unsigned rpgEnemyBoost = 0x70000201u;
inline constexpr unsigned rpgEnemyKinetic = 0x70000202u;
inline constexpr unsigned rpgEnemyEnergy = 0x70000203u;
inline constexpr unsigned rpgEnemyDisruption = 0x70000204u;
inline constexpr unsigned rpgEnemyMelee = 0x70000205u;
inline constexpr unsigned rpgEnemyHit = 0x70000206u;
inline constexpr unsigned rpgEnemyShieldHit = 0x70000207u;
inline constexpr unsigned rpgEnemyBossCharge = 0x70000208u;
inline constexpr unsigned rpgWeaponMuzzle = 0x70000209u;
inline constexpr unsigned rpgWeaponHeavy = 0x7000020au;
inline constexpr unsigned rpgWeaponArc = 0x7000020bu;
inline constexpr unsigned rpgWeaponPulse = 0x7000020cu;
inline constexpr unsigned rpgWeaponBarrier = 0x7000020du;
inline constexpr unsigned rpgWeaponBlast = 0x7000020eu;

inline constexpr unsigned rpgRocketIgnition = 0x7000020fu;
inline constexpr unsigned rpgArrivalDropship = 0x70000210u;
inline constexpr unsigned rpgArrivalWarp = 0x70000211u;
inline constexpr unsigned rpgArrivalBurrow = 0x70000212u;
inline constexpr unsigned rpgArrivalEdge = 0x70000213u;

inline constexpr unsigned rpgScatterMuzzle = 0x70000214u;
inline constexpr unsigned rpgPulseMuzzle = 0x70000215u;
inline constexpr unsigned rpgCutterSparks = 0x70000216u;

inline constexpr unsigned rpgVehicleVolleyWarning = 0x70000217u;
inline constexpr unsigned rpgVehicleBombWarning = 0x70000218u;

inline constexpr unsigned rpgHeavyCrossfireWarning = 0x70000219u;
inline constexpr unsigned rpgAtomicBreathWarning = 0x7000021au;
inline constexpr unsigned rpgAtomicBreath = 0x7000021bu;

inline constexpr float heavyCrossfireWindup = .68f;
inline constexpr float heavyFanOffsets[3][5] = {
    {-.24f, -.12f, 0, .12f, .24f},
    {-.30f, -.18f, -.06f, .06f, .18f},
    {-.18f, -.06f, .06f, .18f, .30f}
};
inline Vec3 patternLane(Vec3 aim, Vec3 lateral, float offset) {
    return unit(aim + lateral * offset, aim);
}

inline constexpr unsigned rpgWeatherWarning=0x7000021cu;
inline constexpr unsigned rpgWeatherPulse=0x7000021du;
inline constexpr unsigned rpgWeatherMeteor=0x7000021eu;
inline constexpr unsigned rpgWeatherSpore=0x7000021fu;
inline constexpr unsigned rpgWeatherHail=0x70000232u;
inline constexpr unsigned rpgWeatherFire=0x70000220u;
inline constexpr unsigned rpgRadarArrival=0x70000221u;
inline constexpr unsigned rpgArrivalKaijuRift=0x70000222u;
inline constexpr unsigned rpgArrivalAirstrike=0x70000223u;
inline constexpr unsigned rpgEnemyMeleeTell=0x70000224u;
inline constexpr unsigned rpgNpcMeleeSlash=0x70000225u;

inline constexpr unsigned rpgDuelGuardTell=0x70000226u;
inline constexpr unsigned rpgDuelCut=0x70000227u;

inline constexpr unsigned rpgMechaWeaveTell=0x70000230u;
inline constexpr unsigned rpgMechaRackTell=0x70000231u;
inline constexpr float enemyEffectLife(unsigned model) {
    switch (model) {
    case rpgMechaWeaveTell: return .76f;
    case rpgMechaRackTell: return 1.05f;
    case rpgEnemyCharge: return .8f;
    case rpgEnemyBoost: return .55f;
    case rpgEnemyKinetic: return .17f;
    case rpgEnemyEnergy: return .25f;
    case rpgEnemyDisruption: return .42f;
    case rpgEnemyMelee: return .24f;
    case rpgDuelGuardTell: return 1.6f;
    case rpgDuelCut: return .27f;
    case rpgNpcMeleeSlash: return .30f;
    case rpgEnemyMeleeTell: return 1.4f;
    case rpgEnemyHit: return .32f;
    case rpgEnemyShieldHit: return .32f;
    case rpgEnemyBossCharge: return 2.f;
    case rpgWeaponMuzzle: return .20f;
    case rpgWeaponHeavy: return .29f;
    case rpgWeaponArc: return .48f;
    case rpgWeaponPulse: return .66f;
    case rpgWeaponBarrier: return .62f;
    case rpgWeaponBlast: return 1.15f;
    case rpgRocketIgnition: return .45f;
    case rpgArrivalDropship: return 5.0f;
    case rpgArrivalWarp: return 2.6f;
    case rpgArrivalBurrow: return 2.7f;
    case rpgArrivalKaijuRift: return 3.05f;
    case rpgArrivalAirstrike: return 2.25f;
    case rpgArrivalEdge: return 2.0f;
    case rpgScatterMuzzle: return .22f;
    case rpgPulseMuzzle: return .32f;
    case rpgCutterSparks: return .38f;
    case rpgVehicleVolleyWarning: return .90f;
    case rpgVehicleBombWarning: return 2.8f;
    case rpgHeavyCrossfireWarning: return 2.f;
    case rpgAtomicBreathWarning: return 1.8f;
    case rpgAtomicBreath: return .195f;
    case rpgWeatherWarning: return 3.f;
    case rpgWeatherHail: return .85f;
    case rpgWeatherPulse: return .8f;
    case rpgWeatherMeteor: return 3.f;
    case rpgWeatherSpore: return 4.25f;
    case rpgWeatherFire: return 3.25f;
    case rpgRadarArrival: return 4.f;
    default: return 0;
    }
}
}
