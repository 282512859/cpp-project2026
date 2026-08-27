// 负责人：成员2：服务端存储
#pragma once

#include "cloud/common/ErrorCode.h"

#include <stdexcept>
#include <string>

namespace cloud::server {

class ServiceError : public std::runtime_error {
public:
    ServiceError(cloud::common::ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}
    [[nodiscard]] cloud::common::ErrorCode code() const noexcept { return code_; }
private:
    cloud::common::ErrorCode code_;
};

} // namespace cloud::server
