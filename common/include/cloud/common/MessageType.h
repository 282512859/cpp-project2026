// 负责人：成员1：服务端架构/组长
#pragma once

#include <cstdint>

namespace cloud::common {

enum class MessageType : std::uint16_t {
    Hello = 1,
    Heartbeat = 2,
    ErrorResp = 3,

    RegisterReq = 100,
    RegisterResp = 101,
    LoginReq = 102,
    LoginResp = 103,
    LogoutReq = 104,
    LogoutResp = 105,

    ListReq = 200,
    ListResp = 201,
    MkdirReq = 202,
    MkdirResp = 203,
    RenameReq = 204,
    RenameResp = 205,
    DeleteReq = 206,
    DeleteResp = 207,

    UploadInitReq = 300,
    UploadInitResp = 301,
    UploadChunk = 302,
    UploadChunkAck = 303,
    UploadFinishReq = 304,
    UploadFinishResp = 305,

    DownloadInitReq = 400,
    DownloadInitResp = 401,
    DownloadChunkReq = 402,
    DownloadChunk = 403,

    ConvertReq = 500,
    ConvertResp = 501,
};

enum MessageFlags : std::uint32_t {
    FlagNone = 0,
    FlagResponse = 1U << 0U,
    FlagError = 1U << 1U,
    FlagBinary = 1U << 2U,
    FlagFinal = 1U << 3U,
    FlagPush = 1U << 4U,
};

inline constexpr std::uint32_t kKnownFlags =
    FlagResponse | FlagError | FlagBinary | FlagFinal | FlagPush;

} // namespace cloud::common
