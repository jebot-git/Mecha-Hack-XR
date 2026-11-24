#include "voice_wire.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include "native_socket.hpp"
#ifdef _WIN32
#include <bcrypt.h>
#endif
#include <fstream>

namespace avp::voice {
namespace sockets = avp::native_socket;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace {
using Token = std::array<uint8_t, 16>;
using Bytes = std::vector<uint8_t>;
constexpr size_t clientHeader = 33, serverHeader = 45;
void append(Bytes &b, uint64_t n, int count) {
    for (int i = count - 1; i >= 0; --i)
        b.push_back(uint8_t(n >> (8 * i)));
}
uint64_t read(std::span<const uint8_t> b, size_t at, int count) {
    uint64_t n = 0;
    for (int i = 0; i < count; ++i)
        n = (n << 8) | b[at + i];
    return n;
}
Bytes header(const char *magic, const Token &token) {
    Bytes b(magic, magic + 4);
    b.insert(b.end(), token.begin(), token.end());
    return b;
}
std::string hex(const Token &t) {
    std::string s;
    for (auto n : t) {
        s += "0123456789abcdef"[n >> 4];
        s += "0123456789abcdef"[n & 15];
    }
    return s;
}
Token parseToken(const std::string &s) {
    if (s.size() != 32)
        throw std::runtime_error("Invalid voice credentials");
    Token t{};
    for (size_t i = 0; i < s.size(); ++i) {
        auto n = std::string_view("0123456789abcdef").find(s[i]);
        if (n == std::string_view::npos)
            throw std::runtime_error("Invalid voice token");
        t[i / 2] |= uint8_t(n << (i % 2 ? 0 : 4));
    }
    return t;
}
struct Replay {
    uint32_t last = 0;
    uint64_t seen = 0;
    bool accept(uint32_t sequence) {
        if (!sequence)
            return false;
        if (sequence > last) {
            uint32_t shift = sequence - last;
            seen = shift >= 64 ? 1 : (seen << shift) | 1;
            last = sequence;
            return true;
        }
        uint32_t behind = last - sequence;
        if (behind >= 64 || (seen & (uint64_t(1) << behind)))
            return false;
        seen |= uint64_t(1) << behind;
        return true;
    }
};
struct Address {
    sockaddr_storage value{};
    sockets::Length size = 0;
};
struct Datagram {
    Address from;
    Bytes bytes;
};
struct Socket {
    sockets::Handle fd = -1;
    uint16_t port = 0;
    ~Socket() { close(); }
    void close() {
        if (fd >= 0)
            sockets::close(fd);
        fd = -1;
    }
    void open(const std::string &host, uint16_t requested, bool server) {
        close();
        sockets::initialize();
        addrinfo hint{}, *raw = nullptr;
        hint.ai_family = AF_UNSPEC;
        hint.ai_socktype = SOCK_DGRAM;
        hint.ai_flags = server ? AI_PASSIVE : 0;
        int error = getaddrinfo(host.empty() ? nullptr : host.c_str(),
                                std::to_string(requested).c_str(), &hint, &raw);
        if (error)
            throw std::runtime_error(std::string("Voice address: ") + gai_strerror(error));
        std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw, freeaddrinfo);
        for (auto a = raw; a; a = a->ai_next) {
            sockets::Handle candidate = ::socket(a->ai_family, SOCK_DGRAM, 0);
            if (candidate < 0)
                continue;
            bool okay = sockets::configure(candidate);
            if (server && a->ai_family == AF_INET6) {
                int zero = 0;
                sockets::option(candidate, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
            }
            if (okay && (server ? ::bind(candidate, a->ai_addr, a->ai_addrlen)
                                : ::connect(candidate, a->ai_addr, a->ai_addrlen)) == 0) {
                fd = candidate;
                break;
            }
            sockets::close(candidate);
        }
        if (fd < 0)
            throw std::runtime_error("Cannot open native UDP voice socket");
        sockaddr_storage local{};
        sockets::Length size = sizeof(local);
        getsockname(fd, reinterpret_cast<sockaddr *>(&local), &size);
        port = local.ss_family == AF_INET
                   ? ntohs(reinterpret_cast<sockaddr_in *>(&local)->sin_port)
                   : ntohs(reinterpret_cast<sockaddr_in6 *>(&local)->sin6_port);
    }
    bool send(const Bytes &b, const Address *to = nullptr) {
        if (fd < 0)
            return false;
        auto count = to ? sockets::sendTo(fd, b.data(), b.size(), 0,
                                          reinterpret_cast<const sockaddr *>(&to->value), to->size)
                        : sockets::send(fd, b.data(), b.size(), 0);
        return count == decltype(count)(b.size());
    }
    std::vector<Datagram> receive() {
        std::vector<Datagram> out;
        for (int i = 0; i < 128; ++i) {
            std::array<uint8_t, 1024> buffer{};
            Address from;
            from.size = sizeof(from.value);
            auto count =
                sockets::receiveFrom(fd, buffer.data(), buffer.size(),
                                     reinterpret_cast<sockaddr *>(&from.value), &from.size);
            if (count < 0)
                break;
            if (count <= decltype(count)(serverHeader + maxPacket))
                out.push_back({from, Bytes(buffer.begin(), buffer.begin() + count)});
        }
        return out;
    }
};

} // namespace
bool validOpus(std::span<const uint8_t> b) {
    if (b.empty() || b.size() > maxPacket || (b[0] & 7) != 0)
        return false;
    int config = b[0] >> 3;
    return config == 1 || config == 5 || config == 9 || config == 13 || config == 15 ||
           config == 19 || config == 23 || config == 27 || config == 31;
}
struct Relay::Impl {
    struct Peer {
        Token token{};
        Address address;
        bool bound = false;
        Replay replay;
        double tokens = 12;
        Clock::time_point refill = Clock::now(), heartbeat = refill;
    };
    Socket socket;
    std::map<uint32_t, Peer> peers;
    uint64_t received = 0, forwarded = 0, rejected = 0;
};
Relay::Relay(const std::string &host, uint16_t port) : p(std::make_unique<Impl>()) {
    p->socket.open(host, port, true);
}
Relay::~Relay() = default;
uint16_t Relay::port() const { return p->socket.port; }
Json Relay::admit(uint32_t id) {
    Impl::Peer peer;
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, peer.token.data(), ULONG(peer.token.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("Cannot obtain voice session credentials");
#else
    std::ifstream random("/dev/urandom", std::ios::binary);
    if (!random.read(reinterpret_cast<char *>(peer.token.data()), peer.token.size()))
        throw std::runtime_error("Cannot obtain voice session credentials");
#endif
    p->peers[id] = peer;
    return {{"version", 1},       {"port", port()},          {"token", hex(peer.token)},
            {"rate", sampleRate}, {"samples", frameSamples}, {"bitrate", bitrate}};
}
void Relay::remove(uint32_t id) { p->peers.erase(id); }
void Relay::poll(std::span<const Participant> players, uint64_t epoch) {
    const auto now = Clock::now();
    auto participant = [&](uint32_t id) -> const Participant * {
        for (const auto &player : players)
            if (player.id == id)
                return &player;
        return nullptr;
    };
    for (const auto &d : p->socket.receive()) {
        const auto &b = d.bytes;
        ++p->received;
        if (b.size() < clientHeader || std::memcmp(b.data(), "AVV1", 4)) {
            ++p->rejected;
            continue;
        }
        auto sender = std::find_if(p->peers.begin(), p->peers.end(), [&](auto &entry) {
            return std::equal(entry.second.token.begin(), entry.second.token.end(), b.begin() + 4);
        });
        if (sender == p->peers.end()) {
            ++p->rejected;
            continue;
        }
        auto &peer = sender->second;
        peer.tokens = std::min(
            12., peer.tokens + std::chrono::duration<double>(now - peer.refill).count() * 55);
        peer.refill = now;
        if (peer.tokens < 1 || read(b, 24, 8) != epoch) {
            ++p->rejected;
            continue;
        }
        --peer.tokens;
        const auto *source = participant(sender->first);
        if (!source) {
            ++p->rejected;
            continue;
        }
        // Registration/heartbeat is control only, even for listen-only peers.
        if (b.size() == clientHeader && b[32] == 2 && read(b, 20, 4) == 0) {
            peer.address = d.from;
            peer.bound = true;
            peer.heartbeat = now;
            auto ack = header("AVR1", peer.token);
            append(ack, 0, 4);
            append(ack, 0, 4);
            append(ack, epoch, 8);
            ack.push_back(2);
            append(ack, 0, 4);
            append(ack, 0, 4);
            p->socket.send(ack, &peer.address);
            continue;
        }
        if (!source->active || b[32] != 1 || !validOpus(std::span(b).subspan(clientHeader)) ||
            !peer.replay.accept(uint32_t(read(b, 20, 4)))) {
            ++p->rejected;
            continue;
        }
        peer.address = d.from;
        peer.bound = true;
        peer.heartbeat = now;
        for (const auto &[id, targetPeer] : p->peers) {
            const auto *target = participant(id);
            if (id == sender->first || !target || !target->active || target->room != source->room || !targetPeer.bound ||
                now - targetPeer.heartbeat > std::chrono::seconds(5))
                continue;
            if (!b[32] && std::hypot(target->position.x - source->position.x,
                                     target->position.z - source->position.z) *
                                  metresPerCell >=
                              proximityMetres)
                continue;
            auto out = header("AVR1", targetPeer.token);
            append(out, sender->first, 4);
            append(out, read(b, 20, 4), 4);
            append(out, epoch, 8);
            out.push_back(b[32]);
            append(out, std::bit_cast<uint32_t>(source->position.x), 4);
            append(out, std::bit_cast<uint32_t>(source->position.z), 4);
            out.insert(out.end(), b.begin() + clientHeader, b.end());
            if (p->socket.send(out, &targetPeer.address))
                ++p->forwarded;
        }
    }
}
Json Relay::report() const {
    return {{"received", p->received},
            {"forwarded", p->forwarded},
            {"rejected", p->rejected},
            {"port", port()}};
}
struct Transport::Impl {
    Socket socket;
    Token token{};
    uint32_t sequence = 0;
    uint64_t epoch = 0, sent = 0, received = 0, rejected = 0;
    Clock::time_point heartbeat{}, acknowledged{};
};
Transport::Transport() : p(std::make_unique<Impl>()) {}
Transport::~Transport() = default;
void Transport::connect(const std::string &host, const Json &credentials) {
    close();
    if (credentials.at("version") != 1 || credentials.at("rate") != sampleRate ||
        credentials.at("samples") != frameSamples || credentials.at("bitrate") != bitrate ||
        !credentials.at("port").is_number_integer() || credentials.at("port") < 1 ||
        credentials.at("port") > 65535)
        throw std::runtime_error("Unsupported voice configuration");
    p->token = parseToken(credentials.at("token"));
    p->socket.open(host, credentials.at("port").get<uint16_t>(), false);
}
void Transport::close() { p = std::make_unique<Impl>(); }
std::vector<Packet> Transport::poll(uint64_t epoch) {
    std::vector<Packet> packets;
    if (p->socket.fd < 0)
        return packets;
    auto now = Clock::now();
    if (p->epoch != epoch) {
        p->epoch = epoch;
        p->heartbeat = {};
        p->acknowledged = {};
    }
    if (now - p->heartbeat >= std::chrono::seconds(1)) {
        auto b = header("AVV1", p->token);
        append(b, 0, 4);
        append(b, epoch, 8);
        b.push_back(2);
        p->socket.send(b);
        p->heartbeat = now;
    }
    for (const auto &d : p->socket.receive()) {
        const auto &b = d.bytes;
        if (b.size() < serverHeader || std::memcmp(b.data(), "AVR1", 4) ||
            !std::equal(p->token.begin(), p->token.end(), b.begin() + 4) ||
            read(b, 28, 8) != epoch) {
            ++p->rejected;
            continue;
        }
        uint32_t id = uint32_t(read(b, 20, 4)), sequence = uint32_t(read(b, 24, 4));
        if (b.size() == serverHeader && !id && !sequence && b[36] == 2) {
            p->acknowledged = now;
            continue;
        }
        float x = std::bit_cast<float>(uint32_t(read(b, 37, 4))),
              z = std::bit_cast<float>(uint32_t(read(b, 41, 4)));
        // Positions are authoritative world metres; retail missions span both
        // negative coordinates and thousands of metres, unlike the source grid.
        if (!id || !sequence || b[36] > 1 || !std::isfinite(x) || !std::isfinite(z) ||
            !validOpus(std::span(b).subspan(serverHeader))) {
            ++p->rejected;
            continue;
        }
        packets.push_back({id,
                           sequence,
                           epoch,
                           Channel(b[36]),
                           {x, 12.f, z},
                           Bytes(b.begin() + serverHeader, b.end())});
        ++p->received;
    }
    return packets;
}
bool Transport::send(Channel channel, uint64_t epoch, std::span<const uint8_t> opus) {
    if (!ready() || uint8_t(channel) > 1 || !validOpus(opus) || p->sequence == UINT32_MAX)
        return false;
    auto b = header("AVV1", p->token);
    append(b, ++p->sequence, 4);
    append(b, epoch, 8);
    b.push_back(uint8_t(channel));
    b.insert(b.end(), opus.begin(), opus.end());
    bool sent = p->socket.send(b);
    if (sent)
        ++p->sent;
    return sent;
}
bool Transport::ready() const {
    return p->socket.fd >= 0 && Clock::now() - p->acknowledged < std::chrono::seconds(3);
}
Json Transport::report() const {
    return {{"ready", ready()},
            {"sent", p->sent},
            {"received", p->received},
            {"rejected", p->rejected}};
}
} // namespace avp::voice
