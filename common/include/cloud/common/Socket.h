// 负责人：成员1：服务端架构/组长
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

// Windows 使用 Socket 前必须调用 WSAStartup，结束时调用 WSACleanup。
// 把它们放进构造/析构函数，就是 C++ 常见的 RAII：对象活着，资源就可用。
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
    // Socket 句柄只能有一个所有者，所以禁止复制、允许移动。
    // 移动后 other 会变成无效句柄，避免两个对象重复 close 同一资源。
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

// 下列函数隐藏 Windows/POSIX Socket API 差异，让业务层只处理 Socket 对象。
Socket connectTcp(const std::string& host, std::uint16_t port);
Socket listenTcp(const std::string& address, std::uint16_t port, int backlog = 32);
Socket acceptTcp(const Socket& listener, std::string* peerAddress = nullptr);
void sendPacket(const Socket& socket, const Packet& packet);
// 先精确读取 24 字节头，再根据 bodyLength 精确读取正文。
Packet receivePacket(const Socket& socket);

} // namespace cloud::common
