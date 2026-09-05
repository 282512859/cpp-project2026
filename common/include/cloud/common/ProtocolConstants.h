// 负责人：成员1：服务端架构/组长
#pragma once

#include <cstddef>
#include <cstdint>

namespace cloud::common {

// Magic 是报文的“暗号”。收到的前 4 字节不是 LCD1 时，可以立即判定它不是本协议。
inline constexpr std::uint32_t kProtocolMagic = 0x4C434431; // "LCD1"
inline constexpr std::uint16_t kProtocolVersion = 1;
// 固定头长度必须与 PacketHeader 的线协议布局保持一致：4+2+2+4+4+8=24 字节。
inline constexpr std::size_t kPacketHeaderSize = 24;
inline constexpr std::uint16_t kDefaultPort = 9000;
// 文件使用 256 KiB 分块传输，避免一次把整个大文件读进内存。
inline constexpr std::uint32_t kDefaultChunkSize = 256U * 1024U;
// 在真正分配 body 内存之前限制长度，防止异常或恶意报文耗尽内存。
inline constexpr std::uint32_t kMaxBodySize = 4U * 1024U * 1024U;
inline constexpr std::uint32_t kMaxJsonBodySize = 1U * 1024U * 1024U;

} // namespace cloud::common
