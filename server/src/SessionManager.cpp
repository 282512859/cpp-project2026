#include "cloud/server/SessionManager.h"
#include "cloud/common/Sha256.h"

namespace cloud::server {

std::string SessionManager::create(std::int64_t userId) {
    std::lock_guard lock(mutex_);
    std::string token;
    do token = cloud::common::randomHex(32); while (sessions_.contains(token));
    sessions_[token] = userId;
    return token;
}
std::optional<std::int64_t> SessionManager::find(const std::string& token) const {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(token);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}
void SessionManager::remove(const std::string& token) {
    std::lock_guard lock(mutex_); sessions_.erase(token);
}

} // namespace cloud::server
