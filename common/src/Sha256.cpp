// 负责人：成员1：服务端架构/组长
#include "cloud/common/Sha256.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace cloud::common {
namespace {

constexpr std::array<std::uint32_t, 64> k = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

constexpr std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

} // namespace

Sha256::Sha256()
    : state_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
             0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19} {}

void Sha256::transform(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i*4]) << 24) |
               (static_cast<std::uint32_t>(block[i*4+1]) << 16) |
               (static_cast<std::uint32_t>(block[i*4+2]) << 8) | block[i*4+3];
    }
    for (int i = 16; i < 64; ++i) {
        const auto s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
        const auto s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    auto [a,b,c,d,e,f,g,h] = state_;
    for (int i = 0; i < 64; ++i) {
        const auto s1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        const auto ch = (e & f) ^ (~e & g);
        const auto t1 = h + s1 + ch + k[i] + w[i];
        const auto s0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        const auto maj = (a & b) ^ (a & c) ^ (b & c);
        const auto t2 = s0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
}

void Sha256::update(const void* data, std::size_t size) {
    if (finished_) throw std::runtime_error("SHA-256 already finalized");
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    totalBytes_ += size;
    while (size) {
        const auto take = std::min(size, buffer_.size() - buffered_);
        std::copy_n(bytes, take, buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
        buffered_ += take; bytes += take; size -= take;
        if (buffered_ == buffer_.size()) { transform(buffer_.data()); buffered_ = 0; }
    }
}

std::string Sha256::finishHex() {
    if (finished_) throw std::runtime_error("SHA-256 already finalized");
    const auto bitLength = totalBytes_ * 8;
    buffer_[buffered_++] = 0x80;
    if (buffered_ > 56) {
        std::fill(buffer_.begin()+static_cast<std::ptrdiff_t>(buffered_), buffer_.end(), 0);
        transform(buffer_.data()); buffered_ = 0;
    }
    std::fill(buffer_.begin()+static_cast<std::ptrdiff_t>(buffered_), buffer_.begin()+56, 0);
    for (int i = 0; i < 8; ++i) buffer_[63-i] = static_cast<std::uint8_t>(bitLength >> (i*8));
    transform(buffer_.data());
    finished_ = true;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto word : state_) out << std::setw(8) << word;
    return out.str();
}

std::string sha256Hex(const std::string& text) {
    Sha256 hash; hash.update(text.data(), text.size()); return hash.finishHex();
}

std::string sha256File(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file for hashing: " + path.string());
    Sha256 hash;
    std::array<char, 256 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (in.gcount() > 0) hash.update(buffer.data(), static_cast<std::size_t>(in.gcount()));
    }
    return hash.finishHex();
}

std::string randomHex(std::size_t byteCount) {
    std::random_device device;
    std::uniform_int_distribution<unsigned int> dist(0, 255);
    std::ostringstream out; out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < byteCount; ++i) out << std::setw(2) << dist(device);
    return out.str();
}

} // namespace cloud::common
