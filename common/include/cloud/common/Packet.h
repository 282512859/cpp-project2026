// 负责人：成员1：服务端架构/组长
#pragma once

#include "cloud/common/MessageType.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cloud::common {

struct PacketHeader {
    std::uint32_t magic{};
    std::uint16_t version{};
    MessageType type{};
    std::uint32_t flags{};
    std::uint32_t bodyLength{};
    std::uint64_t requestId{};
};

struct Packet {
    PacketHeader header{};
    std::vector<std::uint8_t> body;
};

Packet makePacket(MessageType type, std::uint64_t requestId,
                  std::vector<std::uint8_t> body = {},
                  std::uint32_t flags = FlagNone);
Packet makeJsonPacket(MessageType type, std::uint64_t requestId,
                      const std::string& json,
                      std::uint32_t flags = FlagNone);
std::string bodyAsString(const Packet& packet);

} // namespace cloud::common
