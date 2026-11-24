#pragma once
#include "vector.hpp"
#include "matrix.hpp"
#include <memory>
#include <span>
#include <vector>
#include <cstdint>
namespace iron {
struct AudioListener {
    Vec3 position{},right{1,0,0},up{0,1,0},back{0,0,1};
    static AudioListener fromPose(const avp::Matrix& p){return {{p[12],p[13],p[14]},{p[0],p[1],p[2]},{p[4],p[5],p[6]},{p[8],p[9],p[10]}};}
};
// World SFX and intercepted NPC transmitters. A single control thread prepares filters; the audio callback
// renders them. The host serializes these operations with its audio-device lock.
// Samples are immutable and must outlive their playback/tail. No callback I/O,
// allocation, dataset lookup, or dependence on render-frame duration.
class SpatialAudio {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    static constexpr unsigned sourceLimit=24,filterTailFrames=512;
    enum class Priority { Background, Normal, Foreground };
    struct Stats {uint64_t frames=0,queries=0,started=0,stolen=0,dropped=0;unsigned active=0;};
    SpatialAudio();
    ~SpatialAudio();
    SpatialAudio(const SpatialAudio&)=delete;
    SpatialAudio& operator=(const SpatialAudio&)=delete;
    void listener(const AudioListener&);
    void play(const std::vector<float>& mono,Vec3 position,float gain=1,float rate=1,
              Priority priority=Priority::Normal);
    struct Loop {size_t id; const std::vector<float>* samples; Vec3 position; float gain; float rate=1;};
    static constexpr unsigned loopLimit=6;
    void loops(std::span<const Loop>);
    // One directional NPC receiver, independent of world voice stealing. Its
    // caller supplies mono PCM with reception noise and squad-chat ducking.
    void radioEmitter(Vec3 position,bool newTransmission);
    void mixRadioAdd(std::span<const float> mono,std::span<float> stereo);
    void clear();
    void mixAdd(std::span<float> stereo,float volume=1);
    Stats statistics()const;
};
}
