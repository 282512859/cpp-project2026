#include "cloud/common/ProtocolCodec.h"
#include "cloud/common/ProtocolConstants.h"

#include <algorithm>
#include <stdexcept>

namespace cloud::common {
namespace {

void put16(std::uint8_t* out, std::uint16_t value) {
    out[0] = static_cast<std::uint8_t>(value >> 8U);
    out[1] = static_cast<std::uint8_t>(value);
}
void put32(std::uint8_t* out, std::uint32_t value) {
    for (int i = 3; i >= 0; --i) out[3 - i] = static_cast<std::uint8_t>(value >> (i * 8));
}
void put64(std::uint8_t* out, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) out[7 - i] = static_cast<std::uint8_t>(value >> (i * 8));
}
std::uint16_t get16(const std::uint8_t* in) {
    return static_cast<std::uint16_t>((in[0] << 8U) | in[1]);
}
std::uint32_t get32(const std::uint8_t* in) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value = (value << 8U) | in[i];
    return value;
}
std::uint64_t get64(const std::uint8_t* in) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8U) | in[i];
    return value;
}

} // namespace

std::array<std::uint8_t, 24> encodeHeader(const PacketHeader& h) {
    std::array<std::uint8_t, 24> bytes{};
    put32(bytes.data(), h.magic);
    put16(bytes.data() + 4, h.version);
    put16(bytes.data() + 6, static_cast<std::uint16_t>(h.type));
    put32(bytes.data() + 8, h.flags);
    put32(bytes.data() + 12, h.bodyLength);
    put64(bytes.data() + 16, h.requestId);
    return bytes;
}

bool decodeHeader(const std::array<std::uint8_t, 24>& bytes,
                  PacketHeader& h, std::string& error) {
    h.magic = get32(bytes.data());
    h.version = get16(bytes.data() + 4);
    h.type = static_cast<MessageType>(get16(bytes.data() + 6));
    h.flags = get32(bytes.data() + 8);
    h.bodyLength = get32(bytes.data() + 12);
    h.requestId = get64(bytes.data() + 16);
    if (h.magic != kProtocolMagic) { error = "invalid packet magic"; return false; }
    if (h.version != kProtocolVersion) { error = "unsupported protocol version"; return false; }
    if ((h.flags & ~kKnownFlags) != 0) { error = "unknown packet flags"; return false; }
    if (h.bodyLength > kMaxBodySize) { error = "packet body exceeds limit"; return false; }
    return true;
}

std::vector<std::uint8_t> encodePacket(const Packet& packet) {
    PacketHeader h = packet.header;
    h.bodyLength = static_cast<std::uint32_t>(packet.body.size());
    const auto header = encodeHeader(h);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(header.size() + packet.body.size());
    bytes.insert(bytes.end(), header.begin(), header.end());
    bytes.insert(bytes.end(), packet.body.begin(), packet.body.end());
    return bytes;
}

void PacketStreamDecoder::append(const std::uint8_t* data, std::size_t size) {
    buffer_.insert(buffer_.end(), data, data + size);
}

bool PacketStreamDecoder::tryPop(Packet& packet, std::string& error) {
    if (buffer_.size() < kPacketHeaderSize) return false;
    std::array<std::uint8_t, 24> raw{};
    std::copy_n(buffer_.begin(), raw.size(), raw.begin());
    PacketHeader h;
    if (!decodeHeader(raw, h, error)) throw std::runtime_error(error);
    const auto total = kPacketHeaderSize + static_cast<std::size_t>(h.bodyLength);
    if (buffer_.size() < total) return false;
    packet.header = h;
    packet.body.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(kPacketHeaderSize),
                       buffer_.begin() + static_cast<std::ptrdiff_t>(total));
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total));
    return true;
}

} // namespace cloud::common
