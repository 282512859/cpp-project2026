#include "cloud/common/Packet.h"
#include "cloud/common/ProtocolConstants.h"

#include <string>

namespace cloud::common {

Packet makePacket(MessageType type, std::uint64_t requestId,
                  std::vector<std::uint8_t> body, std::uint32_t flags) {
    Packet packet;
    packet.header.magic = kProtocolMagic;
    packet.header.version = kProtocolVersion;
    packet.header.type = type;
    packet.header.flags = flags;
    packet.header.bodyLength = static_cast<std::uint32_t>(body.size());
    packet.header.requestId = requestId;
    packet.body = std::move(body);
    return packet;
}

Packet makeJsonPacket(MessageType type, std::uint64_t requestId,
                      const std::string& json, std::uint32_t flags) {
    return makePacket(type, requestId,
                      std::vector<std::uint8_t>(json.begin(), json.end()), flags);
}

std::string bodyAsString(const Packet& packet) {
    return {packet.body.begin(), packet.body.end()};
}

} // namespace cloud::common
