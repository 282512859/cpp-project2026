// 负责人：成员1：服务端架构/组长
#pragma once

#include <filesystem>

namespace cloud::common {

// Returns the directory that contains the running executable. This makes the
// portable Windows package independent of the caller's current directory.
std::filesystem::path executableDirectory();

} // namespace cloud::common
