// 负责人：成员1：服务端架构/组长
#pragma once

#include "cloud/common/MessageType.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cloud::common {

// 这是报文头在程序中的表示。结构体在内存中可能有填充字节，所以不能直接
// send(sizeof(PacketHeader))；必须交给 ProtocolCodec 按固定的 24 字节格式编码。
struct PacketHeader {
    std::uint32_t magic{};       // 协议标识，固定为 kProtocolMagic。
    std::uint16_t version{};     // 协议版本，当前为 1。
    MessageType type{};          // 本报文要执行的操作。
    std::uint32_t flags{};       // MessageFlags 的按位组合。
    std::uint32_t bodyLength{};  // body 的字节数，不包含 24 字节头。
    std::uint64_t requestId{};   // 请求与响应使用相同 ID，便于客户端配对。
};

// 一个完整逻辑报文 = 固定长度头 + 可变长度正文。
// JSON 请求和二进制文件块都存入字节数组 body，因此 Packet 本身不关心正文格式。
struct Packet {
    PacketHeader header{};
    std::vector<std::uint8_t> body;
};

// 统一构造报文，调用方无需每次重复填写 magic、version 和 bodyLength。
Packet makePacket(MessageType type, std::uint64_t requestId,
                  std::vector<std::uint8_t> body = {},
                  std::uint32_t flags = FlagNone);
Packet makeJsonPacket(MessageType type, std::uint64_t requestId,
                      const std::string& json,
                      std::uint32_t flags = FlagNone);
std::string bodyAsString(const Packet& packet);

} // namespace cloud::common
