#pragma once
#include <cstdint>
namespace iron {
// Disabled Mecha d8 results. These bits are part of the RPG snapshot contract.
enum class MechaInjury : uint32_t {
    Sensors=1, LostArm=2, Hobbled=4, Targeting=8,
    Comms=16, Severe=32, ModuleDestroyed=64, Destroyed=128
};
constexpr bool hasInjury(uint32_t flags,MechaInjury injury){return (flags&uint32_t(injury))!=0;}
constexpr bool injuryDisadvantage(uint32_t flags,int attribute,bool attack=false){
    return hasInjury(flags,MechaInjury::Severe)||
        (attribute==1&&hasInjury(flags,MechaInjury::Hobbled))||
        (attribute==2&&hasInjury(flags,MechaInjury::Sensors))||
        (attack&&hasInjury(flags,MechaInjury::Targeting));
}
constexpr float injuryMovementScale(uint32_t flags){return hasInjury(flags,MechaInjury::Hobbled)?.5f:1.f;}
}
