#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace cloud::server {

class SessionManager {
public:
    std::string create(std::int64_t userId);
    std::optional<std::int64_t> find(const std::string& token) const;
    void remove(const std::string& token);

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::int64_t> sessions_;
};

} // namespace cloud::server
