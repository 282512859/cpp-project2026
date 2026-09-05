// 负责人：成员1：服务端架构/组长
#include "cloud/common/Packet.h"
#include "cloud/common/ProtocolConstants.h"

#include <string>

namespace cloud::common {

Packet makePacket(MessageType type, std::uint64_t requestId,
                  std::vector<std::uint8_t> body, std::uint32_t flags) {
    // 使用默认初始化得到一个干净的 Packet，再补齐所有协议字段。
    Packet packet;
    packet.header.magic = kProtocolMagic;
    packet.header.version = kProtocolVersion;
    packet.header.type = type;
    packet.header.flags = flags;
    packet.header.bodyLength = static_cast<std::uint32_t>(body.size());
    packet.header.requestId = requestId;
    // body 是按值传入的临时副本，move 可以把其内部内存直接交给 packet，避免再复制一次。
    packet.body = std::move(body);
    return packet;
}

Packet makeJsonPacket(MessageType type, std::uint64_t requestId,
                      const std::string& json, std::uint32_t flags) {
    // UTF-8 JSON 本质上也是字节序列；这里逐字节复制到统一的 body 容器。
    return makePacket(type, requestId,
                      std::vector<std::uint8_t>(json.begin(), json.end()), flags);
}

std::string bodyAsString(const Packet& packet) {
    // 只有调用方确定 body 是文本时才能使用；文件块等二进制 body 不应这样转换。
    return {packet.body.begin(), packet.body.end()};
}

} // namespace cloud::common
