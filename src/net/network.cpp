#include "network.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <set>
#include <stdexcept>
#include "native_socket.hpp"
namespace avp::net {
namespace sockets = avp::native_socket;
namespace {
void validate(const Limits &l) {
    if (!l.maxMessageBytes || l.maxMessageBytes > 128 * 1024 * 1024 || l.maxQueuedBytes < 4 ||
        !l.maxQueuedMessages || !l.maxMessagesPerPoll || !l.maxIoBytesPerPoll || !l.maxNesting ||
        l.maxNesting > 128 || !l.maxJsonNodes)
        throw std::invalid_argument("Invalid network resource limits");
}
bool utf8(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        unsigned c = static_cast<unsigned char>(text[i++]);
        if (c < 128)
            continue;
        unsigned n = 0, code = 0, min = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            n = 1;
            code = c & 31;
            min = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            n = 2;
            code = c & 15;
            min = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 3;
            code = c & 7;
            min = 0x10000;
        } else
            return false;
        if (n > text.size() - i)
            return false;
        while (n--) {
            unsigned b = static_cast<unsigned char>(text[i++]);
            if ((b & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (b & 63);
        }
        if (code < min || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}
// Preflight before DOM allocation: bounded recursion/nodes, finite numbers,
// valid UTF8, definite containers, unique string map keys, no binary/tags.
// The native protocol carries JSON values, never cartridge bytes or CBOR code.
struct Cbor {
    const std::vector<uint8_t> &bytes;
    const Limits &limits;
    size_t at = 0, nodes = 0;
    [[noreturn]] void bad() const {
        throw std::runtime_error("Malformed or excessive CBOR message");
    }
    uint8_t byte() {
        if (at == bytes.size())
            bad();
        return bytes[at++];
    }
    uint64_t argument(unsigned a) {
        if (a < 24)
            return a;
        if (a > 27)
            bad();
        unsigned n = 1u << (a - 24);
        if (n > bytes.size() - at)
            bad();
        uint64_t result = 0;
        while (n--)
            result = (result << 8) | byte();
        return result;
    }
    std::string_view text(unsigned a) {
        uint64_t n = argument(a);
        if (n > bytes.size() - at)
            bad();
        std::string_view s(reinterpret_cast<const char *>(bytes.data() + at), size_t(n));
        at += size_t(n);
        if (!utf8(s))
            bad();
        return s;
    }
    void value(size_t depth = 0) {
        if (depth > limits.maxNesting || ++nodes > limits.maxJsonNodes)
            bad();
        unsigned h = byte(), major = h >> 5, a = h & 31;
        if (major == 3) {
            text(a);
            return;
        }
        uint64_t n = argument(a);
        if (major == 0)
            return;
        if (major == 1) {
            if (n > uint64_t(INT64_MAX))
                bad();
            return;
        }
        if (major == 4) {
            if (n > bytes.size() - at || n > limits.maxJsonNodes - nodes)
                bad();
            while (n--)
                value(depth + 1);
            return;
        }
        if (major == 5) {
            if (n > (bytes.size() - at) / 2 || n > (limits.maxJsonNodes - nodes) / 2)
                bad();
            std::set<std::string_view> keys;
            while (n--) {
                if (++nodes > limits.maxJsonNodes)
                    bad();
                unsigned key = byte();
                if ((key >> 5) != 3)
                    bad();
                if (!keys.insert(text(key & 31)).second)
                    bad();
                value(depth + 1);
            }
            return;
        }
        if (major == 7) {
            if (a == 20 || a == 21 || a == 22)
                return;
            if (a == 25 && ((n & 0x7c00) != 0x7c00))
                return;
            if (a == 26 && std::isfinite(std::bit_cast<float>(uint32_t(n))))
                return;
            if (a == 27 && std::isfinite(std::bit_cast<double>(n)))
                return;
        }
        bad();
    }
    void check() {
        value();
        if (at != bytes.size())
            bad();
    }
};
uint16_t portNumber(std::string_view text) {
    if (text.empty())
        throw std::invalid_argument("Missing TCP port");
    unsigned value = 0;
    for (char c : text) {
        if (c < '0' || c > '9')
            throw std::invalid_argument("TCP port must contain digits");
        value = value * 10 + unsigned(c - '0');
        if (value > 65535)
            throw std::invalid_argument("TCP port outside 1..65535");
    }
    if (!value)
        throw std::invalid_argument("TCP port outside 1..65535");
    return uint16_t(value);
}
using sockets::failure;
void configure(sockets::Handle fd) {
    if (!sockets::configure(fd))
        throw std::runtime_error(failure("nonblocking socket"));
}
void tcp(sockets::Handle fd) {
    int yes = 1;
    if (sockets::option(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0)
        throw std::runtime_error(failure("TCP_NODELAY"));
#ifdef SO_NOSIGPIPE
    if (sockets::option(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)) < 0)
        throw std::runtime_error(failure("SO_NOSIGPIPE"));
#endif
}
using Address = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>;
Address addresses(const std::string &host, uint16_t port, bool passive) {
    sockets::initialize();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV | (passive ? AI_PASSIVE : 0);
    addrinfo *out = nullptr;
    auto service = std::to_string(port);
    int result = getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(), &hints, &out);
    if (result)
        throw std::runtime_error(std::string("TCP address: ") + gai_strerror(result));
    return Address(out, freeaddrinfo);
}
} // namespace
Endpoint parseEndpoint(std::string_view text, uint16_t defaultPort) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    if (text.empty() || text.size() > 1024 || !defaultPort)
        throw std::invalid_argument("Missing or excessive TCP endpoint");
    for (unsigned char c : text)
        if (c <= 32 || c == 127)
            throw std::invalid_argument("Whitespace/control character in TCP endpoint");
    Endpoint out;
    out.port = defaultPort;
    if (text.front() == '[') {
        auto end = text.find(']');
        if (end == std::string_view::npos)
            throw std::invalid_argument("Unclosed IPv6 brackets");
        out.host = text.substr(1, end - 1);
        auto rest = text.substr(end + 1);
        if (!rest.empty()) {
            if (rest.front() != ':')
                throw std::invalid_argument("Unexpected text after IPv6 address");
            out.port = portNumber(rest.substr(1));
        }
    } else {
        auto first = text.find(':');
        if (first != std::string_view::npos && first == text.rfind(':')) {
            out.host = text.substr(0, first);
            out.port = portNumber(text.substr(first + 1));
        } else
            out.host = text;
    }
    if (out.host.empty() || out.host.find_first_of("[]/") != std::string::npos)
        throw std::invalid_argument("Invalid TCP host");
    return out;
}
struct Connection::Impl {
    sockets::Handle fd = -1;
    Limits limits;
    std::string error;
    struct Frame {
        std::vector<uint8_t> bytes;
        size_t offset = 0;
        bool replaceable = false;
    };
    std::deque<Frame> output;
    size_t queued = 0;
    std::array<uint8_t, 4> header{};
    size_t headerRead = 0, bodyRead = 0;
    std::vector<uint8_t> body;
    explicit Impl(Limits l) : limits(l) { validate(l); }
    ~Impl() { close(); }
    void close() {
        if (fd >= 0)
            sockets::close(fd);
        fd = -1;
        output.clear();
        queued = 0;
        body.clear();
        headerRead = bodyRead = 0;
    }
    void fail(std::string text) {
        error = std::move(text);
        close();
    }
    void flush() {
        size_t budget = limits.maxIoBytesPerPoll;
        while (fd >= 0 && !output.empty() && budget) {
            auto &f = output.front();
            size_t wanted = std::min(budget, f.bytes.size() - f.offset);
#ifdef MSG_NOSIGNAL
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            auto sent = sockets::send(fd, f.bytes.data() + f.offset, wanted, flags);
            if (sent > 0) {
                f.offset += size_t(sent);
                budget -= size_t(sent);
                if (f.offset == f.bytes.size()) {
                    queued -= f.bytes.size();
                    output.pop_front();
                }
                continue;
            }
            if (sent < 0 && sockets::error() == EINTR)
                continue;
            if (sent < 0 && (sockets::error() == EAGAIN || sockets::error() == EWOULDBLOCK))
                break;
            fail(sent == 0 ? "TCP peer stopped accepting data" : failure("TCP send"));
        }
    }
};
Connection::Connection() : p(std::make_unique<Impl>(Limits{})) {}
Connection::Connection(intptr_t fd, Limits limits) : p(std::make_unique<Impl>(limits)) {
    p->fd = fd;
}
Connection::~Connection() = default;
Connection::Connection(Connection &&) noexcept = default;
Connection &Connection::operator=(Connection &&) noexcept = default;
Connection Connection::connect(const std::string &host, uint16_t port, int timeoutMs,
                               Limits limits) {
    validate(limits);
    if (host.empty() || !port || timeoutMs <= 0)
        throw std::invalid_argument("Invalid TCP connection endpoint/timeout");
    auto list = addresses(host, port, false);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::string error = "No TCP address accepted the connection";
    for (auto *a = list.get(); a; a = a->ai_next) {
        sockets::Handle fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) {
            error = failure("TCP socket");
            continue;
        }
        try {
            configure(fd);
            tcp(fd);
        } catch (...) {
            sockets::close(fd);
            throw;
        }
        int result = ::connect(fd, a->ai_addr, a->ai_addrlen);
        if (result < 0 && (sockets::error() == EINPROGRESS || sockets::error() == EWOULDBLOCK)) {
            for (;;) {
                auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                deadline - std::chrono::steady_clock::now())
                                .count();
                if (left <= 0) {
                    error = "TCP connection timed out";
                    break;
                }
                sockets::Poll wait{decltype(sockets::Poll::fd)(fd), POLLOUT, 0};
                int ready = sockets::poll(&wait, 1, int(std::min<int64_t>(left, INT32_MAX)));
                if (ready < 0 && sockets::error() == EINTR)
                    continue;
                if (ready <= 0) {
                    error = ready == 0 ? "TCP connection timed out" : failure("TCP connect poll");
                    break;
                }
                int status = 0;
                sockets::Length n = sizeof(status);
                if (sockets::getOption(fd, SOL_SOCKET, SO_ERROR, &status, &n) < 0) {
                    error = failure("TCP connection status");
                    break;
                }
                if (status == 0)
                    return Connection(fd, limits);
                error = failure("TCP connect", status);
                break;
            }
        } else if (result == 0)
            return Connection(fd, limits);
        else
            error = failure("TCP connect");
        sockets::close(fd);
        if (std::chrono::steady_clock::now() >= deadline)
            break;
    }
    throw std::runtime_error(error);
}
bool Connection::send(const Json &message, bool replaceSnapshot) {
    if (!connected())
        return false;
    try {
        auto payload = Json::to_cbor(message);
        if (payload.empty() || payload.size() > p->limits.maxMessageBytes) {
            p->fail("Outgoing message exceeds frame size limit");
            return false;
        }
        if (replaceSnapshot)
            for (auto i = p->output.begin(); i != p->output.end();) {
                if (i->replaceable && i->offset == 0) {
                    p->queued -= i->bytes.size();
                    i = p->output.erase(i);
                } else
                    ++i;
            }
        size_t size = payload.size() + 4;
        if (size > p->limits.maxQueuedBytes - p->queued ||
            p->output.size() >= p->limits.maxQueuedMessages) {
            p->fail("TCP peer exceeded outgoing queue limit");
            return false;
        }
        Impl::Frame f;
        f.bytes.reserve(size);
        auto n = uint32_t(payload.size());
        for (int shift : {24, 16, 8, 0})
            f.bytes.push_back(uint8_t(n >> shift));
        f.bytes.insert(f.bytes.end(), payload.begin(), payload.end());
        f.replaceable = replaceSnapshot;
        p->queued += size;
        p->output.push_back(std::move(f));
        p->flush();
        return connected();
    } catch (const std::exception &e) {
        p->fail(std::string("Cannot encode TCP message: ") + e.what());
        return false;
    }
}
std::vector<Json> Connection::poll() {
    std::vector<Json> result;
    if (!connected())
        return result;
    p->flush();
    size_t budget = p->limits.maxIoBytesPerPoll;
    while (connected() && budget && result.size() < p->limits.maxMessagesPerPoll) {
        bool header = p->headerRead < 4;
        size_t remaining = header ? 4 - p->headerRead : p->body.size() - p->bodyRead;
        auto *target = header ? p->header.data() + p->headerRead : p->body.data() + p->bodyRead;
        auto got = sockets::receive(p->fd, target, std::min(remaining, budget), 0);
        if (got > 0) {
            budget -= size_t(got);
            if (header) {
                p->headerRead += size_t(got);
                if (p->headerRead == 4) {
                    size_t n = 0;
                    for (auto b : p->header)
                        n = (n << 8) | b;
                    if (n == 0 || n > p->limits.maxMessageBytes) {
                        p->fail("Incoming frame size outside configured limit");
                        break;
                    }
                    p->body.resize(n);
                    p->bodyRead = 0;
                }
            } else {
                p->bodyRead += size_t(got);
                if (p->bodyRead == p->body.size()) {
                    try {
                        Cbor{p->body, p->limits}.check();
                        result.push_back(Json::from_cbor(p->body));
                    } catch (const std::exception &e) {
                        p->fail(std::string("Invalid TCP message: ") + e.what());
                        break;
                    }
                    p->headerRead = p->bodyRead = 0;
                    p->body.clear();
                }
            }
            continue;
        }
        if (got < 0 && sockets::error() == EINTR)
            continue;
        if (got < 0 && (sockets::error() == EAGAIN || sockets::error() == EWOULDBLOCK))
            break;
        p->fail(got == 0 ? "TCP peer disconnected" : failure("TCP receive"));
        break;
    }
    return result;
}
bool Connection::connected() const { return p && p->fd >= 0; }
std::string Connection::peerHost() const {
    if (!connected())
        return {};
    sockaddr_storage address{};
    sockets::Length size = sizeof(address);
    char host[NI_MAXHOST]{};
    if (getpeername(p->fd, reinterpret_cast<sockaddr *>(&address), &size) == 0 &&
        getnameinfo(reinterpret_cast<sockaddr *>(&address), size, host, sizeof(host), nullptr, 0,
                    NI_NUMERICHOST) == 0)
        return host;
    return {};
}
const std::string &Connection::error() const {
    static const std::string empty;
    return p ? p->error : empty;
}
size_t Connection::queuedBytes() const { return p ? p->queued : 0; }
void Connection::close() {
    if (p)
        p->close();
}
struct Listener::Impl {
    sockets::Handle fd = -1;
    uint16_t port = 0;
    Limits limits;
    ~Impl() {
        if (fd >= 0)
            sockets::close(fd);
    }
};
Listener::Listener() : p(std::make_unique<Impl>()) {}
Listener::~Listener() = default;
Listener::Listener(Listener &&) noexcept = default;
Listener &Listener::operator=(Listener &&) noexcept = default;
Listener Listener::bind(const std::string &host, uint16_t port, Limits limits) {
    validate(limits);
    auto list = addresses(host, port, true);
    std::string error = "No TCP listen address";
    for (auto *a = list.get(); a; a = a->ai_next) {
        sockets::Handle fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) {
            error = failure("TCP listen socket");
            continue;
        }
        int yes = 1;
        sockets::option(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (a->ai_family == AF_INET6) {
            int no = 0;
            sockets::option(fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));
        }
        if (::bind(fd, a->ai_addr, a->ai_addrlen) < 0 || ::listen(fd, 64) < 0) {
            error = failure("TCP listen");
            sockets::close(fd);
            continue;
        }
        try {
            configure(fd);
        } catch (...) {
            sockets::close(fd);
            throw;
        }
        sockaddr_storage address{};
        sockets::Length size = sizeof(address);
        if (getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size) < 0) {
            error = failure("TCP listen port");
            sockets::close(fd);
            continue;
        }
        Listener out;
        out.p->fd = fd;
        out.p->limits = limits;
        out.p->port = ntohs(address.ss_family == AF_INET
                                ? reinterpret_cast<sockaddr_in *>(&address)->sin_port
                                : reinterpret_cast<sockaddr_in6 *>(&address)->sin6_port);
        return out;
    }
    throw std::runtime_error(error);
}
uint16_t Listener::port() const { return p ? p->port : 0; }
std::optional<Connection> Listener::accept() {
    if (!p || p->fd < 0)
        return std::nullopt;
    for (;;) {
        sockets::Handle fd = ::accept(p->fd, nullptr, nullptr);
        if (fd < 0) {
            if (sockets::error() == EINTR)
                continue;
            if (sockets::error() == EAGAIN || sockets::error() == EWOULDBLOCK)
                return std::nullopt;
            throw std::runtime_error(failure("TCP accept"));
        }
        try {
            configure(fd);
            tcp(fd);
        } catch (...) {
            sockets::close(fd);
            throw;
        }
        return Connection(fd, p->limits);
    }
}
void Listener::close() { p = std::make_unique<Impl>(); }
} // namespace avp::net
