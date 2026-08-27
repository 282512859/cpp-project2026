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
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cloud::common {
namespace {

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

void sendAll(CloudNativeSocket s, const std::uint8_t* data, std::size_t size) {
    while (size) {
        const auto chunk = static_cast<int>(std::min<std::size_t>(size, 1U << 20U));
        const int sent = ::send(s, reinterpret_cast<const char*>(data), chunk, 0);
        if (sent <= 0) throw std::runtime_error("socket send failed: " + std::to_string(lastSocketError()));
        data += sent; size -= static_cast<std::size_t>(sent);
    }
}

void receiveAll(CloudNativeSocket s, std::uint8_t* data, std::size_t size) {
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
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const auto service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &results) != 0)
        throw std::runtime_error("cannot resolve server address");
    Socket connected;
    for (auto* p = results; p; p = p->ai_next) {
        Socket candidate(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
        if (candidate.valid() && ::connect(candidate.native(), p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
            connected = std::move(candidate); break;
        }
    }
    freeaddrinfo(results);
    if (!connected.valid()) throw std::runtime_error("cannot connect to server");
    return connected;
}

Socket listenTcp(const std::string& address, std::uint16_t port, int backlog) {
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
