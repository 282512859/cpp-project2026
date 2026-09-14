// 负责人：成员2：服务端存储（扩展功能模块）
#pragma once

#include "cloud/common/MessageType.h"

#include <cstdint>

namespace cloud::common::ext {

/**
 * 界面扩展功能使用的消息类型。
 *
 * 兼容性设计：
 * - 编号统一占用 700～799 区间，与核心协议已使用的 1～613 完全不重叠；
 * - MessageType 的底层类型是 uint16_t，因此这些编号可以与原有报文复用同一条
 *   TCP 连接，既不改协议版本号，也不影响老客户端解析原有响应；
 * - 只升级一端时，另一端收到未知编号会返回 BAD_REQUEST，原有功能仍然可用，
 *   因此升级是渐进、可回退的。
 *
 * 每个请求编号后紧跟一个响应编号（Req/Resp 成对），客户端据此校验响应类型。
 */
enum class ExtendedMessageType : std::uint16_t {
    // 回收站：软删除。节点被移动到隐藏父目录，恢复时放回原位置。
    TrashListReq = 700,
    TrashListResp = 701,
    TrashAddReq = 702,
    TrashAddResp = 703,
    TrashRestoreReq = 704,
    TrashRestoreResp = 705,
    TrashPurgeReq = 706,
    TrashPurgeResp = 707,
    TrashEmptyReq = 708,
    TrashEmptyResp = 709,

    // 归档库：节点仍保留在原目录，只额外记录归档标记。
    ArchiveListReq = 710,
    ArchiveListResp = 711,
    ArchiveAddReq = 712,
    ArchiveAddResp = 713,
    ArchiveRemoveReq = 714,
    ArchiveRemoveResp = 715,

    // 隔离区：可疑文件被移动到隐藏父目录，阻止继续浏览和下载。
    QuarantineListReq = 716,
    QuarantineListResp = 717,
    QuarantineScanReq = 718,
    QuarantineScanResp = 719,
    QuarantineReleaseReq = 720,
    QuarantineReleaseResp = 721,
    QuarantinePurgeReq = 722,
    QuarantinePurgeResp = 723,

    // 文档库：把用户的全部文件平铺列出（附上级目录名）。
    LibraryListReq = 724,
    LibraryListResp = 725,

    // 收发任务：汇总“我发出的分享”与“我接收的记录”。
    TransferListReq = 726,
    TransferListResp = 727,

    // 共享文档：别人通过权限共享给我的节点。
    ShareListReq = 728,
    ShareListResp = 729,

    // 权限共享（ACL）。
    PermissionListReq = 730,
    PermissionListResp = 731,
    PermissionGrantReq = 732,
    PermissionGrantResp = 733,
    PermissionRevokeReq = 734,
    PermissionRevokeResp = 735,
    NodePermissionReq = 736,
    NodePermissionResp = 737,

    // 外链共享。
    LinkListReq = 738,
    LinkListResp = 739,
    LinkCreateReq = 740,
    LinkCreateResp = 741,
    LinkRevokeReq = 742,
    LinkRevokeResp = 743,
    LinkOpenReq = 744,
    LinkOpenResp = 745,

    // 发现共享与屏蔽名单。
    DiscoverListReq = 746,
    DiscoverListResp = 747,
    BlockListReq = 748,
    BlockListResp = 749,
    BlockAddReq = 750,
    BlockAddResp = 751,
    BlockRemoveReq = 752,
    BlockRemoveResp = 753,

    // 权限申请。
    RequestListReq = 754,
    RequestListResp = 755,
    RequestCreateReq = 756,
    RequestCreateResp = 757,

    // 权限审核（同时处理权限申请与流程申请）。
    ReviewListReq = 758,
    ReviewListResp = 759,
    ReviewDecideReq = 760,
    ReviewDecideResp = 761,

    // 流程申请。
    WorkflowListReq = 762,
    WorkflowListResp = 763,
    WorkflowCreateReq = 764,
    WorkflowCreateResp = 765,

    // 群组文档。
    GroupListReq = 766,
    GroupListResp = 767,
    GroupCreateReq = 768,
    GroupCreateResp = 769,
    GroupItemListReq = 770,
    GroupItemListResp = 771,
    GroupItemAddReq = 772,
    GroupItemAddResp = 773,
    GroupItemRemoveReq = 774,
    GroupItemRemoveResp = 775,
    GroupMemberAddReq = 776,
    GroupMemberAddResp = 777,
    GroupItemClaimReq = 778,
    GroupItemClaimResp = 779,

    // 存储配额（左侧栏“存储空间”实时数字）。
    QuotaReq = 780,
    QuotaResp = 781,

    // 把别人授权给我的节点领取到我的文档（复用秒传复制）。
    ShareClaimReq = 782,
    ShareClaimResp = 783,
};

inline constexpr std::uint16_t kExtendedTypeFirst = 700;
inline constexpr std::uint16_t kExtendedTypeLast = 799;

/// 把扩展编号转换成报文头使用的 MessageType。
inline constexpr MessageType asMessageType(ExtendedMessageType type) noexcept {
    return static_cast<MessageType>(static_cast<std::uint16_t>(type));
}

inline constexpr bool isExtendedType(std::uint16_t raw) noexcept {
    return raw >= kExtendedTypeFirst && raw <= kExtendedTypeLast;
}

inline constexpr bool isExtendedType(MessageType type) noexcept {
    return isExtendedType(static_cast<std::uint16_t>(type));
}

} // namespace cloud::common::ext
