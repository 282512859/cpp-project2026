// 负责人：成员2：服务端存储（扩展功能模块）
#pragma once

#include "cloud/common/ExtendedMessageType.h"
#include "cloud/common/JsonLite.h"
#include "cloud/common/Packet.h"
#include "cloud/server/CloudRepository.h"
#include "cloud/server/SessionManager.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace cloud::server {

/**
 * 界面扩展功能的服务端实现。
 *
 * 设计原则：
 * 1. 只新增文件，不修改既有存储实现：本类使用独立的 SQLite 连接创建 ext_* 表和
 *    索引，通过外键引用已有的 users/nodes 表，因此原有数据库可以原地升级；
 * 2. 需要修改 nodes 树的动作（回收站、隔离区）通过“隐藏父目录”实现：
 *    -1 表示回收站，-2 表示隔离区。节点被移出正常目录树后，普通 list/上传/下载
 *    都感知不到；恢复时再写回原父目录；
 * 3. 真正释放文件块（blob 引用计数、用户配额）仍交给 CloudRepository::deleteNode，
 *    避免两套实现各写一遍容易出错的回收逻辑；
 * 4. 所有写操作都在本连接的一个事务里完成，失败自动回滚，不会留下半成品数据。
 */
class ExtendedFeatureService {
public:
    /// 隐藏父目录编号：回收站与隔离区各占一个负数父目录。
    static constexpr std::int64_t kTrashParent = -1;
    static constexpr std::int64_t kQuarantineParent = -2;

    ExtendedFeatureService(const std::filesystem::path& databasePath,
                           CloudRepository& repository,
                           SessionManager& sessions);
    ~ExtendedFeatureService();
    ExtendedFeatureService(const ExtendedFeatureService&) = delete;
    ExtendedFeatureService& operator=(const ExtendedFeatureService&) = delete;

    /**
     * 处理 700～799 号扩展报文。
     * @return 处理完成后的响应包；若消息类型不属于扩展区间则返回 std::nullopt，
     *         由调用方继续走原有协议分支。
     * @throws ServiceError 业务错误，由上层统一转换为 ErrorResp。
     */
    std::optional<cloud::common::Packet> handle(const cloud::common::Packet& request,
                                                const cloud::common::json::Object& body);

private:
    /// 一条 nodes 记录的只读快照。
    struct NodeRecord {
        std::int64_t id{};
        std::int64_t ownerId{};
        std::int64_t parentId{};
        std::string name;
        bool directory{};
        std::int64_t size{};
        std::int64_t blobId{};
        bool hasBlob{};
    };

    // ---- 初始化与通用工具 ----
    void migrate();
    std::int64_t requireUser(const cloud::common::json::Object& body);
    NodeRecord loadNode(std::int64_t nodeId) const;
    NodeRecord requireOwnedNode(std::int64_t userId, std::int64_t nodeId) const;
    std::string uniqueName(std::int64_t ownerId, std::int64_t parentId,
                           const std::string& desired) const;
    std::int64_t subtreeBytes(std::int64_t nodeId) const;
    std::int64_t adminUserId() const;
    void validateNodeName(const std::string& name) const;

    cloud::common::Packet entriesPacket(const cloud::common::Packet& request,
                                        cloud::common::ext::ExtendedMessageType response,
                                        const std::vector<std::string>& entries) const;
    cloud::common::Packet objectPacket(const cloud::common::Packet& request,
                                       cloud::common::ext::ExtendedMessageType response,
                                       const std::string& json) const;

    // ---- 回收站 ----
    std::vector<std::string> listTrash(std::int64_t userId) const;
    void moveToTrash(std::int64_t userId, std::int64_t nodeId);
    void restoreFromTrash(std::int64_t userId, std::int64_t nodeId);
    void purgeTrashNode(std::int64_t userId, std::int64_t nodeId);
    std::int64_t emptyTrash(std::int64_t userId);

    // ---- 归档库 ----
    std::vector<std::string> listArchive(std::int64_t userId) const;
    void addArchive(std::int64_t userId, std::int64_t nodeId);
    void removeArchive(std::int64_t userId, std::int64_t nodeId);

