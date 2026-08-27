#pragma once

#include <cstddef>
#include <cstdint>

namespace cloud::common {

inline constexpr std::uint32_t kProtocolMagic = 0x4C434431; // "LCD1"
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kPacketHeaderSize = 24;
inline constexpr std::uint16_t kDefaultPort = 9000;
inline constexpr std::uint32_t kDefaultChunkSize = 256U * 1024U;
inline constexpr std::uint32_t kMaxBodySize = 4U * 1024U * 1024U;
inline constexpr std::uint32_t kMaxJsonBodySize = 1U * 1024U * 1024U;

} // namespace cloud::common
