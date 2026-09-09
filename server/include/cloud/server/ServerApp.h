// 负责人：成员1：服务端架构/组长
#pragma once

#include "cloud/common/ErrorCode.h"
#include "cloud/common/JsonLite.h"
#include "cloud/common/Packet.h"
#include "cloud/common/Socket.h"
#include "cloud/server/CloudRepository.h"
#include "cloud/server/MarkdownConverter.h"
#include "cloud/server/SessionManager.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace cloud::server {

// 服务端总协调器：监听连接、解析请求、检查登录态，再调用存储和转换组件完成业务。
// 网络层不直接编写 SQL，存储层也不接触 Socket，这样各层职责更清晰。
class ServerApp {
public:
    ServerApp(std::string address, std::uint16_t port,
              const std::filesystem::path& runtimeRoot);
    void run();

private:
    // 每个客户端连接由一个独立线程执行“收请求 -> 处理 -> 发响应”。
    void serveClient(cloud::common::Socket client, std::string peer);
    // 成员2：服务端存储 - 上传会话过期清理后台线程。
    void cleanupLoop();
    // 所有消息类型都从这里进入，再分发到会话、存储或文档转换功能。
    cloud::common::Packet handle(const cloud::common::Packet& request);
    // 从 JSON 中取出 token 并换成 userId；失败时抛出 Unauthorized。
    std::int64_t requireUser(const cloud::common::json::Object& object);
    // 将业务异常统一包装成协议规定的 ErrorResp。
    cloud::common::Packet error(const cloud::common::Packet& request,
                               cloud::common::ErrorCode code,
                               const std::string& message) const;

    std::string address_;
    std::uint16_t port_{};
    // 声明顺序决定构造顺序：网络运行库先初始化，存储和会话对象随后初始化。
    cloud::common::SocketRuntime socketRuntime_;
    CloudRepository repository_;       // SQLite 元数据、目录树和文件块。
    MarkdownConverter markdownConverter_; // 文档转换和预览辅助工具。
    SessionManager sessions_;          // 当前服务端进程内的登录 token。
};

} // namespace cloud::server
