#pragma once

#include "cloud/common/Packet.h"

#include <cstdint>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
using CloudNativeSocket = SOCKET;
inline constexpr CloudNativeSocket kInvalidCloudSocket = INVALID_SOCKET;
#else
using CloudNativeSocket = int;
inline constexpr CloudNativeSocket kInvalidCloudSocket = -1;
#endif

namespace cloud::common {

class SocketRuntime {
public:
    SocketRuntime();
    ~SocketRuntime();
    SocketRuntime(const SocketRuntime&) = delete;
    SocketRuntime& operator=(const SocketRuntime&) = delete;
};

class Socket {
public:
    Socket() = default;
    explicit Socket(CloudNativeSocket handle) : handle_(handle) {}
    ~Socket();
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    [[nodiscard]] bool valid() const noexcept { return handle_ != kInvalidCloudSocket; }
    [[nodiscard]] CloudNativeSocket native() const noexcept { return handle_; }
    void close();
    void shutdownBoth();

private:
    CloudNativeSocket handle_{kInvalidCloudSocket};
};

Socket connectTcp(const std::string& host, std::uint16_t port);
Socket listenTcp(const std::string& address, std::uint16_t port, int backlog = 32);
Socket acceptTcp(const Socket& listener, std::string* peerAddress = nullptr);
void sendPacket(const Socket& socket, const Packet& packet);
Packet receivePacket(const Socket& socket);

} // namespace cloud::common
