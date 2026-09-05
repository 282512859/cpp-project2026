// 负责人：成员1：服务端架构/组长
#pragma once

#include "cloud/common/Packet.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cloud::common {

// 网络上传输的是字节，而不是 C++ 对象。这三个函数负责二者之间的转换。
// 多字节整数统一按网络字节序（高位字节在前）编码，避免不同 CPU 的端序差异。
std::array<std::uint8_t, 24> encodeHeader(const PacketHeader& header);
bool decodeHeader(const std::array<std::uint8_t, 24>& bytes,
                  PacketHeader& header, std::string& error);
std::vector<std::uint8_t> encodePacket(const Packet& packet);

class PacketStreamDecoder {
public:
    // TCP 是连续字节流：一次 append 可能只有半个包，也可能包含多个包。
    void append(const std::uint8_t* data, std::size_t size);
    // 缓冲区中凑齐一个完整包时返回 true 并移除该包；数据不足时返回 false。
    bool tryPop(Packet& packet, std::string& error);
    [[nodiscard]] std::size_t bufferedBytes() const noexcept { return buffer_.size(); }

private:
    // 保存“已经收到、但还没有组成完整报文”的字节。
    std::vector<std::uint8_t> buffer_;
};

} // namespace cloud::common
