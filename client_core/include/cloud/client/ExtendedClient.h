// 负责人：成员2：服务端存储（扩展功能模块）
#pragma once

#include "cloud/client/ClientCore.h"
#include "cloud/common/ExtendedMessageType.h"
#include "cloud/common/JsonLite.h"
#include "cloud/common/Packet.h"
#include "cloud/common/Socket.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cloud::client {

/**
 * 扩展功能列表中的一行。
 *
 * 字段与服务端 ExtendedFeatureService::makeRow() 一一对应；不同视图只使用其中
 * 一部分字段，例如回收站用 kind/name/size/modified，外链共享还会用到 token。
 */
struct ExtendedRow {
    std::string kind;        ///< 行类型：trash/archive/quarantine/link/permission/...
    std::int64_t id{};       ///< 行主键（节点 ID、外链 ID、申请 ID 等，含义由 kind 决定）
    std::string name;        ///< 显示名称
    std::string owner;       ///< 相关用户，如共享发起者、申请人
    std::string detail;      ///< 状态说明文字，直接显示给用户
    std::int64_t size{};     ///< 字节数，非文件行为 0
    std::int64_t modified{}; ///< 时间戳（毫秒），含义由 kind 决定
    bool directory{};        ///< true 表示可以双击进入（当前用于群组）
    std::string token;       ///< 外链分享码，仅 link/discover 行有值
    std::string extra;       ///< 额外标识，如群组条目对应的节点 ID
};

/// 扩展请求的返回结果：列表响应填充 rows，对象响应填充 fields。
struct ExtendedResult {
    std::vector<ExtendedRow> rows;
    cloud::common::json::Object fields;
};

/**
 * 扩展功能的同步客户端。
 *
 * 与 ClientCore 的关系：
 * - 复用同一套 Packet/Socket/JsonLite 基础库，但使用**独立的 TCP 连接**和
 *   独立的登录 token，因此扩展请求不会打断正在进行的大文件上传/下载；
 * - 连接按需建立并保持复用，网络异常后下次调用会自动重连；
 * - 所有调用都会阻塞当前线程，因此必须像 QtClient 一样放在工作线程里使用。
 *
 * 支持的 operation 名称（与 ExtendedMessageType 一一对应）：
 *
 * | operation                | 参数                              | 说明 |
 * |--------------------------|-----------------------------------|------|
 * | trash.list               | -                                 | 回收站列表 |
 * | trash.add                | nodeId                            | 移入回收站 |
 * | trash.restore            | nodeId                            | 还原 |
 * | trash.purge              | nodeId                            | 彻底删除 |
 * | trash.empty              | -                                 | 清空回收站 |
 * | archive.list             | -                                 | 归档列表 |
 * | archive.add              | nodeId                            | 加入归档 |
 * | archive.remove           | nodeId                            | 取消归档 |
 * | quarantine.list          | -                                 | 隔离区列表 |
 * | quarantine.scan          | -                                 | 扫描并隔离可疑文件 |
 * | quarantine.release       | nodeId                            | 解除隔离 |
 * | quarantine.purge         | nodeId                            | 删除隔离项 |
 * | library.list             | -                                 | 文档库（全部文件） |
 * | transfer.list            | -                                 | 收发任务 |
 * | share.list               | -                                 | 别人共享给我的文档 |
 * | share.claim              | nodeId                            | 领取到我的文档 |
 * | permission.list          | -                                 | 权限共享记录 |
 * | permission.node          | nodeId                            | 单个节点的授权列表 |
 * | permission.grant         | nodeId, grantee, canWrite         | 授权 |
 * | permission.revoke        | nodeId, grantee                   | 取消授权 |
 * | link.list                | -                                 | 外链列表 |
 * | link.create              | nodeId, hours, password, discoverable | 创建外链 |
 * | link.revoke              | shareToken                        | 撤销外链 |
 * | link.open                | shareToken, password               | 通过外链领取 |
 * | discover.list            | keyword                           | 发现共享 |
 * | block.list               | -                                 | 屏蔽名单 |
 * | block.add                | shareToken, reason                 | 屏蔽某个分享者 |
 *
 * 注意：`token` 是登录会话字段，客户端会自动填充；外链码统一使用 `shareToken`，
 * 两者不能混用。
 * | block.remove             | blockId                           | 解除屏蔽 |
 * | request.list             | -                                 | 我的权限申请 |
 * | request.create           | nodeId, canWrite, reason           | 发起权限申请 |
 * | review.list              | -                                 | 待我审核的申请 |
 * | review.decide            | kind, reviewId, approve            | 批准/驳回 |
 * | workflow.list            | -                                 | 我的流程申请 |
 * | workflow.create          | kind, title, detail                | 提交流程申请 |
 * | group.list               | -                                 | 我的群组 |
 * | group.create             | name                              | 新建群组 |
 * | group.items              | groupId                           | 群组共享条目 |
 * | group.item.add           | groupId, nodeId                    | 把文件加入群组 |
 * | group.item.remove        | groupId, itemId                    | 移出群组 |
 * | group.member.add         | groupId, member                    | 邀请成员 |
 * | group.item.claim         | groupId, itemId                    | 领取到我的文档 |
 * | quota                    | -                                 | 存储配额 |
 *
 * 参数值必须已经是合法 JSON 值：数字用 std::to_string，字符串用 json::quote。
 */
class ExtendedClientCore {
public:
    using Args = std::vector<std::pair<std::string, std::string>>;

    ExtendedClientCore(std::string host, std::uint16_t port);
    ~ExtendedClientCore();
    ExtendedClientCore(const ExtendedClientCore&) = delete;
    ExtendedClientCore& operator=(const ExtendedClientCore&) = delete;

    /// 使用独立连接登录，成功后保存专属 token。
    void login(const std::string& username, const std::string& password);
    /// 注销并断开连接；未登录时安全返回。
    void logout();
    [[nodiscard]] bool loggedIn() const noexcept { return !token_.empty(); }
    /// 主动断开，下一次调用会重新建立连接。
    void disconnect();

    /**
     * 执行一次扩展操作。
     * @throws ClientError 服务端返回带 FlagError 的业务错误。
     * @throws std::runtime_error 未登录、连接失败、协议或 JSON 解析失败。
     */
    ExtendedResult dispatch(const std::string& operation, const Args& args = {});

    /// operation 名称是否存在（供界面提前校验）。
    [[nodiscard]] static bool supportsOperation(const std::string& operation);

private:
    void ensureConnected();
    cloud::common::Packet request(cloud::common::MessageType type,
                                  const std::string& bodyJson);
    std::string buildBody(const Args& args) const;

    std::string host_;
    std::uint16_t port_{};
    cloud::common::SocketRuntime socketRuntime_;
    cloud::common::Socket socket_;
    std::uint64_t nextRequestId_{1};
    std::string token_;
};

} // namespace cloud::client
