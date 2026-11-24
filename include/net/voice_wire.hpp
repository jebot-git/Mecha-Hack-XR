#pragma once
#include "vector.hpp"
namespace avp { using Vec3=iron::Vec3; inline constexpr float metresPerCell=1; }
#include "network.hpp"
#include <memory>
#include <span>

namespace avp::voice {
inline constexpr int sampleRate = 48000, frameSamples = 960, bitrate = 24000;
inline constexpr size_t maxPacket = 398; // CQ's 400-byte limit minus its sequence prefix.
inline constexpr float proximityMetres = 60;
enum class Channel : uint8_t { Proximity = 0, Radio = 1 };
struct Participant {
    uint32_t id = 0;
    Vec3 position; // Authoritative world metres, never supplied by the sender.
    bool active = false;
    uint64_t room = 0;
};
struct Packet {
    uint32_t sender = 0, sequence = 0;
    uint64_t epoch = 0;
    Channel channel = Channel::Proximity;
    Vec3 position;
    std::vector<uint8_t> opus;
};
bool validOpus(std::span<const uint8_t>);
// Native UDP relay; no microphone, decoder or graphics dependencies.
class Relay {
    struct Impl;
    std::unique_ptr<Impl> p;

  public:
    Relay(const std::string &bind, uint16_t port);
    ~Relay();
    uint16_t port() const;
    nlohmann::json admit(uint32_t id);
    void remove(uint32_t id);
    void poll(std::span<const Participant>, uint64_t epoch);
    nlohmann::json report() const;
};
class Transport {
    struct Impl;
    std::unique_ptr<Impl> p;

  public:
    Transport();
    ~Transport();
    void connect(const std::string &host, const nlohmann::json &credentials);
    void close();
    std::vector<Packet> poll(uint64_t epoch);
    bool send(Channel, uint64_t epoch, std::span<const uint8_t> opus);
    bool ready() const;
    nlohmann::json report() const;
};
} // namespace avp::voice
