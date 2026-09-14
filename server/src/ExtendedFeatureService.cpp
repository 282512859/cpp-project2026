// 负责人：成员2：服务端存储（扩展功能模块）
//
// 本文件实现界面侧栏与工具栏所需的扩展功能：
//   1. 回收站（软删除、恢复、彻底删除、清空）；
//   2. 归档库；
//   3. 隔离区（可疑扩展名扫描、解除隔离、彻底删除）；
//   4. 文档库（全部文件平铺）；
//   5. 收发任务（我发出的分享 / 我接收的记录）；
//   6. 权限共享 ACL、权限申请与审核、流程申请；
//   7. 外链共享、发现共享、屏蔽名单；
//   8. 群组文档；
//   9. 存储配额查询。
//
// 与既有存储层的关系：
// - 只新增 ext_* 表，通过外键引用 users/nodes，原有表结构不变；
// - 需要真正释放文件块时调用 CloudRepository::deleteNode，复用其引用计数与配额回收；
// - 跨用户“领取”统一走 copyNodeForUser()，共享 blob 不复制物理文件（秒传）。

#include "cloud/server/ExtendedFeatureService.h"

#include "cloud/common/ErrorCode.h"
#include "cloud/common/Sha256.h"
#include "cloud/server/ServiceError.h"
#include "cloud/server/SqliteApi.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <utility>

// SQLite 的忙等待设置未包含在项目自带的声明垫片中，这里按官方签名补充声明。
// 扩展模块与 CloudRepository 使用同一个数据库文件、两条连接，事务之间依靠
// busy timeout 等待对方提交，而不是直接返回 SQLITE_BUSY。
extern "C" int sqlite3_busy_timeout(sqlite3*, int);

namespace cloud::server {
namespace {

using cloud::common::ErrorCode;
using cloud::common::ext::ExtendedMessageType;
using cloud::common::makeJsonPacket;
namespace json = cloud::common::json;

std::int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string upperCase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string lowerCase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

/// 可执行类扩展名清单：隔离区扫描命中这些后缀时把文件移入隔离区。
bool riskyExtension(const std::string& name) {
    static const char* kRisky[] = {".exe", ".dll", ".bat", ".cmd",  ".com", ".scr",
                                   ".msi", ".vbs", ".vbe", ".jse",  ".ps1", ".psm1",
                                   ".jar", ".sh",  ".reg", ".lnk",  ".hta", ".cpl"};
    const auto lower = lowerCase(name);
    for (const auto* extension : kRisky) {
        const std::string suffix = extension;
        if (lower.size() > suffix.size() &&
            lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }
    return false;
}

std::string permissionText(bool canWrite) {
    return canWrite ? "可读写" : "只读";
}

/// sqlite3_stmt 的 RAII 封装：构造时编译，析构时 finalize。
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
            throw ServiceError(ErrorCode::DbError, sqlite3_errmsg(db));
    }
    ~Stmt() {
        if (stmt_)
            sqlite3_finalize(stmt_);
    }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& text(int index, const std::string& value) {
        if (sqlite3_bind_text(stmt_, index, value.c_str(),
                              static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw ServiceError(ErrorCode::DbError, "bind text failed");
        return *this;
    }
    Stmt& integer(int index, std::int64_t value) {
        if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK)
            throw ServiceError(ErrorCode::DbError, "bind integer failed");
        return *this;
    }
    Stmt& null(int index) {
        if (sqlite3_bind_null(stmt_, index) != SQLITE_OK)
            throw ServiceError(ErrorCode::DbError, "bind null failed");
        return *this;
    }

    /// 执行一行查询：有数据返回 true，结束时返回 false。
    bool row() {
        const int status = sqlite3_step(stmt_);
        if (status == SQLITE_ROW) return true;
        if (status == SQLITE_DONE) return false;
        throw ServiceError(ErrorCode::DbError, "sqlite step failed");
    }
    /// 执行不带返回值的语句。
    void run() {
        if (sqlite3_step(stmt_) != SQLITE_DONE)
            throw ServiceError(ErrorCode::DbError, "sqlite step failed");
    }

    std::int64_t intAt(int index) const { return sqlite3_column_int64(stmt_, index); }
    std::string textAt(int index) const {
        const auto* value = sqlite3_column_text(stmt_, index);
        return value ? std::string(reinterpret_cast<const char*>(value)) : std::string{};
    }
    bool isNull(int index) const { return sqlite3_column_type(stmt_, index) == SQLITE_NULL; }

private:
    sqlite3* db_{};
    sqlite3_stmt* stmt_{};
};

void execSql(sqlite3* db, const std::string& sql) {
    char* message = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string error = message ? message : "sqlite exec failed";
        if (message)
            sqlite3_free(message);
        throw ServiceError(ErrorCode::DbError, error);
    }
}

/// 事务守卫：正常提交，异常或提前返回时自动回滚。
class Txn {
public:
    explicit Txn(sqlite3* db) : db_(db) { execSql(db_, "BEGIN IMMEDIATE"); }
    ~Txn() {
        if (!committed_) {
            try {
                execSql(db_, "ROLLBACK");
            } catch (...) {
                // 回滚失败通常意味着连接已经出错，此处不再向外抛异常。
            }
        }
    }
    Txn(const Txn&) = delete;
    Txn& operator=(const Txn&) = delete;

    void commit() {
        execSql(db_, "COMMIT");
        committed_ = true;
    }

private:
    sqlite3* db_{};
    bool committed_{false};
};

/// 统一构造一条列表项。客户端只依赖这些固定字段，便于复用到所有侧栏视图。
std::string makeRow(const std::string& kind, std::int64_t id, const std::string& name,
                    const std::string& owner, const std::string& detail,
                    std::int64_t size, std::int64_t modified, bool directory,
                    const std::string& token, const std::string& extraRaw) {
    return json::object({
        {"kind", json::quote(kind)},
        {"id", std::to_string(id)},
        {"name", json::quote(name)},
        {"owner", json::quote(owner)},
        {"detail", json::quote(detail)},
        {"size", std::to_string(size)},
        {"modified", std::to_string(modified)},
        {"directory", directory ? "true" : "false"},
        {"token", json::quote(token)},
        {"extra", extraRaw.empty() ? std::string("0") : extraRaw},
    });
}

std::int64_t optionalInt(const json::Object& object, const std::string& key,
                         std::int64_t fallback) {
    const auto it = object.find(key);
    if (it == object.end()) return fallback;
    try {
        return std::stoll(it->second);
    } catch (...) {
        return fallback;
    }
}

bool optionalBool(const json::Object& object, const std::string& key, bool fallback) {
    const auto it = object.find(key);
    if (it == object.end()) return fallback;
    return it->second == "true";
}

std::string statusOfAclRequest(const std::string& raw) {
    if (raw == "approved") return "已批准";
    if (raw == "rejected") return "已驳回";
    return "待审批";
}

std::string statusOfWorkflow(const std::string& raw) {
    if (raw == "approved") return "已通过";
    if (raw == "rejected") return "已退回";
    return "待审批";
}

} // namespace

ExtendedFeatureService::ExtendedFeatureService(const std::filesystem::path& databasePath,
                                               CloudRepository& repository,
                                               SessionManager& sessions)
    : repository_(repository), sessions_(sessions) {
    if (sqlite3_open(databasePath.string().c_str(), &db_) != SQLITE_OK) {
        const std::string error = db_ ? sqlite3_errmsg(db_) : "cannot open SQLite database";
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        throw ServiceError(ErrorCode::DbError, error);
    }
    sqlite3_busy_timeout(db_, 5000);
    sqlite3_busy_timeout(repository_.handle(), 5000);
    migrate();
}

ExtendedFeatureService::~ExtendedFeatureService() {
    if (db_)
        sqlite3_close(db_);
    db_ = nullptr;
}

/**
 * 创建扩展功能所需的表。
 *
 * 所有表都以 ext_ 开头并引用既有 users/nodes，因此：
 * - 旧数据库可以直接升级，不需要重建；
 * - 旧版本程序删除这些表也不影响核心功能。
 */
