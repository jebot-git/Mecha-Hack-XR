#include "voice.hpp"
#include "radio_damage.hpp"
#include <SDL.h>
#include <opus.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <deque>
#include <stdexcept>

namespace avp::voice {
using Json = nlohmann::json;
namespace {
constexpr float pi = 3.14159265358979323846f;
double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
float separation(Vec3 a, Vec3 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}
} // namespace
void Controls::update(bool allowed, bool headValid, Vec3 head, const std::array<Vec3, 2> &hands,
                      const std::array<bool, 2> &valid, const std::array<bool, 2> &grip,
                      bool desktopRadio) {
    (void)headValid;(void)head;(void)hands;(void)valid;(void)grip;(void)desktopRadio;
    radio=allowed;channel=Channel::Radio;captureAllowed=allowed;
}
bool ActivityGate::process(std::span<const float> samples) {
    double energy = 0;
    for (float value : samples) {
        if (!std::isfinite(value))
            value = 0;
        high = .9896f * (high + value - previous);
        previous = value;
        low += .4076f * (high - low);
        energy += low * low;
    }
    float rms = std::sqrt(energy / std::max(size_t(1), samples.size()));
    float threshold = std::max(.008f, noise * 3.2f);
    if (rms > threshold * (hangover > 0 ? .6f : 1.f))
        hangover = 12;
    else if (hangover > 0)
        --hangover;
    else
        noise += .04f * (rms - noise);
    return hangover > 0;
}
float RadioFilter::Biquad::sample(float in) {
    float out = b0 * in + z1;
    z1 = b1 * in - a1 * out + z2;
    z2 = b2 * in - a2 * out;
    return out;
}
RadioFilter::RadioFilter() {
    auto setup = [](Biquad &b, float cutoff, bool highpass) {
        float w = 2 * pi * cutoff / sampleRate, c = std::cos(w),
              alpha = std::sin(w) / std::sqrt(2.f);
        float a0 = 1 + alpha;
        b.b0 = (highpass ? 1 + c : 1 - c) / (2 * a0);
        b.b1 = (highpass ? -(1 + c) : 1 - c) / a0;
        b.b2 = b.b0;
        b.a1 = -2 * c / a0;
        b.a2 = (1 - alpha) / a0;
    };
    setup(high, 320, true);
    setup(low, 3500, false);
}
float RadioFilter::sample(float in) {
    // CQ radio: 320 Hz high-pass, gentle waveshaping and +4 dB, then 3.5 kHz low-pass.
    return low.sample(std::tanh(high.sample(in) * 1.8f) / std::tanh(1.8f) * 1.5848932f);
}
struct SpeechCodec::Impl {
    OpusEncoder *encoder = nullptr;
    OpusDecoder *decoder = nullptr;
    ~Impl() {
        if (encoder)
            opus_encoder_destroy(encoder);
        if (decoder)
            opus_decoder_destroy(decoder);
    }
};
SpeechCodec::SpeechCodec() : p(std::make_unique<Impl>()) {}
SpeechCodec::~SpeechCodec() = default;
std::vector<uint8_t> SpeechCodec::encode(std::span<const float> samples) {
    if (samples.size() != frameSamples)
        throw std::runtime_error("Voice capture must contain 20 ms mono");
    if (!p->encoder) {
        int error;
        p->encoder = opus_encoder_create(sampleRate, 1, OPUS_APPLICATION_VOIP, &error);
        if (error != OPUS_OK || !p->encoder)
            throw std::runtime_error("Opus encoder unavailable");
        opus_encoder_ctl(p->encoder, OPUS_SET_BITRATE(bitrate));
        opus_encoder_ctl(p->encoder, OPUS_SET_COMPLEXITY(5));
        opus_encoder_ctl(p->encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(p->encoder, OPUS_SET_DTX(0));
    }
    std::vector<uint8_t> packet(maxPacket);
    int n = opus_encode_float(p->encoder, samples.data(), frameSamples, packet.data(),
                              int(packet.size()));
    if (n < 0)
        throw std::runtime_error("Opus encode failed");
    packet.resize(n);
    if (!validOpus(packet))
        throw std::runtime_error("Unexpected Opus frame configuration");
    return packet;
}
std::array<float, frameSamples> SpeechCodec::decode(std::span<const uint8_t> packet) {
    if (!packet.empty() &&
        (!validOpus(packet) ||
         opus_packet_get_nb_samples(packet.data(), int(packet.size()), sampleRate) != frameSamples))
        throw std::runtime_error("Invalid 20 ms voice packet");
    if (!p->decoder) {
        int error;
        p->decoder = opus_decoder_create(sampleRate, 1, &error);
        if (error != OPUS_OK || !p->decoder)
            throw std::runtime_error("Opus decoder unavailable");
    }
    std::array<float, frameSamples> samples{};
    int n = opus_decode_float(p->decoder, packet.empty() ? nullptr : packet.data(),
                              int(packet.size()), samples.data(), frameSamples, 0);
    if (n != frameSamples)
        throw std::runtime_error("Opus decode failed");
    return samples;
}
void SpeechCodec::reset() { p = std::make_unique<Impl>(); }
struct Playback::Impl {
    struct Stream {
        SpeechCodec codec;
        RadioFilter filter;
        std::map<uint32_t, Packet> queue;
        uint32_t last = 0, next = 0;
        Channel channel = Channel::Proximity;
        Vec3 position;
        double start = 0, received = 0;
        int losses = 0;
        bool playing = false;
    };
    std::map<uint32_t, std::unique_ptr<Stream>> streams;
    std::map<uint32_t, uint32_t> lastSeen;
    uint64_t received = 0, decoded = 0, concealed = 0, discarded = 0, radioFrames = 0,
             proximityFrames = 0;
    struct Cue {
        int remaining = 0;
        bool end = false;
        uint32_t noise = 4242;
    };
    std::vector<Cue> cues;
    bool localRadio = false;
    uint64_t localRadioEngages = 0, localRadioDisengages = 0;
    void cue(bool end) {
        if (cues.size() < 8)
            cues.push_back({5760, end, 4242});
    }
};
Playback::Playback() : p(std::make_unique<Impl>()) {}
Playback::~Playback() = default;
void Playback::setLocalRadio(bool engaged) {
    if (engaged == p->localRadio)
        return;
    p->localRadio = engaged;
    if (engaged)
        ++p->localRadioEngages;
    else
        ++p->localRadioDisengages;
    p->cue(!engaged);
}
void Playback::receive(Packet packet, double now) {
    if (!packet.sender || !packet.sequence || uint8_t(packet.channel) > 1 ||
        !validOpus(packet.opus) || !std::isfinite(now) || !std::isfinite(packet.position.x) ||
        !std::isfinite(packet.position.z)) {
        ++p->discarded;
        return;
    }
    auto &entry = p->streams[packet.sender];
    if (!entry && packet.sequence <= p->lastSeen[packet.sender]) {
        p->streams.erase(packet.sender);
        ++p->discarded;
        return;
    }
    if (entry && packet.channel != entry->channel && packet.sequence <= entry->last) {
        ++p->discarded;
        return;
    }
    if (entry && (packet.channel != entry->channel || now - entry->received > .16 ||
                  uint64_t(packet.sequence) > uint64_t(entry->last) + 50)) {
        if (entry->channel == Channel::Radio)
            p->cue(true);
        entry.reset();
    }
    if (!entry) {
        if (p->streams.size() > 32) {
            p->streams.erase(packet.sender);
            ++p->discarded;
            return;
        }
        entry = std::make_unique<Impl::Stream>();
        entry->channel = packet.channel;
        entry->start = now + .06;
        if (packet.channel == Channel::Radio)
            p->cue(false);
    }
    auto &s = *entry;
    if ((s.playing && packet.sequence < s.next) || s.queue.contains(packet.sequence) ||
        (s.last >= packet.sequence && s.last - packet.sequence >= 32)) {
        ++p->discarded;
        return;
    }
    if (s.queue.size() >= 16) {
        ++p->discarded;
        return;
    }
    s.last = std::max(s.last, packet.sequence);
    p->lastSeen[packet.sender] = std::max(p->lastSeen[packet.sender], packet.sequence);
    s.received = now;
    s.position = packet.position;
    s.queue.emplace(packet.sequence, std::move(packet));
    ++p->received;
}
void Playback::mix(float *out, Vec3 listener, float yaw, double now, float volume) {
    std::fill(out, out + frameSamples * 2, 0.f);
    volume = std::isfinite(volume) ? std::clamp(volume, 0.f, 1.f) : 0;
    for (auto it = p->streams.begin(); it != p->streams.end();) {
        auto &s = *it->second;
        if (now - s.received > .16) {
            if (s.channel == Channel::Radio)
                p->cue(true);
            it = p->streams.erase(it);
            continue;
        }
        ++it;
        if (now < s.start)
            continue;
        if (!s.playing) {
            if (s.queue.empty())
                continue;
            s.next = s.queue.begin()->first;
            s.playing = true;
        }
        std::array<float, frameSamples> pcm{};
        auto found = s.queue.find(s.next);
        try {
            if (found != s.queue.end()) {
                pcm = s.codec.decode(found->second.opus);
                s.queue.erase(found);
                s.losses = 0;
                ++p->decoded;
            } else if (!s.queue.empty() && ++s.losses <= 3) {
                pcm = s.codec.decode();
                ++p->concealed;
            } else {
                if (!s.queue.empty())
                    s.next = s.queue.begin()->first;
                continue;
            }
        } catch (const std::exception &) {
            ++p->discarded;
            if (found != s.queue.end())
                s.queue.erase(found);
        }
        ++s.next;
        float left = .70710678f, right = left;
        if (s.channel == Channel::Proximity) {
            float dx = (s.position.x - listener.x) * metresPerCell,
                  dz = (s.position.z - listener.z) * metresPerCell;
            float distance = std::hypot(dx, dz),
                  pan = distance > .001f
                            ? std::clamp((dx * std::cos(yaw) + dz * std::sin(yaw)) / distance, -1.f,
                                         1.f)
                            : 0;
            float gain =
                8.f / std::max(8.f, distance) *
                std::max(0.f, 1 - distance * distance / (proximityMetres * proximityMetres));
            left = std::sqrt((1 - pan) * .5f) * gain;
            right = std::sqrt((1 + pan) * .5f) * gain;
            ++p->proximityFrames;
        } else
            ++p->radioFrames;
        for (int i = 0; i < frameSamples; ++i) {
            float v = s.channel == Channel::Radio ? s.filter.sample(pcm[i]) : pcm[i];
            out[2 * i] += v * left * volume;
            out[2 * i + 1] += v * right * volume;
        }
    }
    for (auto &cue : p->cues)
        for (int i = 0; i < frameSamples && cue.remaining; ++i, --cue.remaining) {
            float t = (5760 - cue.remaining) / float(sampleRate);
            cue.noise = cue.noise * 1664525 + 1013904223;
            float noise = (float(cue.noise >> 8) / 16777215.f - .5f) * .16f;
            float value = (std::sin(t * 2 * pi * (cue.end ? 740 : 1120)) * .16f + noise) *
                          std::min(t / .005f, 1.f) * std::max(0.f, 1 - t / .12f) * volume;
            out[2 * i] += value;
            out[2 * i + 1] += value;
        }
    std::erase_if(p->cues, [](const auto &cue) { return !cue.remaining; });
    for (int i = 0; i < frameSamples * 2; ++i)
        out[i] = std::clamp(out[i], -1.f, 1.f);
}
void Playback::retain(std::span<const Participant> players) {
    std::erase_if(p->streams, [&](const auto &entry) {
        return std::none_of(players.begin(), players.end(),
                            [&](const auto &v) { return v.id == entry.first && v.active; });
    });
    std::erase_if(p->lastSeen, [&](const auto &entry) {
        return std::none_of(players.begin(), players.end(),
                            [&](const auto &v) { return v.id == entry.first; });
    });
}
void Playback::clear() {
    p->streams.clear();
    p->lastSeen.clear();
    p->cues.clear();
    p->localRadio = false;
}
Json Playback::report() const {
    return {{"received", p->received},
            {"decoded", p->decoded},
            {"concealed", p->concealed},
            {"discarded", p->discarded},
            {"radio_frames", p->radioFrames},
            {"proximity_frames", p->proximityFrames},
            {"streams", p->streams.size()},
            {"local_radio_engaged", p->localRadio},
            {"local_radio_engages", p->localRadioEngages},
            {"local_radio_disengages", p->localRadioDisengages}};
}
struct Chat::Impl {
    Transport transport;
    SpeechCodec encoder;
    Playback playback;
    RadioDamage transmitDamage,receiveDamage;
    bool malfunction=false;
    SDL_AudioDeviceID capture = 0, output = 0;
    Chat::Output shared;
    bool outputOpen() const { return output || bool(shared.queue); }
    size_t outputQueued() const {
        return shared.queue ? shared.queuedBytes() : SDL_GetQueuedAudioSize(output);
    }
    void clearOutput() {
        if (shared.queue)
            shared.clear();
        else if (output)
            SDL_ClearQueuedAudio(output);
    }
    void queueOutput(std::span<const float> samples) {
        if (shared.queue)
            shared.queue(samples);
        else
            SDL_QueueAudio(output, samples.data(), Uint32(samples.size_bytes()));
    }
    SDL_AudioSpec captureSpec{};
    SDL_AudioStream *resampler = nullptr;
    bool devices = true, configured = false, transmitting = false, audioInitialized = false,
         captureAttempted = false;
    Channel channel = Channel::Proximity;
    std::string inputDevice, message = "Voice: not connected";
    uint64_t epoch = 0, encoded = 0, droppedCaptureBytes = 0;
    bool captureEnabledLast = true, capturing = false;
    ActivityGate activity;
    std::deque<std::array<float, frameSamples>> history, pending;
    uint64_t voicedFrames = 0, silentFrames = 0;
    ~Impl() {
        if (capture)
            SDL_CloseAudioDevice(capture);
        if (output)
            SDL_CloseAudioDevice(output);
        if (resampler)
            SDL_FreeAudioStream(resampler);
        if (audioInitialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    void openOutput() {
        if (outputOpen() || !devices)
            return;
        if (!audioInitialized) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
                throw std::runtime_error(SDL_GetError());
            audioInitialized = true;
        }
        SDL_AudioSpec spec{};
        spec.freq = sampleRate;
        spec.format = AUDIO_F32SYS;
        spec.channels = 2;
        spec.samples = 960;
        output = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
        if (!output)
            throw std::runtime_error(std::string("Voice playback: ") + SDL_GetError());
        SDL_PauseAudioDevice(output, 0);
    }
    void openCapture() {
        if (capture || !devices || captureAttempted)
            return;
        captureAttempted = true;
        if (!audioInitialized) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
                throw std::runtime_error(SDL_GetError());
            audioInitialized = true;
        }
        SDL_AudioSpec spec{};
        spec.freq = sampleRate;
        spec.format = AUDIO_F32SYS;
        spec.channels = 1;
        spec.samples = 960;
        capture = SDL_OpenAudioDevice(inputDevice.empty() ? nullptr : inputDevice.c_str(), 1, &spec,
                                      &captureSpec, SDL_AUDIO_ALLOW_ANY_CHANGE);
        if (!capture)
            throw std::runtime_error(std::string("Microphone unavailable: ") + SDL_GetError());
        resampler = SDL_NewAudioStream(captureSpec.format, captureSpec.channels, captureSpec.freq,
                                       AUDIO_F32SYS, 1, sampleRate);
        if (!resampler) {
            SDL_CloseAudioDevice(capture);
            capture = 0;
            throw std::runtime_error(SDL_GetError());
        }
    }
    void stopCapture(bool close = false) {
        if (capture) {
            SDL_PauseAudioDevice(capture, 1);
            SDL_ClearQueuedAudio(capture);
        }
        if (resampler)
            SDL_AudioStreamClear(resampler);
        transmitting = capturing = false;
        activity.reset();
        history.clear();
        pending.clear();
        if (close) {
            if (capture)
                SDL_CloseAudioDevice(capture);
            if (resampler)
                SDL_FreeAudioStream(resampler);
            capture = 0;
            resampler = nullptr;
            captureAttempted = false;
        }
    }
};
Chat::Chat(bool devices, const std::string &inputDevice) : p(std::make_unique<Impl>()) {
    p->devices = devices;
    p->inputDevice = inputDevice;
}
Chat::~Chat() = default;
void Chat::shareOutput(Output output) {
    if (p->configured || !output.queue || !output.queuedBytes || !output.clear)
        throw std::logic_error("Set a complete shared voice output before connecting");
    p->shared = std::move(output);
}
void Chat::configure(const std::string &endpoint, const Json &credentials) {
    if (p->configured)
        return;
    p->configured = true;
    if (!p->devices) {
        p->message = "Voice disabled (--no-sound)";
        return;
    }
    try {
        if (credentials.is_null()) {
            p->message = "Server has no voice support";
            return;
        }
        p->transport.connect(net::parseEndpoint(endpoint).host, credentials);
        p->openOutput();
        p->message = "Voice: connecting UDP";
    } catch (const std::exception &e) {
        p->message = e.what();
    }
}
void Chat::update(uint64_t epoch, uint32_t localId, std::span<const Participant> players,
                  Vec3 listener, float yaw, bool allowed, bool micEnabled, const Controls &controls,
                  float volume) {
    if (!p->configured || !p->devices || !p->outputOpen())
        return;
    if(controls.malfunction!=p->malfunction){
        p->malfunction=controls.malfunction;p->clearOutput();
        p->transmitDamage.reset();p->receiveDamage.reset();
    }
    if (epoch != p->epoch) {
        p->epoch = epoch;
        p->playback.clear();
        p->stopCapture();
        p->encoder.reset();
        p->transmitDamage.reset();p->receiveDamage.reset();
        p->clearOutput();
    }
    if (micEnabled != p->captureEnabledLast) {
        p->captureAttempted = false;
        p->captureEnabledLast = micEnabled;
    }
    double now = seconds();
    auto packets = p->transport.poll(epoch);
    if (!allowed) {
        p->stopCapture(!micEnabled);
        p->playback.clear();
        p->clearOutput();
        p->message = micEnabled ? "Voice: paused" : "Voice: listening only";
        return;
    }
    p->playback.retain(players);
    for (auto &packet : packets)
        if (packet.sender != localId &&
            std::any_of(players.begin(), players.end(),
                        [&](const auto &v) { return v.id == packet.sender && v.active; }))
            p->playback.receive(std::move(packet), now);
    bool talk = micEnabled && controls.captureAllowed && p->transport.ready();
    if (!talk)
        p->stopCapture(!micEnabled);
    else {
        try {
            p->openCapture();
            if (p->capture && (!p->capturing || p->channel != controls.channel)) {
                p->stopCapture();
                p->encoder.reset();
                p->channel = controls.channel;
                SDL_PauseAudioDevice(p->capture, 0);
                p->capturing = true;
            }
            if (p->capture && p->capturing) {
                int bytesPerFrame =
                    SDL_AUDIO_BITSIZE(p->captureSpec.format) / 8 * p->captureSpec.channels;
                int chunkBytes = std::max(1, p->captureSpec.freq / 50) * bytesPerFrame;
                std::vector<uint8_t> raw(chunkBytes);
                while (SDL_GetQueuedAudioSize(p->capture) > uint32_t(chunkBytes * 4))
                    p->droppedCaptureBytes +=
                        SDL_DequeueAudio(p->capture, raw.data(), uint32_t(raw.size()));
                // At most60 ms of encoding per display frame, never a backlog.
                auto started = std::chrono::steady_clock::now();
                for (int n = 0; n < 3; ++n) {
                    if (n &&
                        std::chrono::steady_clock::now() - started > std::chrono::milliseconds(1))
                        break;
                    if (SDL_AudioStreamAvailable(p->resampler) <
                        int(frameSamples * sizeof(float))) {
                        auto count = SDL_DequeueAudio(p->capture, raw.data(), uint32_t(raw.size()));
                        if (count)
                            SDL_AudioStreamPut(p->resampler, raw.data(), int(count));
                    }
                    if (SDL_AudioStreamAvailable(p->resampler) < int(frameSamples * sizeof(float)))
                        break;
                    std::array<float, frameSamples> pcm{};
                    if (SDL_AudioStreamGet(p->resampler, pcm.data(), sizeof(pcm)) != sizeof(pcm))
                        break;
                    bool active = p->activity.process(pcm);
                    if (active) {
                        ++p->voicedFrames;
                        if (!p->transmitting) {
                            p->pending.insert(p->pending.end(), p->history.begin(),
                                              p->history.end());
                            p->history.clear();
                        }
                        p->pending.push_back(pcm);
                    } else {
                        ++p->silentFrames;
                        p->history.push_back(pcm);
                        while (p->history.size() > 4)
                            p->history.pop_front();
                    }
                    p->transmitting = active;
                    while (p->pending.size() > 12)
                        p->pending.pop_front();
                }
                for (int n = 0; n < 3 && !p->pending.empty(); ++n) {
                    p->transmitDamage.process(p->pending.front(),1,controls.malfunction);
                    auto packet = p->encoder.encode(p->pending.front());
                    p->pending.pop_front();
                    if (p->transport.send(p->channel, epoch, packet))
                        ++p->encoded;
                }
            }
        } catch (const std::exception &e) {
            p->stopCapture();
            p->message = e.what();
        }
    }
    p->playback.setLocalRadio(talk && p->capturing && p->channel == Channel::Radio);
    for (int n = 0; n < 3; ++n) {
        auto queued = p->outputQueued();
        if (queued >= frameSamples * 2 * sizeof(float) * 2)
            break;
        std::array<float, frameSamples * 2> stereo{};
        p->playback.mix(stereo.data(), listener, yaw,
                        now + double(queued) / (sampleRate * 2 * sizeof(float)), volume);
        p->receiveDamage.process(stereo,2,controls.malfunction);
        p->queueOutput(stereo);
    }
    if (!p->captureAttempted || p->capture)
        p->message = !p->transport.ready() ? "Voice UDP unavailable"
                     : micEnabled          ? "Voice: voice activated"
                                           : "Voice: listening only";
}
void Chat::close() {
    p->stopCapture(true);
    p->playback.clear();
    p->transport.close();
    p->clearOutput();
    p->configured = false;
    p->message = "Voice: disconnected";
}
bool Chat::transmitting() const { return p->transmitting; }
std::string Chat::status() const { return p->message; }
Json Chat::report() const {
    return {{"configured", p->configured},
            {"capture_open", bool(p->capture)},
            {"output_open", p->outputOpen()},
            {"shared_output", bool(p->shared.queue)},
            {"transmitting", p->transmitting},
            {"channel", p->channel == Channel::Radio ? "radio" : "proximity"},
            {"encoded", p->encoded},
            {"radio_malfunction", p->malfunction},
            {"distorted_tx_frames", p->transmitDamage.processedFrames()},
            {"distorted_rx_frames", p->receiveDamage.processedFrames()},
            {"capturing", p->capturing},
            {"voiced_frames", p->voicedFrames},
            {"silent_frames", p->silentFrames},
            {"dropped_capture_bytes", p->droppedCaptureBytes},
            {"status", p->message},
            {"transport", p->transport.report()},
            {"playback", p->playback.report()}};
}
} // namespace avp::voice
