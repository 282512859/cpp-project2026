// 负责人：成员1：服务端架构/组长
#pragma once

#include "cloud/common/Packet.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cloud::common {

std::array<std::uint8_t, 24> encodeHeader(const PacketHeader& header);
bool decodeHeader(const std::array<std::uint8_t, 24>& bytes,
                  PacketHeader& header, std::string& error);
std::vector<std::uint8_t> encodePacket(const Packet& packet);

class PacketStreamDecoder {
public:
    void append(const std::uint8_t* data, std::size_t size);
    bool tryPop(Packet& packet, std::string& error);
    [[nodiscard]] std::size_t bufferedBytes() const noexcept { return buffer_.size(); }

private:
    std::vector<std::uint8_t> buffer_;
};

} // namespace cloud::common
