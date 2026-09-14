// 负责人：成员1：服务端架构/组长
#include "cloud/common/Socket.h"
#include "cloud/common/ProtocolCodec.h"
#include "cloud/common/ProtocolConstants.h"

#include <array>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace cloud::common {
namespace {

// 把平台相关的错误码和关闭函数集中封装，后面的网络流程就可以跨平台复用。
int lastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

void closeNative(CloudNativeSocket s) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

// 避免断网时操作系统无限等待，使工作线程可以尽快向 UI 报错。
constexpr long kConnectTimeoutMs = 8000;
constexpr long kIoTimeoutMs = 15000;

void setBlocking(CloudNativeSocket s, bool blocking) {
#ifdef _WIN32
    u_long mode = blocking ? 0UL : 1UL;
    if (ioctlsocket(s, FIONBIO, &mode) != 0) throw std::runtime_error("cannot configure socket mode");
#else
    const int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0 || fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) < 0)
        throw std::runtime_error("cannot configure socket mode");
#endif
}

void setIoTimeouts(CloudNativeSocket s) {
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(kIoTimeoutMs);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    const timeval timeout{ kIoTimeoutMs / 1000, (kIoTimeoutMs % 1000) * 1000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

bool connectWithTimeout(CloudNativeSocket s, const sockaddr* address, int length) {
    setBlocking(s, false);
    const int result = ::connect(s, address, length);
    if (result == 0) {
        setBlocking(s, true);
        return true;
    }
    const int error = lastSocketError();
#ifdef _WIN32
    if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) {
#else
    if (error != EINPROGRESS && error != EWOULDBLOCK) {
#endif
        setBlocking(s, true);
        return false;
    }
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(s, &writable);
    timeval timeout{ kConnectTimeoutMs / 1000, (kConnectTimeoutMs % 1000) * 1000 };
#ifdef _WIN32
    const int selected = select(0, nullptr, &writable, nullptr, &timeout);
#else
    const int selected = select(s + 1, nullptr, &writable, nullptr, &timeout);
#endif
    int socketError = 0;
#ifdef _WIN32
    int socketErrorLength = sizeof(socketError);
#else
    socklen_t socketErrorLength = sizeof(socketError);
#endif
    const bool connected = selected > 0 &&
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorLength) == 0 &&
        socketError == 0;
    setBlocking(s, true);
    return connected;
}

void sendAll(CloudNativeSocket s, const std::uint8_t* data, std::size_t size) {
    // send 可能只发送一部分数据；循环直到 size 变成 0，才能保证整个报文已交给系统。
    while (size) {
        const auto chunk = static_cast<int>(std::min<std::size_t>(size, 1U << 20U));
        const int sent = ::send(s, reinterpret_cast<const char*>(data), chunk, 0);
        if (sent <= 0) throw std::runtime_error("socket send failed: " + std::to_string(lastSocketError()));
        data += sent; size -= static_cast<std::size_t>(sent);
    }
}

void receiveAll(CloudNativeSocket s, std::uint8_t* data, std::size_t size) {
    // recv 同样不保证一次返回所需长度，因此要处理“半包”。返回 0 表示对端正常关闭连接。
    while (size) {
        const auto chunk = static_cast<int>(std::min<std::size_t>(size, 1U << 20U));
        const int received = ::recv(s, reinterpret_cast<char*>(data), chunk, 0);
        if (received == 0) throw std::runtime_error("peer closed the connection");
        if (received < 0) throw std::runtime_error("socket receive failed: " + std::to_string(lastSocketError()));
        data += received; size -= static_cast<std::size_t>(received);
    }
}

} // namespace

SocketRuntime::SocketRuntime() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2,2), &data) != 0) throw std::runtime_error("WSAStartup failed");
#endif
}
SocketRuntime::~SocketRuntime() {
#ifdef _WIN32
    WSACleanup();
#endif
}

Socket::~Socket() { close(); }
// 移动构造把句柄所有权交给新对象，并使旧对象失效。
Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) { other.handle_ = kInvalidCloudSocket; }
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) { close(); handle_ = other.handle_; other.handle_ = kInvalidCloudSocket; }
    return *this;
}
void Socket::close() { if (valid()) { closeNative(handle_); handle_ = kInvalidCloudSocket; } }
void Socket::shutdownBoth() {
    if (!valid()) return;
#ifdef _WIN32
    ::shutdown(handle_, SD_BOTH);
#else
    ::shutdown(handle_, SHUT_RDWR);
#endif
}

Socket connectTcp(const std::string& host, std::uint16_t port) {
    // getaddrinfo 可能同时给出 IPv4/IPv6 等多个候选地址，逐个尝试直到连接成功。
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const auto service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &results) != 0)
        throw std::runtime_error("cannot resolve server address");
    Socket connected;
    for (auto* p = results; p; p = p->ai_next) {
        Socket candidate(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
        if (candidate.valid() && connectWithTimeout(candidate.native(), p->ai_addr,
                                                    static_cast<int>(p->ai_addrlen))) {
            setIoTimeouts(candidate.native());
            connected = std::move(candidate); break;
        }
    }
    freeaddrinfo(results);
    if (!connected.valid()) throw std::runtime_error("cannot connect to server");
    return connected;
}

Socket listenTcp(const std::string& address, std::uint16_t port, int backlog) {
    // 服务端流程是 socket -> bind -> listen；任一候选地址成功即可。
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* results = nullptr;
    const auto service = std::to_string(port);
    const char* host = address.empty() ? nullptr : address.c_str();
    if (getaddrinfo(host, service.c_str(), &hints, &results) != 0)
        throw std::runtime_error("cannot resolve listen address");
    Socket listener;
    for (auto* p = results; p; p = p->ai_next) {
        Socket candidate(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
        if (!candidate.valid()) continue;
        // 允许服务端重启后较快重新绑定同一端口。
        int yes = 1;
        setsockopt(candidate.native(), SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&yes), sizeof(yes));
        if (::bind(candidate.native(), p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0 &&
            ::listen(candidate.native(), backlog) == 0) { listener = std::move(candidate); break; }
    }
    freeaddrinfo(results);
    if (!listener.valid()) throw std::runtime_error("cannot bind/listen on port " + service);
    return listener;
}

Socket acceptTcp(const Socket& listener, std::string* peerAddress) {
    sockaddr_storage addr{};
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    const auto accepted = ::accept(listener.native(), reinterpret_cast<sockaddr*>(&addr), &len);
    if (accepted == kInvalidCloudSocket) throw std::runtime_error("accept failed");
    if (peerAddress) {
        // 只取数字 IP，不做可能阻塞的反向 DNS 查询。
        char host[NI_MAXHOST]{};
        if (getnameinfo(reinterpret_cast<sockaddr*>(&addr), len, host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0)
            *peerAddress = host;
    }
    return Socket(accepted);
}

void sendPacket(const Socket& socket, const Packet& packet) {
    const auto bytes = encodePacket(packet);
    sendAll(socket.native(), bytes.data(), bytes.size());
}

Packet receivePacket(const Socket& socket) {
    // 固定头和正文分两步读取，bodyLength 经 decodeHeader 校验后才用于 resize。
    std::array<std::uint8_t, 24> raw{};
    receiveAll(socket.native(), raw.data(), raw.size());
    Packet packet;
    std::string error;
    if (!decodeHeader(raw, packet.header, error)) throw std::runtime_error(error);
    packet.body.resize(packet.header.bodyLength);
    if (!packet.body.empty()) receiveAll(socket.native(), packet.body.data(), packet.body.size());
    return packet;
}

} // namespace cloud::common
