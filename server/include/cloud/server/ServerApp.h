// 负责人：成员1：服务端架构/组长
#pragma once

#include "cloud/common/ErrorCode.h"
#include "cloud/common/JsonLite.h"
#include "cloud/common/Packet.h"
#include "cloud/common/Socket.h"
#include "cloud/server/CloudRepository.h"
#include "cloud/server/SessionManager.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace cloud::server {

class ServerApp {
public:
    ServerApp(std::string address, std::uint16_t port,
              const std::filesystem::path& runtimeRoot);
    void run();

private:
    void serveClient(cloud::common::Socket client, std::string peer);
    // 成员2：服务端存储 - 上传会话过期清理后台线程。
    void cleanupLoop();
    cloud::common::Packet handle(const cloud::common::Packet& request);
    std::int64_t requireUser(const cloud::common::json::Object& object);
    cloud::common::Packet error(const cloud::common::Packet& request,
                               cloud::common::ErrorCode code,
                               const std::string& message) const;

    std::string address_;
    std::uint16_t port_{};
    cloud::common::SocketRuntime socketRuntime_;
    CloudRepository repository_;
    SessionManager sessions_;
};

} // namespace cloud::server
