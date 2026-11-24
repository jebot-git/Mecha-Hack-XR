#pragma once
// Native BSD/Winsock adapters; wire framing and resource bounds stay shared.
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace avp::native_socket {
using Handle = intptr_t;
#ifdef _WIN32
using Length = int;
using Poll = WSAPOLLFD;
inline void initialize() {
    static const auto initialized = [] {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data))
            throw std::runtime_error("Cannot initialize Winsock 2.2");
        return true;
    }();
    (void)initialized;
}
inline int error() {
    int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK)
        return EWOULDBLOCK;
    if (e == WSAEINTR)
        return EINTR;
    if (e == WSAEINPROGRESS || e == WSAEALREADY)
        return EINPROGRESS;
    return e;
}
inline void close(Handle fd) {
    if (fd >= 0)
        closesocket(SOCKET(fd));
}
inline bool configure(Handle fd) {
    u_long nonblocking = 1;
    return ioctlsocket(SOCKET(fd), FIONBIO, &nonblocking) == 0 &&
           SetHandleInformation(reinterpret_cast<HANDLE>(fd), HANDLE_FLAG_INHERIT, 0);
}
inline int poll(Poll *p, int n, int timeout) { return WSAPoll(p, n, timeout); }
#else
using Length = socklen_t;
using Poll = pollfd;
inline void initialize() {}
inline int error() { return errno; }
inline void close(Handle fd) {
    if (fd >= 0)
        ::close(int(fd));
}
inline bool configure(Handle fd) {
    int flags = fcntl(int(fd), F_GETFL), descriptor = fcntl(int(fd), F_GETFD);
    return flags >= 0 && descriptor >= 0 && fcntl(int(fd), F_SETFL, flags | O_NONBLOCK) >= 0 &&
           fcntl(int(fd), F_SETFD, descriptor | FD_CLOEXEC) >= 0;
}
inline int poll(Poll *p, int n, int timeout) { return ::poll(p, n, timeout); }
#endif
inline std::string failure(const char *operation, int e = error()) {
#ifdef _WIN32
    return std::string(operation) + " (socket error " + std::to_string(e) + ")";
#else
    return std::string(operation) + ": " + std::strerror(e);
#endif
}
inline int option(Handle fd, int level, int name, const void *v, Length n) {
    return ::setsockopt(fd, level, name, reinterpret_cast<const char *>(v), n);
}
inline int getOption(Handle fd, int level, int name, void *v, Length *n) {
    return ::getsockopt(fd, level, name, reinterpret_cast<char *>(v), n);
}
inline auto send(Handle fd, const void *data, size_t n, int flags = 0) {
    return ::send(fd, reinterpret_cast<const char *>(data), int(std::min(n, size_t(INT32_MAX))),
                  flags);
}
inline auto receive(Handle fd, void *data, size_t n, int flags = 0) {
    return ::recv(fd, reinterpret_cast<char *>(data), int(std::min(n, size_t(INT32_MAX))), flags);
}
inline auto sendTo(Handle fd, const void *data, size_t n, int flags, const sockaddr *to,
                   Length size) {
    return ::sendto(fd, reinterpret_cast<const char *>(data), int(n), flags, to, size);
}
inline auto receiveFrom(Handle fd, void *data, size_t n, sockaddr *from, Length *size) {
#ifdef _WIN32
    constexpr int flags = 0; // Oversized UDP frames return WSAEMSGSIZE and are discarded.
#else
    constexpr int flags = MSG_TRUNC;
#endif
    return ::recvfrom(fd, reinterpret_cast<char *>(data), int(n), flags, from, size);
}
} // namespace avp::native_socket
