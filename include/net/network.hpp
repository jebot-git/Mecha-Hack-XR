#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace avp::net {
using Json = nlohmann::json;
struct Limits {
    size_t maxMessageBytes = 4 * 1024 * 1024;
    size_t maxQueuedBytes = 8 * 1024 * 1024;
    size_t maxQueuedMessages = 128;
    size_t maxMessagesPerPoll = 64;
    size_t maxIoBytesPerPoll = 1024 * 1024;
    size_t maxNesting = 64;
    size_t maxJsonNodes = 262144;
};
struct Endpoint {
    std::string host;
    uint16_t port = 29497;
};
// host[:port], [IPv6][:port], or an unbracketed IPv6 address with default port.
Endpoint parseEndpoint(std::string_view text, uint16_t defaultPort = 29497);
class Listener;
class Connection {
    struct Impl;
    std::unique_ptr<Impl> p;
    explicit Connection(intptr_t descriptor, Limits);
    friend class Listener;

  public:
    Connection();
    ~Connection();
    Connection(Connection &&) noexcept;
    Connection &operator=(Connection &&) noexcept;
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    // DNS resolution uses the system resolver; timeout bounds TCP connection.
    // Throws on resolution, connection, or configuration errors.
    static Connection connect(const std::string &host, uint16_t port, int timeoutMs = 3000,
                              Limits limits = {});
    // Nonblocking. A replaceable snapshot may replace only entirely unsent
    // snapshots; partial frames/control messages always retain their framing.
    bool send(const Json &, bool replaceSnapshot = false);
    // Bounded nonblocking receive/flush. Messages preserve wire order. An error
    // or EOF closes this peer only; already decoded messages may be returned.
    std::vector<Json> poll();
    bool connected() const;
    std::string peerHost() const; // Numeric address actually selected by TCP.
    const std::string &error() const;
    size_t queuedBytes() const;
    void close();
};
class Listener {
    struct Impl;
    std::unique_ptr<Impl> p;

  public:
    Listener();
    ~Listener();
    Listener(Listener &&) noexcept;
    Listener &operator=(Listener &&) noexcept;
    Listener(const Listener &) = delete;
    Listener &operator=(const Listener &) = delete;
    static Listener bind(const std::string &host, uint16_t port, Limits limits = {});
    uint16_t port() const;
    std::optional<Connection> accept();
    void close();
};
} // namespace avp::net