void ExtendedFeatureService::migrate() {
    execSql(db_, "PRAGMA foreign_keys=ON;");
    execSql(db_, R"sql(
CREATE TABLE IF NOT EXISTS ext_trash(
 node_id INTEGER PRIMARY KEY REFERENCES nodes(id) ON DELETE CASCADE,
 owner_id INTEGER NOT NULL,
 original_parent INTEGER NOT NULL,
 original_name TEXT NOT NULL,
 size INTEGER NOT NULL DEFAULT 0,
 deleted_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS ext_archive(
 node_id INTEGER PRIMARY KEY REFERENCES nodes(id) ON DELETE CASCADE,
 owner_id INTEGER NOT NULL,
 archived_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS ext_quarantine(
 node_id INTEGER PRIMARY KEY REFERENCES nodes(id) ON DELETE CASCADE,
 owner_id INTEGER NOT NULL,
 original_parent INTEGER NOT NULL,
 original_name TEXT NOT NULL,
 reason TEXT NOT NULL DEFAULT '',
 quarantined_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS ext_acl(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 node_id INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
 owner_id INTEGER NOT NULL,
 grantee_id INTEGER NOT NULL,
 can_write INTEGER NOT NULL DEFAULT 0,
 created_at INTEGER NOT NULL,
 UNIQUE(node_id,grantee_id)
);

CREATE TABLE IF NOT EXISTS ext_acl_requests(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 requester_id INTEGER NOT NULL,
 owner_id INTEGER NOT NULL,
 node_id INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
 can_write INTEGER NOT NULL DEFAULT 0,
 reason TEXT NOT NULL DEFAULT '',
 status TEXT NOT NULL DEFAULT 'pending',
 created_at INTEGER NOT NULL,
 decided_at INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS ext_workflows(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 requester_id INTEGER NOT NULL,
 reviewer_id INTEGER NOT NULL DEFAULT 0,
 kind TEXT NOT NULL,
 title TEXT NOT NULL,
 detail TEXT NOT NULL DEFAULT '',
 status TEXT NOT NULL DEFAULT 'submitted',
 created_at INTEGER NOT NULL,
 decided_at INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS ext_share_links(
 token TEXT PRIMARY KEY,
 owner_id INTEGER NOT NULL,
 node_id INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
 password_hash TEXT NOT NULL DEFAULT '',
 salt TEXT NOT NULL DEFAULT '',
 discoverable INTEGER NOT NULL DEFAULT 0,
 created_at INTEGER NOT NULL,
 expires_at INTEGER NOT NULL,
 revoked INTEGER NOT NULL DEFAULT 0,
 open_count INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS ext_link_claims(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 token TEXT NOT NULL,
 owner_id INTEGER NOT NULL,
 claimer_id INTEGER NOT NULL,
 name TEXT NOT NULL,
 size INTEGER NOT NULL DEFAULT 0,
 claimed_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS ext_blocks(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 user_id INTEGER NOT NULL,
 owner_id INTEGER NOT NULL,
 owner_name TEXT NOT NULL DEFAULT '',
 reason TEXT NOT NULL DEFAULT '',
 created_at INTEGER NOT NULL,
 UNIQUE(user_id,owner_id)
);

CREATE TABLE IF NOT EXISTS ext_groups(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 name TEXT NOT NULL,
 owner_id INTEGER NOT NULL,
 created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS ext_group_members(
 group_id INTEGER NOT NULL REFERENCES ext_groups(id) ON DELETE CASCADE,
 user_id INTEGER NOT NULL,
 role TEXT NOT NULL DEFAULT 'member',
 joined_at INTEGER NOT NULL,
 UNIQUE(group_id,user_id)
);

CREATE TABLE IF NOT EXISTS ext_group_items(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 group_id INTEGER NOT NULL REFERENCES ext_groups(id) ON DELETE CASCADE,
 node_id INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
 added_by INTEGER NOT NULL,
 created_at INTEGER NOT NULL,
 UNIQUE(group_id,node_id)
);

CREATE INDEX IF NOT EXISTS idx_ext_trash_owner ON ext_trash(owner_id);
CREATE INDEX IF NOT EXISTS idx_ext_archive_owner ON ext_archive(owner_id);
CREATE INDEX IF NOT EXISTS idx_ext_quarantine_owner ON ext_quarantine(owner_id);
CREATE INDEX IF NOT EXISTS idx_ext_acl_grantee ON ext_acl(grantee_id);
CREATE INDEX IF NOT EXISTS idx_ext_acl_owner ON ext_acl(owner_id);
CREATE INDEX IF NOT EXISTS idx_ext_requests_owner ON ext_acl_requests(owner_id,status);
CREATE INDEX IF NOT EXISTS idx_ext_workflows_reviewer ON ext_workflows(reviewer_id,status);
CREATE INDEX IF NOT EXISTS idx_ext_links_owner ON ext_share_links(owner_id);
CREATE INDEX IF NOT EXISTS idx_ext_links_public ON ext_share_links(discoverable,revoked);
CREATE INDEX IF NOT EXISTS idx_ext_group_members ON ext_group_members(user_id);
)sql");
}

std::optional<cloud::common::Packet> ExtendedFeatureService::handle(
    const cloud::common::Packet& request, const json::Object& body) {
    const auto raw = static_cast<std::uint16_t>(request.header.type);
    if (!cloud::common::ext::isExtendedType(raw))
        return std::nullopt;

    const auto message = static_cast<ExtendedMessageType>(raw);
    std::lock_guard lock(mutex_);
    const auto userId = requireUser(body);

    switch (message) {
    case ExtendedMessageType::TrashListReq:
        return entriesPacket(request, ExtendedMessageType::TrashListResp, listTrash(userId));
    case ExtendedMessageType::TrashAddReq: {
        const auto nodeId = json::requireInt(body, "nodeId");
        moveToTrash(userId, nodeId);
        return objectPacket(request, ExtendedMessageType::TrashAddResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }
    case ExtendedMessageType::TrashRestoreReq: {
        const auto nodeId = json::requireInt(body, "nodeId");
        restoreFromTrash(userId, nodeId);
        return objectPacket(request, ExtendedMessageType::TrashRestoreResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }
    case ExtendedMessageType::TrashPurgeReq: {
        const auto nodeId = json::requireInt(body, "nodeId");
        purgeTrashNode(userId, nodeId);
        return objectPacket(request, ExtendedMessageType::TrashPurgeResp,
                            json::object({{"ok", "true"}}));
    }
    case ExtendedMessageType::TrashEmptyReq: {
        const auto removed = emptyTrash(userId);
        return objectPacket(request, ExtendedMessageType::TrashEmptyResp,
                            json::object({{"removed", std::to_string(removed)}}));
    }

    case ExtendedMessageType::ArchiveListReq:
        return entriesPacket(request, ExtendedMessageType::ArchiveListResp,
                             listArchive(userId));
    case ExtendedMessageType::ArchiveAddReq: {
        const auto nodeId = json::requireInt(body, "nodeId");
        addArchive(userId, nodeId);
        return objectPacket(request, ExtendedMessageType::ArchiveAddResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }
    case ExtendedMessageType::ArchiveRemoveReq: {
        const auto nodeId = json::requireInt(body, "nodeId");
        removeArchive(userId, nodeId);
        return objectPacket(request, ExtendedMessageType::ArchiveRemoveResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }

    case ExtendedMessageType::QuarantineListReq:
        return entriesPacket(request, ExtendedMessageType::QuarantineListResp,
                             listQuarantine(userId));
    case ExtendedMessageType::QuarantineScanReq: {
        const auto findings = scanQuarantine(userId);
        std::vector<std::string> quoted;
        quoted.reserve(findings.size());
        for (const auto& name : findings)
            quoted.push_back(json::quote(name));
        return objectPacket(request, ExtendedMessageType::QuarantineScanResp,
                            json::object({{"count", std::to_string(findings.size())},
                                          {"findings", json::array(quoted)}}));
    }
    case ExtendedMessageType::QuarantineReleaseReq: {
        const auto nodeId = json::requireInt(body, "nodeId");
        releaseQuarantine(userId, nodeId);
        return objectPacket(request, ExtendedMessageType::QuarantineReleaseResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }
    case ExtendedMessageType::QuarantinePurgeReq: {
        const auto nodeId = json::requireInt(body, "nodeId");
        purgeQuarantine(userId, nodeId);
        return objectPacket(request, ExtendedMessageType::QuarantinePurgeResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }

    case ExtendedMessageType::LibraryListReq:
        return entriesPacket(request, ExtendedMessageType::LibraryListResp,
                             listLibrary(userId));
    case ExtendedMessageType::TransferListReq:
        return entriesPacket(request, ExtendedMessageType::TransferListResp,
                             listTransfers(userId));

    case ExtendedMessageType::ShareListReq:
        return entriesPacket(request, ExtendedMessageType::ShareListResp,
                             listSharedWithMe(userId));
    case ExtendedMessageType::PermissionListReq:
        return entriesPacket(request, ExtendedMessageType::PermissionListResp,
                             listPermissions(userId));
    case ExtendedMessageType::NodePermissionReq:
        return entriesPacket(request, ExtendedMessageType::NodePermissionResp,
                             listNodePermissions(userId, json::requireInt(body, "nodeId")));
    case ExtendedMessageType::PermissionGrantReq: {
        const auto nodeId = json::requireInt(body, "nodeId");
        grantPermission(userId, nodeId, json::requireString(body, "grantee"),
                        optionalBool(body, "canWrite", false));
        return objectPacket(request, ExtendedMessageType::PermissionGrantResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }
    case ExtendedMessageType::PermissionRevokeReq: {
        const auto nodeId = json::requireInt(body, "nodeId");
        revokePermission(userId, nodeId, json::requireString(body, "grantee"));
        return objectPacket(request, ExtendedMessageType::PermissionRevokeResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }
    case ExtendedMessageType::ShareClaimReq: {
        const auto nodeId = claimSharedNode(userId, json::requireInt(body, "nodeId"));
        return objectPacket(request, ExtendedMessageType::ShareClaimResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }

    case ExtendedMessageType::LinkListReq:
        return entriesPacket(request, ExtendedMessageType::LinkListResp, listLinks(userId));
    case ExtendedMessageType::LinkCreateReq: {
        const auto token = createLink(userId, json::requireInt(body, "nodeId"),
                                      optionalInt(body, "hours", 24),
                                      json::optionalString(body, "password", {}),
                                      optionalBool(body, "discoverable", false));
        return objectPacket(request, ExtendedMessageType::LinkCreateResp,
                            json::object({{"token", json::quote(token)}}));
    }
    case ExtendedMessageType::LinkRevokeReq: {
        // 外链码字段名必须与登录会话的 "token" 区分开，否则会覆盖鉴权字段。
        revokeLink(userId, json::requireString(body, "shareToken"));
        return objectPacket(request, ExtendedMessageType::LinkRevokeResp,
                            json::object({{"ok", "true"}}));
    }
    case ExtendedMessageType::LinkOpenReq: {
        const auto nodeId = openLink(userId, json::requireString(body, "shareToken"),
                                     json::optionalString(body, "password", {}));
        return objectPacket(request, ExtendedMessageType::LinkOpenResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }
    case ExtendedMessageType::DiscoverListReq:
        return entriesPacket(request, ExtendedMessageType::DiscoverListResp,
                             discoverLinks(userId, json::optionalString(body, "keyword", {})));

    case ExtendedMessageType::BlockListReq:
        return entriesPacket(request, ExtendedMessageType::BlockListResp, listBlocks(userId));
    case ExtendedMessageType::BlockAddReq: {
        const auto token = json::requireString(body, "shareToken");
        Stmt stmt(db_, "SELECT owner_id,node_id FROM ext_share_links WHERE token=?");
        stmt.text(1, token);
        if (!stmt.row())
            throw ServiceError(ErrorCode::NotFound, "share link not found");
        addBlock(userId, stmt.intAt(0), json::optionalString(body, "reason", "用户主动屏蔽"));
        return objectPacket(request, ExtendedMessageType::BlockAddResp,
                            json::object({{"ok", "true"}}));
    }
    case ExtendedMessageType::BlockRemoveReq: {
        removeBlock(userId, json::requireInt(body, "blockId"));
        return objectPacket(request, ExtendedMessageType::BlockRemoveResp,
                            json::object({{"ok", "true"}}));
    }

    case ExtendedMessageType::RequestListReq:
        return entriesPacket(request, ExtendedMessageType::RequestListResp,
                             listMyRequests(userId));
    case ExtendedMessageType::RequestCreateReq: {
        createRequest(userId, json::requireInt(body, "nodeId"),
                      optionalBool(body, "canWrite", false),
                      json::optionalString(body, "reason", {}));
        return objectPacket(request, ExtendedMessageType::RequestCreateResp,
                            json::object({{"ok", "true"}}));
    }
    case ExtendedMessageType::ReviewListReq:
        return entriesPacket(request, ExtendedMessageType::ReviewListResp, listReviews(userId));
    case ExtendedMessageType::ReviewDecideReq: {
        decideReview(userId, json::requireString(body, "kind"),
                     json::requireInt(body, "reviewId"), json::requireBool(body, "approve"),
                     json::optionalString(body, "comment", {}));
        return objectPacket(request, ExtendedMessageType::ReviewDecideResp,
                            json::object({{"ok", "true"}}));
    }
    case ExtendedMessageType::WorkflowListReq:
        return entriesPacket(request, ExtendedMessageType::WorkflowListResp,
                             listWorkflows(userId));
    case ExtendedMessageType::WorkflowCreateReq: {
        createWorkflow(userId, json::requireString(body, "kind"),
                       json::requireString(body, "title"),
                       json::optionalString(body, "detail", {}));
        return objectPacket(request, ExtendedMessageType::WorkflowCreateResp,
                            json::object({{"ok", "true"}}));
    }

    case ExtendedMessageType::GroupListReq:
        return entriesPacket(request, ExtendedMessageType::GroupListResp, listGroups(userId));
    case ExtendedMessageType::GroupCreateReq: {
        const auto groupId = createGroup(userId, json::requireString(body, "name"));
        return objectPacket(request, ExtendedMessageType::GroupCreateResp,
                            json::object({{"groupId", std::to_string(groupId)}}));
    }
    case ExtendedMessageType::GroupItemListReq:
        return entriesPacket(request, ExtendedMessageType::GroupItemListResp,
                             listGroupItems(userId, json::requireInt(body, "groupId")));
    case ExtendedMessageType::GroupItemAddReq: {
        addGroupItem(userId, json::requireInt(body, "groupId"),
                     json::requireInt(body, "nodeId"));
        return objectPacket(request, ExtendedMessageType::GroupItemAddResp,
                            json::object({{"ok", "true"}}));
    }
    case ExtendedMessageType::GroupItemRemoveReq: {
        removeGroupItem(userId, json::requireInt(body, "groupId"),
                        json::requireInt(body, "itemId"));
        return objectPacket(request, ExtendedMessageType::GroupItemRemoveResp,
                            json::object({{"ok", "true"}}));
    }
    case ExtendedMessageType::GroupMemberAddReq: {
        addGroupMember(userId, json::requireInt(body, "groupId"),
                       json::requireString(body, "member"));
        return objectPacket(request, ExtendedMessageType::GroupMemberAddResp,
                            json::object({{"ok", "true"}}));
    }
    case ExtendedMessageType::GroupItemClaimReq: {
        const auto nodeId = claimGroupItem(userId, json::requireInt(body, "groupId"),
                                           json::requireInt(body, "itemId"));
        return objectPacket(request, ExtendedMessageType::GroupItemClaimResp,
                            json::object({{"nodeId", std::to_string(nodeId)}}));
    }

    case ExtendedMessageType::QuotaReq:
        return objectPacket(request, ExtendedMessageType::QuotaResp, quotaInfo(userId));

    default:
        throw ServiceError(ErrorCode::BadRequest, "unsupported extended message type");
    }
}

// ---------------------------------------------------------------- 通用工具

std::int64_t ExtendedFeatureService::requireUser(const json::Object& body) {
    const auto token = json::requireString(body, "token");
    const auto user = sessions_.find(token);
    if (!user)
        throw ServiceError(ErrorCode::Unauthorized, "login required or session expired");
    return *user;
}

ExtendedFeatureService::NodeRecord ExtendedFeatureService::loadNode(std::int64_t nodeId) const {
    Stmt stmt(db_,
              "SELECT id,owner_id,parent_id,name,is_directory,size,blob_id "
              "FROM nodes WHERE id=?");
    stmt.integer(1, nodeId);
    if (!stmt.row())
        throw ServiceError(ErrorCode::NotFound, "node not found");

    NodeRecord node;
    node.id = stmt.intAt(0);
    node.ownerId = stmt.intAt(1);
    node.parentId = stmt.intAt(2);
    node.name = stmt.textAt(3);
    node.directory = stmt.intAt(4) != 0;
    node.size = stmt.intAt(5);
    node.hasBlob = !stmt.isNull(6);
    node.blobId = node.hasBlob ? stmt.intAt(6) : 0;
    return node;
}

ExtendedFeatureService::NodeRecord ExtendedFeatureService::requireOwnedNode(
    std::int64_t userId, std::int64_t nodeId) const {
    const auto node = loadNode(nodeId);
    // 统一返回 NotFound，避免通过错误码探测其他用户的节点是否存在。
    if (node.ownerId != userId)
        throw ServiceError(ErrorCode::NotFound, "node not found in your space");
    return node;
}

std::string ExtendedFeatureService::uniqueName(std::int64_t ownerId, std::int64_t parentId,
                                               const std::string& desired) const {
    for (int attempt = 0; attempt < 50; ++attempt) {
        const std::string candidate =
            attempt == 0 ? desired : desired + " (" + std::to_string(attempt + 1) + ")";
        Stmt probe(db_, "SELECT 1 FROM nodes WHERE owner_id=? AND parent_id=? AND name=?");
        probe.integer(1, ownerId).integer(2, parentId).text(3, candidate);
        if (!probe.row())
            return candidate;
    }
    throw ServiceError(ErrorCode::NameConflict, "cannot find an unused name");
}

std::int64_t ExtendedFeatureService::subtreeBytes(std::int64_t nodeId) const {
    Stmt stmt(db_,
              "WITH RECURSIVE tree(id,size,blob_id) AS ("
              "SELECT id,size,blob_id FROM nodes WHERE id=? "
              "UNION ALL "
              "SELECT n.id,n.size,n.blob_id FROM nodes n JOIN tree t ON n.parent_id=t.id"
              ") SELECT COALESCE(SUM(size),0) FROM tree WHERE blob_id IS NOT NULL");
    stmt.integer(1, nodeId);
    return stmt.row() ? stmt.intAt(0) : 0;
}

std::int64_t ExtendedFeatureService::adminUserId() const {
    Stmt stmt(db_, "SELECT id FROM users ORDER BY id LIMIT 1");
    return stmt.row() ? stmt.intAt(0) : 0;
}

void ExtendedFeatureService::validateNodeName(const std::string& name) const {
    if (name.empty() || name.size() > 255)
        throw ServiceError(ErrorCode::BadRequest, "name length must be 1..255");
    if (name == "." || name == "..")
        throw ServiceError(ErrorCode::BadRequest, "name is reserved");
    for (const char c : name) {
        if (c == '/' || c == '\\' || c == '\0')
            throw ServiceError(ErrorCode::BadRequest, "name contains an illegal character");
        if (static_cast<unsigned char>(c) < 0x20)
            throw ServiceError(ErrorCode::BadRequest, "name contains a control character");
    }
}

cloud::common::Packet ExtendedFeatureService::entriesPacket(
    const cloud::common::Packet& request, ExtendedMessageType response,
    const std::vector<std::string>& entries) const {
    return makeJsonPacket(cloud::common::ext::asMessageType(response), request.header.requestId,
                          json::object({{"count", std::to_string(entries.size())},
                                        {"entries", json::array(entries)}}),
                          cloud::common::FlagResponse);
}

cloud::common::Packet ExtendedFeatureService::objectPacket(
    const cloud::common::Packet& request, ExtendedMessageType response,
    const std::string& json) const {
    return makeJsonPacket(cloud::common::ext::asMessageType(response), request.header.requestId,
                          json, cloud::common::FlagResponse);
}

// ---------------------------------------------------------------- 回收站

std::vector<std::string> ExtendedFeatureService::listTrash(std::int64_t userId) const {
    // 回收站里节点名带有内部去重后缀，这里显示原始文件名。
    Stmt stmt(db_,
              "SELECT t.node_id,t.original_name,n.is_directory,n.size,t.deleted_at "
              "FROM ext_trash t JOIN nodes n ON n.id=t.node_id "
              "WHERE t.owner_id=? ORDER BY t.deleted_at DESC");
    stmt.integer(1, userId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        entries.push_back(makeRow("trash", stmt.intAt(0), stmt.textAt(1), {},
                                  "回收站内可恢复或彻底删除", stmt.intAt(3), stmt.intAt(4),
                                  stmt.intAt(2) != 0, {}, {}));
    }
    return entries;
}

void ExtendedFeatureService::moveToTrash(std::int64_t userId, std::int64_t nodeId) {
    const auto node = requireOwnedNode(userId, nodeId);
    if (node.parentId == kTrashParent)
        throw ServiceError(ErrorCode::BadRequest, "item is already in the recycle bin");
    if (node.parentId == kQuarantineParent)
        throw ServiceError(ErrorCode::BadRequest,
                           "release the item from the quarantine area first");

    Txn txn(db_);
    Stmt remember(db_,
                  "INSERT OR REPLACE INTO ext_trash"
                  "(node_id,owner_id,original_parent,original_name,size,deleted_at) "
                  "VALUES(?,?,?,?,?,?)");
    remember.integer(1, nodeId)
        .integer(2, userId)
        .integer(3, node.parentId)
        .text(4, node.name)
        .integer(5, node.size)
        .integer(6, nowMillis());
    remember.run();

    // 隐藏父目录下可能与同名的其它回收项冲突，因此临时给名字附加节点编号；
    // 原始名字已经保存在 ext_trash 中，恢复时会还原。
    Stmt move(db_, "UPDATE nodes SET parent_id=?, name=? WHERE id=? AND owner_id=?");
    move.integer(1, kTrashParent)
        .text(2, node.name + "#" + std::to_string(nodeId))
        .integer(3, nodeId)
        .integer(4, userId);
    move.run();
    txn.commit();
}

void ExtendedFeatureService::restoreFromTrash(std::int64_t userId, std::int64_t nodeId) {
    Stmt query(db_, "SELECT original_parent,original_name FROM ext_trash WHERE node_id=? AND owner_id=?");
    query.integer(1, nodeId).integer(2, userId);
    if (!query.row())
        throw ServiceError(ErrorCode::NotFound, "item is not in the recycle bin");
    const auto originalParent = query.intAt(0);
    const auto originalName = query.textAt(1);

    Txn txn(db_);
    std::int64_t parent = 0;
    if (originalParent > 0) {
        // 原目录可能已经被删除，此时退回根目录，避免恢复出不可见的节点。
        Stmt probe(db_, "SELECT 1 FROM nodes WHERE id=? AND owner_id=? AND is_directory=1");
        probe.integer(1, originalParent).integer(2, userId);
        if (probe.row())
            parent = originalParent;
    }
    const auto name = uniqueName(userId, parent, originalName);
    Stmt move(db_, "UPDATE nodes SET parent_id=?, name=? WHERE id=? AND owner_id=?");
    move.integer(1, parent).text(2, name).integer(3, nodeId).integer(4, userId);
    move.run();

    Stmt forget(db_, "DELETE FROM ext_trash WHERE node_id=? AND owner_id=?");
    forget.integer(1, nodeId).integer(2, userId);
    forget.run();
    txn.commit();
}

void ExtendedFeatureService::purgeTrashNode(std::int64_t userId, std::int64_t nodeId) {
    Stmt query(db_, "SELECT 1 FROM ext_trash WHERE node_id=? AND owner_id=?");
    query.integer(1, nodeId).integer(2, userId);
    if (!query.row())
        throw ServiceError(ErrorCode::NotFound, "item is not in the recycle bin");

    // 先摘掉回收站记录，再交给仓库完成 blob 引用计数和配额回收。
    Stmt forget(db_, "DELETE FROM ext_trash WHERE node_id=? AND owner_id=?");
    forget.integer(1, nodeId).integer(2, userId);
    forget.run();
    repository_.deleteNode(userId, nodeId);
}

std::int64_t ExtendedFeatureService::emptyTrash(std::int64_t userId) {
    std::vector<std::int64_t> nodeIds;
    {
        Stmt stmt(db_, "SELECT node_id FROM ext_trash WHERE owner_id=?");
        stmt.integer(1, userId);
        while (stmt.row())
            nodeIds.push_back(stmt.intAt(0));
    }
    for (const auto nodeId : nodeIds)
        purgeTrashNode(userId, nodeId);
    return static_cast<std::int64_t>(nodeIds.size());
}

// ---------------------------------------------------------------- 归档库

std::vector<std::string> ExtendedFeatureService::listArchive(std::int64_t userId) const {
    Stmt stmt(db_,
              "SELECT a.node_id,n.name,n.is_directory,n.size,a.archived_at "
              "FROM ext_archive a JOIN nodes n ON n.id=a.node_id "
              "WHERE a.owner_id=? ORDER BY a.archived_at DESC");
    stmt.integer(1, userId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        entries.push_back(makeRow("archive", stmt.intAt(0), stmt.textAt(1), {},
                                  "已归档，文件仍保存在原目录", stmt.intAt(3), stmt.intAt(4),
                                  stmt.intAt(2) != 0, {}, {}));
    }
    return entries;
}

void ExtendedFeatureService::addArchive(std::int64_t userId, std::int64_t nodeId) {
    requireOwnedNode(userId, nodeId);
    Stmt stmt(db_,
              "INSERT OR REPLACE INTO ext_archive(node_id,owner_id,archived_at) VALUES(?,?,?)");
    stmt.integer(1, nodeId).integer(2, userId).integer(3, nowMillis());
    stmt.run();
}

void ExtendedFeatureService::removeArchive(std::int64_t userId, std::int64_t nodeId) {
    Stmt stmt(db_, "DELETE FROM ext_archive WHERE node_id=? AND owner_id=?");
    stmt.integer(1, nodeId).integer(2, userId);
    stmt.run();
    if (sqlite3_changes(db_) == 0)
        throw ServiceError(ErrorCode::NotFound, "item is not archived");
}

// ---------------------------------------------------------------- 隔离区

std::vector<std::string> ExtendedFeatureService::listQuarantine(std::int64_t userId) const {
    // 与回收站同理：隔离区展示原始文件名，而不是内部去重名。
    Stmt stmt(db_,
              "SELECT q.node_id,q.original_name,n.is_directory,n.size,q.quarantined_at,"
              "q.reason "
              "FROM ext_quarantine q JOIN nodes n ON n.id=q.node_id "
              "WHERE q.owner_id=? ORDER BY q.quarantined_at DESC");
    stmt.integer(1, userId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        entries.push_back(makeRow("quarantine", stmt.intAt(0), stmt.textAt(1), {},
                                  stmt.textAt(5), stmt.intAt(3), stmt.intAt(4),
                                  stmt.intAt(2) != 0, {}, {}));
    }
    return entries;
}

std::vector<std::string> ExtendedFeatureService::scanQuarantine(std::int64_t userId) {
    std::vector<std::pair<std::int64_t, std::string>> targets;
    {
        Stmt stmt(db_,
                  "SELECT n.id,n.name FROM nodes n "
                  "LEFT JOIN ext_quarantine q ON q.node_id=n.id "
                  "WHERE n.owner_id=? AND n.is_directory=0 AND n.parent_id>=0 "
                  "AND q.node_id IS NULL");
        stmt.integer(1, userId);
        while (stmt.row()) {
            const auto nodeId = stmt.intAt(0);
            const auto name = stmt.textAt(1);
            if (riskyExtension(name))
                targets.emplace_back(nodeId, name);
        }
    }

    std::vector<std::string> findings;
    for (const auto& [nodeId, name] : targets) {
        const auto dot = name.find_last_of('.');
        const auto extension = dot == std::string::npos ? name : name.substr(dot);
        quarantineNode(userId, nodeId, "命中可执行文件特征：" + lowerCase(extension));
        findings.push_back(name);
    }
    return findings;
}

void ExtendedFeatureService::quarantineNode(std::int64_t userId, std::int64_t nodeId,
                                            const std::string& reason) {
    const auto node = requireOwnedNode(userId, nodeId);
    if (node.parentId == kQuarantineParent)
        throw ServiceError(ErrorCode::BadRequest, "item is already quarantined");
    if (node.parentId == kTrashParent)
        throw ServiceError(ErrorCode::BadRequest, "restore the item from the recycle bin first");

    Txn txn(db_);
    Stmt remember(db_,
                  "INSERT OR REPLACE INTO ext_quarantine"
                  "(node_id,owner_id,original_parent,original_name,reason,quarantined_at) "
                  "VALUES(?,?,?,?,?,?)");
    remember.integer(1, nodeId)
        .integer(2, userId)
        .integer(3, node.parentId)
        .text(4, node.name)
        .text(5, reason)
        .integer(6, nowMillis());
    remember.run();

    Stmt move(db_, "UPDATE nodes SET parent_id=?, name=? WHERE id=? AND owner_id=?");
    move.integer(1, kQuarantineParent)
        .text(2, node.name + "#" + std::to_string(nodeId))
        .integer(3, nodeId)
        .integer(4, userId);
    move.run();
    txn.commit();
}

void ExtendedFeatureService::releaseQuarantine(std::int64_t userId, std::int64_t nodeId) {
    Stmt query(db_,
               "SELECT original_parent,original_name FROM ext_quarantine "
               "WHERE node_id=? AND owner_id=?");
    query.integer(1, nodeId).integer(2, userId);
    if (!query.row())
        throw ServiceError(ErrorCode::NotFound, "item is not quarantined");
    const auto originalParent = query.intAt(0);
    const auto originalName = query.textAt(1);

    Txn txn(db_);
    std::int64_t parent = 0;
    if (originalParent > 0) {
        Stmt probe(db_, "SELECT 1 FROM nodes WHERE id=? AND owner_id=? AND is_directory=1");
        probe.integer(1, originalParent).integer(2, userId);
        if (probe.row())
            parent = originalParent;
    }
    const auto name = uniqueName(userId, parent, originalName);
    Stmt move(db_, "UPDATE nodes SET parent_id=?, name=? WHERE id=? AND owner_id=?");
    move.integer(1, parent).text(2, name).integer(3, nodeId).integer(4, userId);
    move.run();

    Stmt forget(db_, "DELETE FROM ext_quarantine WHERE node_id=? AND owner_id=?");
    forget.integer(1, nodeId).integer(2, userId);
    forget.run();
    txn.commit();
}

void ExtendedFeatureService::purgeQuarantine(std::int64_t userId, std::int64_t nodeId) {
    Stmt query(db_, "SELECT 1 FROM ext_quarantine WHERE node_id=? AND owner_id=?");
    query.integer(1, nodeId).integer(2, userId);
    if (!query.row())
        throw ServiceError(ErrorCode::NotFound, "item is not quarantined");

    Stmt forget(db_, "DELETE FROM ext_quarantine WHERE node_id=? AND owner_id=?");
    forget.integer(1, nodeId).integer(2, userId);
    forget.run();
    repository_.deleteNode(userId, nodeId);
}

// ---------------------------------------------------------------- 文档库 / 收发任务

std::vector<std::string> ExtendedFeatureService::listLibrary(std::int64_t userId) const {
    Stmt stmt(db_,
              "SELECT n.id,n.name,n.is_directory,n.size,n.modified_at,COALESCE(p.name,''),"
              "COALESCE(b.sha256,'') "
              "FROM nodes n "
              "LEFT JOIN nodes p ON p.id=n.parent_id "
              "LEFT JOIN blobs b ON b.id=n.blob_id "
              "WHERE n.owner_id=? AND n.parent_id>=0 "
              "ORDER BY n.modified_at DESC LIMIT 500");
    stmt.integer(1, userId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        const auto parentName = stmt.textAt(5);
        std::string detail = parentName.empty() ? std::string("位于 文档库根目录")
                                                : "位于 " + parentName;
        const auto sha = stmt.textAt(6);
        if (!sha.empty())
            detail += " · 秒传指纹 " + sha.substr(0, 12);
        entries.push_back(makeRow("node", stmt.intAt(0), stmt.textAt(1), {}, detail,
                                  stmt.intAt(3), stmt.intAt(4), stmt.intAt(2) != 0, {}, {}));
    }
    return entries;
}

std::vector<std::string> ExtendedFeatureService::listTransfers(std::int64_t userId) const {
    std::vector<std::pair<std::int64_t, std::string>> collected;
    const auto now = nowMillis();

    // 发送：我创建的提取码
    {
        Stmt stmt(db_,
                  "SELECT s.code,n.name,n.size,s.created_at,s.expires_at,s.claimed_at "
                  "FROM shares s JOIN nodes n ON n.id=s.node_id "
                  "WHERE s.owner_id=? ORDER BY s.created_at DESC LIMIT 100");
        stmt.integer(1, userId);
        while (stmt.row()) {
            const bool claimed = !stmt.isNull(5);
            const bool expired = stmt.intAt(4) > 0 && stmt.intAt(4) < now;
            const std::string status = claimed ? "已领取" : (expired ? "已过期" : "等待领取");
            collected.emplace_back(
                stmt.intAt(3),
                makeRow("transfer", 0, stmt.textAt(1), {},
                        "发送 · 提取码 " + upperCase(stmt.textAt(0)) + " · " + status,
                        stmt.intAt(2), stmt.intAt(3), false, {}, {}));
        }
    }

    // 发送：我创建的外链
    {
        Stmt stmt(db_,
                  "SELECT l.token,n.name,n.size,l.created_at,l.expires_at,l.revoked,"
                  "CASE WHEN l.discoverable=1 THEN 1 ELSE 0 END "
                  "FROM ext_share_links l JOIN nodes n ON n.id=l.node_id "
                  "WHERE l.owner_id=? ORDER BY l.created_at DESC LIMIT 100");
        stmt.integer(1, userId);
        while (stmt.row()) {
            const bool revoked = stmt.intAt(5) != 0;
            const bool expired = stmt.intAt(4) > 0 && stmt.intAt(4) < now;
            std::string status = revoked ? "已撤销" : (expired ? "已过期" : "有效");
            if (stmt.intAt(6) != 0)
                status += " · 已发布到发现共享";
            collected.emplace_back(
                stmt.intAt(3),
                makeRow("transfer", 0, stmt.textAt(1), {},
                        "发送 · 外链 " + upperCase(stmt.textAt(0)) + " · " + status,
                        stmt.intAt(2), stmt.intAt(3), false, {}, {}));
        }
    }

    // 接收：我用外链领取过的文件
    {
        Stmt stmt(db_,
                  "SELECT c.id,c.name,c.size,c.claimed_at,u.username "
                  "FROM ext_link_claims c JOIN users u ON u.id=c.owner_id "
                  "WHERE c.claimer_id=? ORDER BY c.claimed_at DESC LIMIT 100");
        stmt.integer(1, userId);
        while (stmt.row()) {
            collected.emplace_back(
                stmt.intAt(3),
                makeRow("transfer", stmt.intAt(0), stmt.textAt(1), stmt.textAt(4),
                        "接收 · 来自 " + stmt.textAt(4) + " 的外链", stmt.intAt(2),
                        stmt.intAt(3), false, {}, {}));
        }
    }

    std::stable_sort(collected.begin(), collected.end(),
                     [](const auto& left, const auto& right) {
                         return left.first > right.first;
                     });
    std::vector<std::string> entries;
    entries.reserve(collected.size());
    for (auto& [timestamp, row] : collected) {
        (void)timestamp;
        entries.push_back(std::move(row));
    }
    return entries;
}

// ---------------------------------------------------------------- 权限共享

std::vector<std::string> ExtendedFeatureService::listSharedWithMe(std::int64_t userId) const {
    Stmt stmt(db_,
              "SELECT a.node_id,n.name,n.is_directory,n.size,n.modified_at,u.username,a.can_write "
              "FROM ext_acl a "
              "JOIN nodes n ON n.id=a.node_id "
              "JOIN users u ON u.id=a.owner_id "
              "WHERE a.grantee_id=? ORDER BY a.created_at DESC");
    stmt.integer(1, userId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        entries.push_back(makeRow("share", stmt.intAt(0), stmt.textAt(1), stmt.textAt(5),
                                  "来自 " + stmt.textAt(5) + " · " +
                                      permissionText(stmt.intAt(6) != 0) + " · 可领取到我的文档",
                                  stmt.intAt(3), stmt.intAt(4), stmt.intAt(2) != 0, {}, {}));
    }
    return entries;
}

std::vector<std::string> ExtendedFeatureService::listPermissions(std::int64_t userId) const {
    std::vector<std::string> entries;
    {
        Stmt stmt(db_,
                  "SELECT a.node_id,n.name,n.is_directory,n.size,n.modified_at,u.username,"
                  "a.can_write FROM ext_acl a "
                  "JOIN nodes n ON n.id=a.node_id JOIN users u ON u.id=a.grantee_id "
                  "WHERE a.owner_id=? ORDER BY a.created_at DESC");
        stmt.integer(1, userId);
        while (stmt.row()) {
            entries.push_back(makeRow("permission", stmt.intAt(0), stmt.textAt(1), stmt.textAt(5),
                                      "我授予 " + stmt.textAt(5) + " · " +
                                          permissionText(stmt.intAt(6) != 0),
                                      stmt.intAt(3), stmt.intAt(4), stmt.intAt(2) != 0, {}, {}));
        }
    }
    {
        Stmt stmt(db_,
                  "SELECT a.node_id,n.name,n.is_directory,n.size,n.modified_at,u.username,"
                  "a.can_write FROM ext_acl a "
                  "JOIN nodes n ON n.id=a.node_id JOIN users u ON u.id=a.owner_id "
                  "WHERE a.grantee_id=? ORDER BY a.created_at DESC");
        stmt.integer(1, userId);
        while (stmt.row()) {
            entries.push_back(makeRow("permission", stmt.intAt(0), stmt.textAt(1), stmt.textAt(5),
                                      "别人授予我 · " + stmt.textAt(5) + " · " +
                                          permissionText(stmt.intAt(6) != 0),
                                      stmt.intAt(3), stmt.intAt(4), stmt.intAt(2) != 0, {}, {}));
        }
    }
    return entries;
}

std::vector<std::string> ExtendedFeatureService::listNodePermissions(std::int64_t userId,
                                                                    std::int64_t nodeId) const {
    const auto node = requireOwnedNode(userId, nodeId);
    Stmt stmt(db_,
              "SELECT a.id,u.username,a.can_write,a.created_at FROM ext_acl a "
              "JOIN users u ON u.id=a.grantee_id WHERE a.node_id=? ORDER BY a.created_at DESC");
    stmt.integer(1, nodeId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        entries.push_back(makeRow("permission", stmt.intAt(0), node.name, stmt.textAt(1),
                                  "共享给 " + stmt.textAt(1) + " · " +
                                      permissionText(stmt.intAt(2) != 0),
                                  node.size, stmt.intAt(3), node.directory, {}, {}));
    }
    return entries;
}

void ExtendedFeatureService::grantPermission(std::int64_t userId, std::int64_t nodeId,
                                             const std::string& grantee, bool canWrite) {
    requireOwnedNode(userId, nodeId);
    Stmt user(db_, "SELECT id FROM users WHERE username=?");
    user.text(1, grantee);
    if (!user.row())
        throw ServiceError(ErrorCode::NotFound, "user not found: " + grantee);
    const auto granteeId = user.intAt(0);
    if (granteeId == userId)
        throw ServiceError(ErrorCode::BadRequest, "cannot share a node with yourself");

    Stmt stmt(db_,
              "INSERT OR REPLACE INTO ext_acl(node_id,owner_id,grantee_id,can_write,created_at) "
              "VALUES(?,?,?,?,?)");
    stmt.integer(1, nodeId)
        .integer(2, userId)
        .integer(3, granteeId)
        .integer(4, canWrite ? 1 : 0)
        .integer(5, nowMillis());
    stmt.run();
}

void ExtendedFeatureService::revokePermission(std::int64_t userId, std::int64_t nodeId,
                                              const std::string& grantee) {
    requireOwnedNode(userId, nodeId);
    Stmt stmt(db_,
              "DELETE FROM ext_acl WHERE node_id=? AND owner_id=? AND grantee_id="
              "(SELECT id FROM users WHERE username=?)");
    stmt.integer(1, nodeId).integer(2, userId).text(3, grantee);
    stmt.run();
    if (sqlite3_changes(db_) == 0)
        throw ServiceError(ErrorCode::NotFound, "no permission record for " + grantee);
}

/**
 * 领取别人授权给我的节点。
 *
 * 只有 ext_acl 中确实存在“授予我”的记录才允许领取，且复制过程复用
 * copyNodeForUser()，物理文件通过 blob 引用计数共享，不需要二次上传。
 */
std::int64_t ExtendedFeatureService::claimSharedNode(std::int64_t userId, std::int64_t nodeId) {
    std::string name;
    {
        Stmt check(db_,
                   "SELECT n.name FROM ext_acl a JOIN nodes n ON n.id=a.node_id "
                   "WHERE a.node_id=? AND a.grantee_id=?");
        check.integer(1, nodeId).integer(2, userId);
        if (!check.row())
            throw ServiceError(ErrorCode::NotFound, "no permission grant for this node");
        name = check.textAt(0);
    }
    return copyNodeForUser(userId, nodeId, 0, name);
}

// ---------------------------------------------------------------- 外链共享

std::vector<std::string> ExtendedFeatureService::listLinks(std::int64_t userId) const {
    const auto now = nowMillis();
    Stmt stmt(db_,
              "SELECT l.rowid,n.name,n.size,l.created_at,l.expires_at,l.revoked,l.token,"
              "l.open_count,COALESCE(l.password_hash,'') "
              "FROM ext_share_links l JOIN nodes n ON n.id=l.node_id "
              "WHERE l.owner_id=? ORDER BY l.created_at DESC");
    stmt.integer(1, userId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        const bool revoked = stmt.intAt(5) != 0;
        const bool expired = stmt.intAt(4) > 0 && stmt.intAt(4) < now;
        std::string status = revoked ? "已撤销" : (expired ? "已过期" : "有效");
        if (!stmt.textAt(8).empty())
            status += " · 需要访问密码";
        const std::string detail =
            status + " · 外链码 " + upperCase(stmt.textAt(6)) + " · 访问 " +
            std::to_string(stmt.intAt(7)) + " 次";
        entries.push_back(makeRow("link", stmt.intAt(0), stmt.textAt(1), {}, detail,
                                  stmt.intAt(2), stmt.intAt(4), false, stmt.textAt(6), {}));
    }
    return entries;
}

std::string ExtendedFeatureService::createLink(std::int64_t userId, std::int64_t nodeId,
                                               std::int64_t hours,
                                               const std::string& password,
                                               bool discoverable) {
    const auto node = requireOwnedNode(userId, nodeId);
    if (node.directory)
        throw ServiceError(ErrorCode::BadRequest,
                           "external links support single files only");
    if (hours <= 0)
        hours = 24;
    if (hours > 24 * 30)
        hours = 24 * 30;

    std::string token;
    for (int attempt = 0; attempt < 10 && token.empty(); ++attempt) {
        const auto candidate = cloud::common::randomHex(6);
        Stmt probe(db_, "SELECT 1 FROM ext_share_links WHERE token=?");
        probe.text(1, candidate);
        if (!probe.row())
            token = candidate;
    }
    if (token.empty())
        throw ServiceError(ErrorCode::InternalError, "cannot allocate a share token");

    const auto salt = password.empty() ? std::string{} : cloud::common::randomHex(8);
    const auto hash = password.empty() ? std::string{}
                                       : cloud::common::sha256Hex(salt + password);
    const auto created = nowMillis();

    Stmt stmt(db_,
              "INSERT INTO ext_share_links"
              "(token,owner_id,node_id,password_hash,salt,discoverable,created_at,expires_at) "
              "VALUES(?,?,?,?,?,?,?,?)");
    stmt.text(1, token)
        .integer(2, userId)
        .integer(3, nodeId)
        .text(4, hash)
        .text(5, salt)
        .integer(6, discoverable ? 1 : 0)
        .integer(7, created)
        .integer(8, created + hours * 60 * 60 * 1000);
    stmt.run();
    return token;
}

void ExtendedFeatureService::revokeLink(std::int64_t userId, const std::string& token) {
    Stmt stmt(db_,
              "UPDATE ext_share_links SET revoked=1 WHERE token=? AND owner_id=?");
    stmt.text(1, token).integer(2, userId);
    stmt.run();
    if (sqlite3_changes(db_) == 0)
        throw ServiceError(ErrorCode::NotFound, "share link not found");
}

std::int64_t ExtendedFeatureService::openLink(std::int64_t userId, const std::string& token,
                                              const std::string& password) {
    Stmt stmt(db_,
              "SELECT l.owner_id,l.node_id,l.password_hash,l.salt,l.expires_at,l.revoked,"
              "n.name FROM ext_share_links l JOIN nodes n ON n.id=l.node_id WHERE l.token=?");
    stmt.text(1, token);
    if (!stmt.row())
        throw ServiceError(ErrorCode::NotFound, "share link not found");

    const auto ownerId = stmt.intAt(0);
    const auto nodeId = stmt.intAt(1);
    const auto hash = stmt.textAt(2);
    const auto salt = stmt.textAt(3);
    const auto expiresAt = stmt.intAt(4);
    const bool revoked = stmt.intAt(5) != 0;
    const auto name = stmt.textAt(6);

    if (revoked)
        throw ServiceError(ErrorCode::NotFound, "this share link has been revoked");
    if (expiresAt > 0 && expiresAt < nowMillis())
        throw ServiceError(ErrorCode::NotFound, "this share link has expired");
    if (ownerId == userId)
        throw ServiceError(ErrorCode::BadRequest, "this share link belongs to you");
    {
        Stmt blocked(db_, "SELECT 1 FROM ext_blocks WHERE user_id=? AND owner_id=?");
        blocked.integer(1, userId).integer(2, ownerId);
        if (blocked.row())
            throw ServiceError(ErrorCode::Forbidden,
                               "you blocked this sharer; remove the block first");
    }
    if (!hash.empty()) {
        if (password.empty())
            throw ServiceError(ErrorCode::Unauthorized, "access password required");
        if (cloud::common::sha256Hex(salt + password) != hash)
            throw ServiceError(ErrorCode::Unauthorized, "wrong access password");
    }

    const auto copied = copyNodeForUser(userId, nodeId, 0, name);
    {
        Stmt bump(db_, "UPDATE ext_share_links SET open_count=open_count+1 WHERE token=?");
        bump.text(1, token);
        bump.run();
    }
    {
        Stmt record(db_,
                    "INSERT INTO ext_link_claims"
                    "(token,owner_id,claimer_id,name,size,claimed_at) VALUES(?,?,?,?,?,?)");
        record.text(1, token)
            .integer(2, ownerId)
            .integer(3, userId)
            .text(4, name)
            .integer(5, subtreeBytes(copied))
            .integer(6, nowMillis());
        record.run();
    }
    return copied;
}

std::vector<std::string> ExtendedFeatureService::discoverLinks(std::int64_t userId,
                                                               const std::string& keyword) const {
    Stmt stmt(db_,
              "SELECT l.token,n.name,n.size,l.expires_at,u.username,l.owner_id,l.open_count,"
              "n.id "
              "FROM ext_share_links l "
              "JOIN nodes n ON n.id=l.node_id "
              "JOIN users u ON u.id=l.owner_id "
              "WHERE l.discoverable=1 AND l.revoked=0 AND l.expires_at>? AND l.owner_id<>? "
              "AND NOT EXISTS(SELECT 1 FROM ext_blocks b "
              "WHERE b.user_id=? AND b.owner_id=l.owner_id) "
              "ORDER BY l.created_at DESC LIMIT 200");
    stmt.integer(1, nowMillis()).integer(2, userId).integer(3, userId);

    const auto needle = lowerCase(keyword);
    std::vector<std::string> entries;
    std::int64_t index = 0;
    while (stmt.row()) {
        const auto token = stmt.textAt(0);
        const auto name = stmt.textAt(1);
        const auto owner = stmt.textAt(4);
        if (!needle.empty() && lowerCase(name).find(needle) == std::string::npos &&
            lowerCase(owner).find(needle) == std::string::npos)
            continue;
        ++index;
        entries.push_back(makeRow("discover", index, name, owner,
                                  "来自 " + owner + " · 外链码 " + upperCase(token) +
                                      " · 已被领取 " + std::to_string(stmt.intAt(6)) + " 次",
                                  stmt.intAt(2), stmt.intAt(3), false, token,
                                  std::to_string(stmt.intAt(7))));
    }
    return entries;
}

// ---------------------------------------------------------------- 屏蔽名单

std::vector<std::string> ExtendedFeatureService::listBlocks(std::int64_t userId) const {
    Stmt stmt(db_,
              "SELECT b.id,b.owner_name,b.reason,b.created_at,u.username FROM ext_blocks b "
              "LEFT JOIN users u ON u.id=b.owner_id WHERE b.user_id=? "
              "ORDER BY b.created_at DESC");
    stmt.integer(1, userId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        const auto owner = stmt.textAt(4).empty() ? stmt.textAt(1) : stmt.textAt(4);
        entries.push_back(makeRow("block", stmt.intAt(0), owner, owner,
                                  "已屏蔽该用户分享 · " + stmt.textAt(2), 0, stmt.intAt(3),
                                  false, {}, {}));
    }
    return entries;
}

void ExtendedFeatureService::addBlock(std::int64_t userId, std::int64_t ownerId,
                                      const std::string& reason) {
    if (ownerId == userId)
        throw ServiceError(ErrorCode::BadRequest, "cannot block yourself");
    std::string ownerName;
    {
        Stmt user(db_, "SELECT username FROM users WHERE id=?");
        user.integer(1, ownerId);
        if (user.row())
            ownerName = user.textAt(0);
    }
    Stmt stmt(db_,
              "INSERT OR REPLACE INTO ext_blocks(user_id,owner_id,owner_name,reason,created_at) "
              "VALUES(?,?,?,?,?)");
    stmt.integer(1, userId)
        .integer(2, ownerId)
        .text(3, ownerName)
        .text(4, reason.empty() ? "用户主动屏蔽" : reason)
        .integer(5, nowMillis());
    stmt.run();
}

void ExtendedFeatureService::removeBlock(std::int64_t userId, std::int64_t blockId) {
    Stmt stmt(db_, "DELETE FROM ext_blocks WHERE id=? AND user_id=?");
    stmt.integer(1, blockId).integer(2, userId);
    stmt.run();
    if (sqlite3_changes(db_) == 0)
        throw ServiceError(ErrorCode::NotFound, "block record not found");
}

// ---------------------------------------------------------------- 申请与审核

std::vector<std::string> ExtendedFeatureService::listMyRequests(std::int64_t userId) const {
    Stmt stmt(db_,
              "SELECT r.id,n.name,n.is_directory,n.size,r.created_at,r.can_write,r.status,"
              "u.username,r.reason FROM ext_acl_requests r "
              "JOIN nodes n ON n.id=r.node_id JOIN users u ON u.id=r.owner_id "
              "WHERE r.requester_id=? ORDER BY r.created_at DESC");
    stmt.integer(1, userId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        std::string detail = permissionText(stmt.intAt(5) != 0) + " 权限 · " +
                             statusOfAclRequest(stmt.textAt(6)) + " · 文件所有者 " +
                             stmt.textAt(7);
        if (!stmt.textAt(8).empty())
            detail += " · 申请理由：" + stmt.textAt(8);
        entries.push_back(makeRow("request", stmt.intAt(0), stmt.textAt(1), stmt.textAt(7),
                                  detail, stmt.intAt(3), stmt.intAt(4), stmt.intAt(2) != 0,
                                  {}, {}));
    }
    return entries;
}

void ExtendedFeatureService::createRequest(std::int64_t userId, std::int64_t nodeId,
                                           bool canWrite, const std::string& reason) {
    const auto node = loadNode(nodeId);
    if (node.ownerId == userId)
        throw ServiceError(ErrorCode::BadRequest, "this node already belongs to you");

    Stmt pending(db_,
                 "SELECT id FROM ext_acl_requests "
                 "WHERE requester_id=? AND node_id=? AND status='pending'");
    pending.integer(1, userId).integer(2, nodeId);
    if (pending.row())
        throw ServiceError(ErrorCode::BadRequest, "an identical request is already pending");

    Stmt stmt(db_,
              "INSERT INTO ext_acl_requests"
              "(requester_id,owner_id,node_id,can_write,reason,status,created_at) "
              "VALUES(?,?,?,?,?,'pending',?)");
    stmt.integer(1, userId)
        .integer(2, node.ownerId)
        .integer(3, nodeId)
        .integer(4, canWrite ? 1 : 0)
        .text(5, reason)
        .integer(6, nowMillis());
    stmt.run();
}

std::vector<std::string> ExtendedFeatureService::listReviews(std::int64_t userId) const {
    std::vector<std::string> entries;
    {
        Stmt stmt(db_,
                  "SELECT r.id,n.name,n.size,r.can_write,r.created_at,u.username,r.reason "
                  "FROM ext_acl_requests r "
                  "JOIN nodes n ON n.id=r.node_id JOIN users u ON u.id=r.requester_id "
                  "WHERE r.owner_id=? AND r.status='pending' ORDER BY r.created_at DESC");
        stmt.integer(1, userId);
        while (stmt.row()) {
            std::string detail = "权限申请 · " + permissionText(stmt.intAt(3) != 0) +
                                 " · 申请人 " + stmt.textAt(5);
            if (!stmt.textAt(6).empty())
                detail += " · 理由：" + stmt.textAt(6);
            entries.push_back(makeRow("review-acl", stmt.intAt(0), stmt.textAt(1),
                                      stmt.textAt(5), detail, stmt.intAt(2), stmt.intAt(4),
                                      false, {}, {}));
        }
    }
    {
        Stmt stmt(db_,
                  "SELECT w.id,w.title,w.kind,w.created_at,u.username,w.detail "
                  "FROM ext_workflows w JOIN users u ON u.id=w.requester_id "
                  "WHERE w.reviewer_id=? AND w.status='submitted' ORDER BY w.created_at DESC");
        stmt.integer(1, userId);
        while (stmt.row()) {
            std::string detail = "流程申请 · " + stmt.textAt(2) + " · 申请人 " + stmt.textAt(4);
            if (!stmt.textAt(5).empty())
                detail += " · 说明：" + stmt.textAt(5);
            entries.push_back(makeRow("review-workflow", stmt.intAt(0), stmt.textAt(1),
                                      stmt.textAt(4), detail, 0, stmt.intAt(3), false, {}, {}));
        }
    }
    return entries;
}

void ExtendedFeatureService::decideReview(std::int64_t userId, const std::string& kind,
                                          std::int64_t reviewId, bool approve,
                                          const std::string& comment) {
    (void)comment;
    if (kind == "acl") {
        Stmt query(db_,
                   "SELECT requester_id,owner_id,node_id,can_write,status FROM ext_acl_requests "
                   "WHERE id=?");
        query.integer(1, reviewId);
        if (!query.row())
            throw ServiceError(ErrorCode::NotFound, "request not found");
        const auto requesterId = query.intAt(0);
        const auto ownerId = query.intAt(1);
        const auto nodeId = query.intAt(2);
        const bool canWrite = query.intAt(3) != 0;
        if (ownerId != userId)
            throw ServiceError(ErrorCode::Forbidden, "only the owner can review this request");
        if (query.textAt(4) != "pending")
            throw ServiceError(ErrorCode::BadRequest, "request has already been decided");

        Stmt update(db_,
                    "UPDATE ext_acl_requests SET status=?,decided_at=? WHERE id=?");
        update.text(1, approve ? "approved" : "rejected")
            .integer(2, nowMillis())
            .integer(3, reviewId);
        update.run();

        if (approve) {
            Stmt grant(db_,
                       "INSERT OR REPLACE INTO ext_acl"
                       "(node_id,owner_id,grantee_id,can_write,created_at) VALUES(?,?,?,?,?)");
            grant.integer(1, nodeId)
                .integer(2, ownerId)
                .integer(3, requesterId)
                .integer(4, canWrite ? 1 : 0)
                .integer(5, nowMillis());
            grant.run();
        }
        return;
    }

    if (kind == "workflow") {
        Stmt query(db_, "SELECT reviewer_id,status FROM ext_workflows WHERE id=?");
        query.integer(1, reviewId);
        if (!query.row())
            throw ServiceError(ErrorCode::NotFound, "workflow not found");
        if (query.intAt(0) != userId)
            throw ServiceError(ErrorCode::Forbidden, "only the reviewer can decide");
        if (query.textAt(1) != "submitted")
            throw ServiceError(ErrorCode::BadRequest, "workflow has already been decided");

        Stmt update(db_, "UPDATE ext_workflows SET status=?,decided_at=? WHERE id=?");
        update.text(1, approve ? "approved" : "rejected")
            .integer(2, nowMillis())
            .integer(3, reviewId);
        update.run();
        return;
    }

    throw ServiceError(ErrorCode::BadRequest, "unknown review kind: " + kind);
}

std::vector<std::string> ExtendedFeatureService::listWorkflows(std::int64_t userId) const {
    Stmt stmt(db_,
              "SELECT w.id,w.title,w.kind,w.detail,w.created_at,w.status,u.username "
              "FROM ext_workflows w LEFT JOIN users u ON u.id=w.reviewer_id "
              "WHERE w.requester_id=? ORDER BY w.created_at DESC");
    stmt.integer(1, userId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        std::string detail = stmt.textAt(2) + " · " + statusOfWorkflow(stmt.textAt(5));
        if (!stmt.textAt(6).empty())
            detail += " · 审批人 " + stmt.textAt(6);
        if (!stmt.textAt(3).empty())
            detail += " · " + stmt.textAt(3);
        entries.push_back(makeRow("workflow", stmt.intAt(0), stmt.textAt(1), {}, detail, 0,
                                  stmt.intAt(4), false, {}, {}));
    }
    return entries;
}

void ExtendedFeatureService::createWorkflow(std::int64_t userId, const std::string& kind,
                                            const std::string& title,
                                            const std::string& detail) {
    if (title.empty())
        throw ServiceError(ErrorCode::BadRequest, "title is required");
    // 单机版没有角色表：约定第一个注册的账号是管理员，由它审批流程申请。
    Stmt stmt(db_,
              "INSERT INTO ext_workflows"
              "(requester_id,reviewer_id,kind,title,detail,status,created_at) "
              "VALUES(?,?,?,?,?,'submitted',?)");
    stmt.integer(1, userId)
        .integer(2, adminUserId())
        .text(3, kind.empty() ? "通用申请" : kind)
        .text(4, title)
        .text(5, detail)
        .integer(6, nowMillis());
    stmt.run();
}

// ---------------------------------------------------------------- 群组文档

std::vector<std::string> ExtendedFeatureService::listGroups(std::int64_t userId) const {
    Stmt stmt(db_,
              "SELECT g.id,g.name,u.username,g.created_at,"
              "(SELECT COUNT(*) FROM ext_group_members m2 WHERE m2.group_id=g.id),"
              "(SELECT COUNT(*) FROM ext_group_items i WHERE i.group_id=g.id) "
              "FROM ext_groups g "
              "JOIN ext_group_members m ON m.group_id=g.id AND m.user_id=? "
              "JOIN users u ON u.id=g.owner_id "
              "ORDER BY g.created_at DESC");
    stmt.integer(1, userId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        entries.push_back(makeRow("group", stmt.intAt(0), stmt.textAt(1), stmt.textAt(2),
                                  "创建者 " + stmt.textAt(2) + " · " +
                                      std::to_string(stmt.intAt(4)) + " 名成员 · " +
                                      std::to_string(stmt.intAt(5)) + " 个共享条目",
                                  0, stmt.intAt(3), true, {}, {}));
    }
    return entries;
}

std::int64_t ExtendedFeatureService::createGroup(std::int64_t userId,
                                                 const std::string& name) {
    validateNodeName(name);
    Txn txn(db_);
    Stmt insert(db_, "INSERT INTO ext_groups(name,owner_id,created_at) VALUES(?,?,?)");
    insert.text(1, name).integer(2, userId).integer(3, nowMillis());
    insert.run();
    const auto groupId = sqlite3_last_insert_rowid(db_);

    Stmt member(db_,
                "INSERT OR REPLACE INTO ext_group_members(group_id,user_id,role,joined_at) "
                "VALUES(?,?,'owner',?)");
    member.integer(1, groupId).integer(2, userId).integer(3, nowMillis());
    member.run();
    txn.commit();
    return groupId;
}

void ExtendedFeatureService::requireMembership(std::int64_t userId,
                                               std::int64_t groupId) const {
    Stmt stmt(db_, "SELECT 1 FROM ext_group_members WHERE group_id=? AND user_id=?");
    stmt.integer(1, groupId).integer(2, userId);
    if (!stmt.row())
        throw ServiceError(ErrorCode::Forbidden, "you are not a member of this group");
}

std::vector<std::string> ExtendedFeatureService::listGroupItems(std::int64_t userId,
                                                                std::int64_t groupId) const {
    requireMembership(userId, groupId);
    Stmt stmt(db_,
              "SELECT i.id,n.id,n.name,n.is_directory,n.size,n.modified_at,u.username "
              "FROM ext_group_items i "
              "JOIN nodes n ON n.id=i.node_id "
              "JOIN users u ON u.id=i.added_by "
              "WHERE i.group_id=? ORDER BY i.created_at DESC");
    stmt.integer(1, groupId);
    std::vector<std::string> entries;
    while (stmt.row()) {
        entries.push_back(makeRow("group-item", stmt.intAt(0), stmt.textAt(2), stmt.textAt(6),
                                  "由 " + stmt.textAt(6) + " 添加 · 可领取到我的文档",
                                  stmt.intAt(4), stmt.intAt(5), stmt.intAt(3) != 0, {},
                                  std::to_string(stmt.intAt(1))));
    }
    return entries;
}

void ExtendedFeatureService::addGroupItem(std::int64_t userId, std::int64_t groupId,
                                          std::int64_t nodeId) {
    requireMembership(userId, groupId);
    requireOwnedNode(userId, nodeId);
    Stmt stmt(db_,
              "INSERT OR REPLACE INTO ext_group_items(group_id,node_id,added_by,created_at) "
              "VALUES(?,?,?,?)");
    stmt.integer(1, groupId).integer(2, nodeId).integer(3, userId).integer(4, nowMillis());
    stmt.run();
}

void ExtendedFeatureService::removeGroupItem(std::int64_t userId, std::int64_t groupId,
                                             std::int64_t itemId) {
    requireMembership(userId, groupId);
    Stmt stmt(db_, "DELETE FROM ext_group_items WHERE id=? AND group_id=?");
    stmt.integer(1, itemId).integer(2, groupId);
    stmt.run();
    if (sqlite3_changes(db_) == 0)
        throw ServiceError(ErrorCode::NotFound, "group item not found");
}

void ExtendedFeatureService::addGroupMember(std::int64_t userId, std::int64_t groupId,
                                            const std::string& member) {
    Stmt owner(db_, "SELECT owner_id FROM ext_groups WHERE id=?");
    owner.integer(1, groupId);
    if (!owner.row())
        throw ServiceError(ErrorCode::NotFound, "group not found");
    if (owner.intAt(0) != userId)
        throw ServiceError(ErrorCode::Forbidden, "only the group owner can invite members");

    Stmt user(db_, "SELECT id FROM users WHERE username=?");
    user.text(1, member);
    if (!user.row())
        throw ServiceError(ErrorCode::NotFound, "user not found: " + member);

    Stmt stmt(db_,
              "INSERT OR REPLACE INTO ext_group_members(group_id,user_id,role,joined_at) "
              "VALUES(?,?,'member',?)");
    stmt.integer(1, groupId).integer(2, user.intAt(0)).integer(3, nowMillis());
    stmt.run();
}

std::int64_t ExtendedFeatureService::claimGroupItem(std::int64_t userId, std::int64_t groupId,
                                                    std::int64_t itemId) {
    requireMembership(userId, groupId);
    Stmt stmt(db_, "SELECT node_id FROM ext_group_items WHERE id=? AND group_id=?");
    stmt.integer(1, itemId).integer(2, groupId);
    if (!stmt.row())
        throw ServiceError(ErrorCode::NotFound, "group item not found");
    const auto nodeId = stmt.intAt(0);
    const auto name = loadNode(nodeId).name;
    return copyNodeForUser(userId, nodeId, 0, name);
}

// ---------------------------------------------------------------- 跨用户复制

std::int64_t ExtendedFeatureService::insertCopiedNode(std::int64_t ownerId,
                                                      std::int64_t parentId,
                                                      const std::string& name,
                                                      const NodeRecord& source) {
    Stmt insert(db_,
                "INSERT INTO nodes(owner_id,parent_id,name,is_directory,blob_id,size,modified_at) "
                "VALUES(?,?,?,?,?,?,?)");
    insert.integer(1, ownerId).integer(2, parentId).text(3, name).integer(
        4, source.directory ? 1 : 0);
    if (!source.directory && source.hasBlob)
        insert.integer(5, source.blobId);
    else
        insert.null(5);
    insert.integer(6, source.directory ? 0 : source.size).integer(7, nowMillis());
    insert.run();
    const auto newId = sqlite3_last_insert_rowid(db_);

    if (!source.directory && source.hasBlob) {
        // 领取共享文件时只增加 blob 引用计数：物理文件不复制，等价于一次秒传。
        Stmt bump(db_, "UPDATE blobs SET ref_count=ref_count+1 WHERE id=?");
        bump.integer(1, source.blobId);
        bump.run();

        Stmt quota(db_, "UPDATE users SET storage_used=storage_used+? WHERE id=?");
        quota.integer(1, source.size).integer(2, ownerId);
        quota.run();
    }
    return newId;
}

/**
 * 把 sourceNodeId 复制到 requesterId 的 targetParentId 目录下。
 *
 * 用途：共享文档领取、外链领取、群组条目领取。
 * 目录会递归复制，但所有文件都复用已有的 blob，只增加引用计数，
 * 因此不会产生额外的物理磁盘占用；配额仍按逻辑大小累加。
 */
std::int64_t ExtendedFeatureService::copyNodeForUser(std::int64_t requesterId,
                                                     std::int64_t sourceNodeId,
                                                     std::int64_t targetParentId,
                                                     const std::string& desiredName) {
    const auto source = loadNode(sourceNodeId);

    if (targetParentId != 0) {
        Stmt parent(db_, "SELECT 1 FROM nodes WHERE id=? AND owner_id=? AND is_directory=1");
        parent.integer(1, targetParentId).integer(2, requesterId);
        if (!parent.row())
            throw ServiceError(ErrorCode::NotFound, "target directory not found");
    }

    const auto total = subtreeBytes(sourceNodeId);
    Txn txn(db_);
    {
        Stmt account(db_, "SELECT quota,storage_used FROM users WHERE id=?");
        account.integer(1, requesterId);
        if (!account.row())
            throw ServiceError(ErrorCode::NotFound, "user not found");
        if (total > 0 && account.intAt(1) + total > account.intAt(0))
            throw ServiceError(ErrorCode::QuotaExceeded,
                               "not enough quota to receive this item");
    }

    const auto rootName =
        uniqueName(requesterId, targetParentId, desiredName.empty() ? source.name : desiredName);
    const auto rootId = insertCopiedNode(requesterId, targetParentId, rootName, source);

    std::vector<std::pair<std::int64_t, std::int64_t>> pending{{sourceNodeId, rootId}};
    while (!pending.empty()) {
        const auto [sourceDir, targetDir] = pending.back();
        pending.pop_back();

        std::vector<NodeRecord> children;
        {
            Stmt stmt(db_,
                      "SELECT id,owner_id,parent_id,name,is_directory,size,blob_id "
                      "FROM nodes WHERE parent_id=? ORDER BY is_directory DESC,name");
            stmt.integer(1, sourceDir);
            while (stmt.row()) {
                NodeRecord child;
                child.id = stmt.intAt(0);
                child.ownerId = stmt.intAt(1);
                child.parentId = stmt.intAt(2);
                child.name = stmt.textAt(3);
                child.directory = stmt.intAt(4) != 0;
                child.size = stmt.intAt(5);
                child.hasBlob = !stmt.isNull(6);
                child.blobId = child.hasBlob ? stmt.intAt(6) : 0;
                children.push_back(std::move(child));
            }
        }
        for (const auto& child : children) {
            const auto copyId =
                insertCopiedNode(requesterId, targetDir, child.name, child);
            if (child.directory)
                pending.emplace_back(child.id, copyId);
        }
    }

    txn.commit();
    return rootId;
}

// ---------------------------------------------------------------- 配额

std::string ExtendedFeatureService::quotaInfo(std::int64_t userId) const {
    Stmt stmt(db_, "SELECT quota,storage_used FROM users WHERE id=?");
    stmt.integer(1, userId);
    if (!stmt.row())
        throw ServiceError(ErrorCode::NotFound, "user not found");
    const auto quota = stmt.intAt(0);
    const auto used = stmt.intAt(1);
    const auto free = quota > used ? quota - used : 0;
    return json::object({{"quota", std::to_string(quota)},
                         {"used", std::to_string(used)},
                         {"free", std::to_string(free)}});
}

} // namespace cloud::server
