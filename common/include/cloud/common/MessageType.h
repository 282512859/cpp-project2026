// 负责人：成员1：服务端架构/组长
#pragma once

#include <cstdint>

namespace cloud::common {

// 每个 TCP 报文都带有一个消息类型。显式指定数值非常重要：客户端和服务端
// 即使分别编译，也必须对同一个数字有完全相同的理解。
enum class MessageType : std::uint16_t {
    // 连接与通用消息（1～99）。
    Hello = 1,
    Heartbeat = 2,
    ErrorResp = 3,

    // 账户与会话消息（100～199）。Req 表示请求，Resp 表示响应。
    RegisterReq = 100,
    RegisterResp = 101,
    LoginReq = 102,
    LoginResp = 103,
    LogoutReq = 104,
    LogoutResp = 105,

    // 远端目录和文件节点操作（200～299）。
    ListReq = 200,
    ListResp = 201,
    MkdirReq = 202,
    MkdirResp = 203,
    RenameReq = 204,
    RenameResp = 205,
    DeleteReq = 206,
    DeleteResp = 207,

    // 上传分为初始化、多个数据块、结束确认三个阶段（300～399）。
    UploadInitReq = 300,
    UploadInitResp = 301,
    UploadChunk = 302,
    UploadChunkAck = 303,
    UploadFinishReq = 304,
    UploadFinishResp = 305,

    // 下载先获取元数据，再按 offset 分块读取（400～499）。
    DownloadInitReq = 400,
    DownloadInitResp = 401,
    DownloadChunkReq = 402,
    DownloadChunk = 403,

    ShareCreateReq = 500,
    ShareCreateResp = 501,
    ShareClaimReq = 502,
    ShareClaimResp = 503,

    ConvertReq = 600,
    ConvertResp = 601,
};

// 标志位使用二进制的不同 bit 表示多个可以同时成立的属性。
// 例如一个下载数据包可以同时设置 Response、Binary 和 Final。
enum MessageFlags : std::uint32_t {
    FlagNone = 0,
    FlagResponse = 1U << 0U,
    FlagError = 1U << 1U,
    FlagBinary = 1U << 2U,
    FlagFinal = 1U << 3U,
    FlagPush = 1U << 4U,
};

// 解码时只允许这些已知 bit，其他 bit 很可能表示协议版本不匹配或数据损坏。
inline constexpr std::uint32_t kKnownFlags =
    FlagResponse | FlagError | FlagBinary | FlagFinal | FlagPush;

} // namespace cloud::common