    // ---- 隔离区 ----
    std::vector<std::string> listQuarantine(std::int64_t userId) const;
    std::vector<std::string> scanQuarantine(std::int64_t userId);
    void quarantineNode(std::int64_t userId, std::int64_t nodeId,
                        const std::string& reason);
    void releaseQuarantine(std::int64_t userId, std::int64_t nodeId);
    void purgeQuarantine(std::int64_t userId, std::int64_t nodeId);

    // ---- 文档库与收发任务 ----
    std::vector<std::string> listLibrary(std::int64_t userId) const;
    std::vector<std::string> listTransfers(std::int64_t userId) const;

    // ---- 权限共享（ACL）----
    std::vector<std::string> listSharedWithMe(std::int64_t userId) const;
    std::vector<std::string> listPermissions(std::int64_t userId) const;
    std::vector<std::string> listNodePermissions(std::int64_t userId,
                                                 std::int64_t nodeId) const;
    void grantPermission(std::int64_t userId, std::int64_t nodeId,
                         const std::string& grantee, bool canWrite);
    void revokePermission(std::int64_t userId, std::int64_t nodeId,
                          const std::string& grantee);
    std::int64_t claimSharedNode(std::int64_t userId, std::int64_t nodeId);

    // ---- 外链共享 ----
    std::vector<std::string> listLinks(std::int64_t userId) const;
    std::string createLink(std::int64_t userId, std::int64_t nodeId, std::int64_t hours,
                           const std::string& password, bool discoverable);
    void revokeLink(std::int64_t userId, const std::string& token);
    std::int64_t openLink(std::int64_t userId, const std::string& token,
                          const std::string& password);
    std::vector<std::string> discoverLinks(std::int64_t userId,
                                           const std::string& keyword) const;

    // ---- 屏蔽名单 ----
    std::vector<std::string> listBlocks(std::int64_t userId) const;
    void addBlock(std::int64_t userId, std::int64_t ownerId, const std::string& reason);
    void removeBlock(std::int64_t userId, std::int64_t blockId);

    // ---- 权限申请与流程申请 ----
    std::vector<std::string> listMyRequests(std::int64_t userId) const;
    void createRequest(std::int64_t userId, std::int64_t nodeId, bool canWrite,
                       const std::string& reason);
    std::vector<std::string> listReviews(std::int64_t userId) const;
    void decideReview(std::int64_t userId, const std::string& kind,
                      std::int64_t reviewId, bool approve,
                      const std::string& comment);
    std::vector<std::string> listWorkflows(std::int64_t userId) const;
    void createWorkflow(std::int64_t userId, const std::string& kind,
                        const std::string& title, const std::string& detail);

    // ---- 群组文档 ----
    std::vector<std::string> listGroups(std::int64_t userId) const;
    std::int64_t createGroup(std::int64_t userId, const std::string& name);
    std::vector<std::string> listGroupItems(std::int64_t userId,
                                            std::int64_t groupId) const;
    void addGroupItem(std::int64_t userId, std::int64_t groupId, std::int64_t nodeId);
    void removeGroupItem(std::int64_t userId, std::int64_t groupId, std::int64_t itemId);
    void addGroupMember(std::int64_t userId, std::int64_t groupId,
                        const std::string& member);
    std::int64_t claimGroupItem(std::int64_t userId, std::int64_t groupId,
                                std::int64_t itemId);

    // ---- 跨用户复制（共享领取的统一原语）----
    std::int64_t insertCopiedNode(std::int64_t ownerId, std::int64_t parentId,
                                  const std::string& name, const NodeRecord& source);
    std::int64_t copyNodeForUser(std::int64_t requesterId, std::int64_t sourceNodeId,
                                 std::int64_t targetParentId,
                                 const std::string& desiredName);

    // ---- 配额 ----
    std::string quotaInfo(std::int64_t userId) const;

    /// 判断群组是否存在且当前用户是成员。
    void requireMembership(std::int64_t userId, std::int64_t groupId) const;

    sqlite3* db_{};
    CloudRepository& repository_;
    SessionManager& sessions_;
    mutable std::mutex mutex_;
};

} // namespace cloud::server
