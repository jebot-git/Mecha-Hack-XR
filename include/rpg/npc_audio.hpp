#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace iron::rpg {
enum class RadioLine { FriendlyContact, FriendlySupport, FriendlyConfirm,
                       EnemyContact, EnemyAttack, EnemyDistress, Count };
inline constexpr int npcRadioBase = 256; // SoundEvent::Special payload, separate from weapon cues
inline constexpr int npcRadioFactions = 3;
inline constexpr int npcRadioVariants = 4;
inline constexpr int npcRadioBankVariants = 2;
inline constexpr int npcRadioBankSize = npcRadioFactions * int(RadioLine::Count) * npcRadioBankVariants;
inline constexpr int npcRadioClips = npcRadioFactions * int(RadioLine::Count) * npcRadioVariants;
inline constexpr float npcRadioFullRange = 35.f;
inline constexpr float npcRadioMaxRange = 180.f;
inline constexpr float npcRadioInterference(float distance) {
    const float t = std::clamp((distance - npcRadioFullRange) /
                               (npcRadioMaxRange - npcRadioFullRange), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
inline constexpr int radioCue(int faction, RadioLine line, uint32_t variant) {
    // A second bank extends the original two-take file and wire IDs.
    int take=int(variant % npcRadioVariants);
    return npcRadioBase + (take / npcRadioBankVariants) * npcRadioBankSize +
           (faction * int(RadioLine::Count) + int(line)) * npcRadioBankVariants + take % npcRadioBankVariants;
}
inline constexpr bool isNpcRadio(int cue) {
    return cue >= npcRadioBase && cue < npcRadioBase + npcRadioClips;
}

// Authoritative mission-local history, shared by all actors on this net.
struct RadioRotation {
    float ready=0;
    std::array<int,2> lastBank{-1,-1};
    int next(unsigned crew,uint32_t roll){
        int &bank=lastBank[crew];
        bank=bank<0?int(roll%2):1-bank;
        return bank*npcRadioBankVariants+int(crew);
    }
};
inline constexpr float radioRepeatInterval(RadioLine line){
    switch(line){
        case RadioLine::EnemyAttack:case RadioLine::FriendlySupport:return 65.f;
        case RadioLine::FriendlyConfirm:return 52.f;
        case RadioLine::EnemyDistress:return 28.f;
        default:return 42.f;
    }
}
}
