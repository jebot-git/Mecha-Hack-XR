#pragma once
#include "voice_wire.hpp"
#include <array>
#include <functional>

namespace avp::voice {
// Lobby-global team radio. Microphone activation is driven exclusively by audio
// activity; tracking/gesture arguments are retained from the AvP interface.
class Controls {
    bool radio = false;

  public:
    Channel channel = Channel::Radio;
    bool captureAllowed = false;
    bool malfunction = false;
    void update(bool allowed, bool headValid, Vec3 head, const std::array<Vec3, 2> &hands,
                const std::array<bool, 2> &valid, const std::array<bool, 2> &grip,
                bool desktopRadio = false);
};
// Native 20ms voice activity gate, with adaptive quiet-floor and release hysteresis.
class ActivityGate {
    float noise = .0006f, previous = 0, high = 0, low = 0;
    int hangover = 0;

  public:
    bool process(std::span<const float>);
    void reset() {
        noise = .0006f;
        previous = high = low = 0;
        hangover = 0;
    }
};
class RadioFilter {
    struct Biquad {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float sample(float);
    } high, low;

  public:
    RadioFilter();
    float sample(float);
};
class SpeechCodec {
    struct Impl;
    std::unique_ptr<Impl> p;

  public:
    SpeechCodec();
    ~SpeechCodec();
    std::vector<uint8_t> encode(std::span<const float>);
    std::array<float, frameSamples> decode(std::span<const uint8_t> = {});
    void reset();
};
// Per-speaker bounded reorder/jitter buffers. The same mixer is used by SDL
// playback and deterministic tests; now is a monotonic time in seconds.
class Playback {
    struct Impl;
    std::unique_ptr<Impl> p;

  public:
    Playback();
    ~Playback();
    void receive(Packet, double now);
    // Local radio connection feedback follows the gesture, independently of VAD.
    void setLocalRadio(bool engaged);
    void mix(float *stereo960, Vec3 listener, float yaw, double now, float volume);
    void retain(std::span<const Participant>);
    void clear();
    nlohmann::json report() const;
};
class Chat {
    struct Impl;
    std::unique_ptr<Impl> p;

  public:
    struct Output {
        std::function<size_t()> queuedBytes;
        std::function<void(std::span<const float>)> queue;
        std::function<void()> clear;
    };
    explicit Chat(bool devices = true, const std::string &inputDevice = {});
    ~Chat();
    // Set before configure; callbacks outlive Chat and use an existing game output.
    void shareOutput(Output);
    void configure(const std::string &endpoint, const nlohmann::json &credentials);
    void update(uint64_t epoch, uint32_t localId, std::span<const Participant> players,
                Vec3 listener, float yaw, bool allowed, bool micEnabled, const Controls &,
                float volume);
    void close();
    bool transmitting() const;
    std::string status() const;
    nlohmann::json report() const;
};
} // namespace avp::voice
