#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace cloud::common {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    std::string finishHex();

private:
    void transform(const std::uint8_t* block);
    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t totalBytes_{};
    std::size_t buffered_{};
    bool finished_{};
};

std::string sha256Hex(const std::string& text);
std::string sha256File(const std::filesystem::path& path);
std::string randomHex(std::size_t byteCount);

} // namespace cloud::common
