#include "cloud/common/ExecutablePath.h"

#include <array>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace cloud::common {

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(),
                                           static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        throw std::runtime_error("GetModuleFileNameW failed");
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    std::array<char, 4096> buffer{};
    const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) return std::filesystem::current_path();
    return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(length))).parent_path();
#endif
}

} // namespace cloud::common
