// 负责人：成员2：服务端存储（扩展功能模块）
#include "cloud/client/ExtendedClient.h"

#include "cloud/common/ErrorCode.h"
#include "cloud/common/ProtocolConstants.h"

#include <stdexcept>

namespace cloud::client {
namespace {

using cloud::common::MessageType;
using cloud::common::ext::ExtendedMessageType;
namespace json = cloud::common::json;

/// 一个扩展操作的协议映射：请求编号、响应编号、是否返回列表。
struct OperationSpec {
    ExtendedMessageType request{};
    ExtendedMessageType response{};
    bool returnsEntries{};
};

using OperationTable = std::map<std::string, OperationSpec>;

const OperationTable& operationTable() {
    static const OperationTable table = {
        {"trash.list", {ExtendedMessageType::TrashListReq, ExtendedMessageType::TrashListResp, true}},
        {"trash.add", {ExtendedMessageType::TrashAddReq, ExtendedMessageType::TrashAddResp, false}},
        {"trash.restore", {ExtendedMessageType::TrashRestoreReq, ExtendedMessageType::TrashRestoreResp, false}},
        {"trash.purge", {ExtendedMessageType::TrashPurgeReq, ExtendedMessageType::TrashPurgeResp, false}},
        {"trash.empty", {ExtendedMessageType::TrashEmptyReq, ExtendedMessageType::TrashEmptyResp, false}},

        {"archive.list", {ExtendedMessageType::ArchiveListReq, ExtendedMessageType::ArchiveListResp, true}},
        {"archive.add", {ExtendedMessageType::ArchiveAddReq, ExtendedMessageType::ArchiveAddResp, false}},
        {"archive.remove", {ExtendedMessageType::ArchiveRemoveReq, ExtendedMessageType::ArchiveRemoveResp, false}},

        {"quarantine.list", {ExtendedMessageType::QuarantineListReq, ExtendedMessageType::QuarantineListResp, true}},
        {"quarantine.scan", {ExtendedMessageType::QuarantineScanReq, ExtendedMessageType::QuarantineScanResp, false}},
        {"quarantine.release", {ExtendedMessageType::QuarantineReleaseReq, ExtendedMessageType::QuarantineReleaseResp, false}},
        {"quarantine.purge", {ExtendedMessageType::QuarantinePurgeReq, ExtendedMessageType::QuarantinePurgeResp, false}},

        {"library.list", {ExtendedMessageType::LibraryListReq, ExtendedMessageType::LibraryListResp, true}},
        {"transfer.list", {ExtendedMessageType::TransferListReq, ExtendedMessageType::TransferListResp, true}},
        {"share.list", {ExtendedMessageType::ShareListReq, ExtendedMessageType::ShareListResp, true}},

        {"permission.list", {ExtendedMessageType::PermissionListReq, ExtendedMessageType::PermissionListResp, true}},
        {"permission.node", {ExtendedMessageType::NodePermissionReq, ExtendedMessageType::NodePermissionResp, true}},
        {"permission.grant", {ExtendedMessageType::PermissionGrantReq, ExtendedMessageType::PermissionGrantResp, false}},
        {"permission.revoke", {ExtendedMessageType::PermissionRevokeReq, ExtendedMessageType::PermissionRevokeResp, false}},
        {"share.claim", {ExtendedMessageType::ShareClaimReq, ExtendedMessageType::ShareClaimResp, false}},

        {"link.list", {ExtendedMessageType::LinkListReq, ExtendedMessageType::LinkListResp, true}},
        {"link.create", {ExtendedMessageType::LinkCreateReq, ExtendedMessageType::LinkCreateResp, false}},
        {"link.revoke", {ExtendedMessageType::LinkRevokeReq, ExtendedMessageType::LinkRevokeResp, false}},
        {"link.open", {ExtendedMessageType::LinkOpenReq, ExtendedMessageType::LinkOpenResp, false}},
        {"discover.list", {ExtendedMessageType::DiscoverListReq, ExtendedMessageType::DiscoverListResp, true}},

        {"block.list", {ExtendedMessageType::BlockListReq, ExtendedMessageType::BlockListResp, true}},
        {"block.add", {ExtendedMessageType::BlockAddReq, ExtendedMessageType::BlockAddResp, false}},
        {"block.remove", {ExtendedMessageType::BlockRemoveReq, ExtendedMessageType::BlockRemoveResp, false}},

        {"request.list", {ExtendedMessageType::RequestListReq, ExtendedMessageType::RequestListResp, true}},
        {"request.create", {ExtendedMessageType::RequestCreateReq, ExtendedMessageType::RequestCreateResp, false}},
        {"review.list", {ExtendedMessageType::ReviewListReq, ExtendedMessageType::ReviewListResp, true}},
        {"review.decide", {ExtendedMessageType::ReviewDecideReq, ExtendedMessageType::ReviewDecideResp, false}},
        {"workflow.list", {ExtendedMessageType::WorkflowListReq, ExtendedMessageType::WorkflowListResp, true}},
        {"workflow.create", {ExtendedMessageType::WorkflowCreateReq, ExtendedMessageType::WorkflowCreateResp, false}},

        {"group.list", {ExtendedMessageType::GroupListReq, ExtendedMessageType::GroupListResp, true}},
        {"group.create", {ExtendedMessageType::GroupCreateReq, ExtendedMessageType::GroupCreateResp, false}},
        {"group.items", {ExtendedMessageType::GroupItemListReq, ExtendedMessageType::GroupItemListResp, true}},
        {"group.item.add", {ExtendedMessageType::GroupItemAddReq, ExtendedMessageType::GroupItemAddResp, false}},
        {"group.item.remove", {ExtendedMessageType::GroupItemRemoveReq, ExtendedMessageType::GroupItemRemoveResp, false}},
        {"group.member.add", {ExtendedMessageType::GroupMemberAddReq, ExtendedMessageType::GroupMemberAddResp, false}},
        {"group.item.claim", {ExtendedMessageType::GroupItemClaimReq, ExtendedMessageType::GroupItemClaimResp, false}},

        {"quota", {ExtendedMessageType::QuotaReq, ExtendedMessageType::QuotaResp, false}},
    };
    return table;
}

std::int64_t asInt(const json::Object& object, const std::string& key) {
    const auto raw = json::optionalString(object, key, "0");
    try {
        return std::stoll(raw);
    } catch (...) {
        return 0;
    }
}

/// 把键值对表重新编码成 JSON 对象文本（值必须已经是合法 JSON 值）。
std::string encodeObject(const json::Object& object) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, raw] : object) {
        if (!first)
            out.push_back(',');
        first = false;
        out += json::quote(key) + ":" + raw;
    }
    out.push_back('}');
    return out;
}

ExtendedRow parseRow(const json::Object& object) {
    ExtendedRow row;
    row.kind = json::optionalString(object, "kind", {});
    row.id = asInt(object, "id");
    row.name = json::optionalString(object, "name", {});
    row.owner = json::optionalString(object, "owner", {});
    row.detail = json::optionalString(object, "detail", {});
    row.size = asInt(object, "size");
    row.modified = asInt(object, "modified");
    row.directory = json::optionalString(object, "directory", "false") == "true";
    row.token = json::optionalString(object, "token", {});
    row.extra = json::optionalString(object, "extra", {});
    return row;
}

} // namespace

ExtendedClientCore::ExtendedClientCore(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port) {}

ExtendedClientCore::~ExtendedClientCore() = default;

bool ExtendedClientCore::supportsOperation(const std::string& operation) {
    return operationTable().find(operation) != operationTable().end();
}

void ExtendedClientCore::disconnect() {
    socket_.close();
}

void ExtendedClientCore::ensureConnected() {
    if (socket_.valid())
        return;
    socket_ = cloud::common::connectTcp(host_, port_);
    // 与 ClientCore 一致：先握手确认对端理解本协议。
    const auto hello = request(MessageType::Hello, "{}");
    (void)hello;
}

std::string ExtendedClientCore::buildBody(const Args& args) const {
    json::Object object;
    if (!token_.empty())
        object["token"] = json::quote(token_);
    for (const auto& [key, value] : args) {
        // "token" 专用于登录会话；外链码等其它令牌必须使用各自的字段名，
        // 否则会覆盖鉴权字段，让服务端误判为未登录。
        if (key == "token")
            throw std::runtime_error(
                "operation argument 'token' is reserved for the session token");
        object[key] = value;
    }
    return encodeObject(object);
}

cloud::common::Packet ExtendedClientCore::request(MessageType type,
                                                  const std::string& bodyJson) {
    ensureConnected();
    const auto id = nextRequestId_++;
    sendPacket(socket_, makeJsonPacket(type, id, bodyJson));
    const auto response = receivePacket(socket_);

    if (response.header.requestId != id ||
        (response.header.flags & cloud::common::FlagResponse) == 0)
        throw std::runtime_error("server returned an unmatched response");

    if ((response.header.flags & cloud::common::FlagError) != 0) {
        const auto object = json::parseObject(bodyAsString(response));
        throw ClientError(json::optionalString(object, "errorCode", "INTERNAL_ERROR"),
                          json::optionalString(object, "message", "extended request failed"));
    }
    return response;
}

void ExtendedClientCore::login(const std::string& username, const std::string& password) {
    try {
        const auto body = json::object({{"username", json::quote(username)},
                                        {"password", json::quote(password)}});
        const auto response =
            request(MessageType::LoginReq, body);
        const auto object = json::parseObject(bodyAsString(response));
        const auto token = json::optionalString(object, "token", {});
        if (token.empty())
            throw std::runtime_error("server did not return a session token");
        token_ = token;
    } catch (const ClientError&) {
        throw;
    } catch (...) {
        // 连接或协议错误：断开以免后续请求复用坏连接。
        disconnect();
        throw;
    }
}

void ExtendedClientCore::logout() {
    if (!loggedIn()) {
        disconnect();
        return;
    }
    try {
        request(MessageType::LogoutReq,
                json::object({{"token", json::quote(token_)}}));
    } catch (...) {
        // 注销失败不影响本地状态清理。
    }
    token_.clear();
    disconnect();
}

ExtendedResult ExtendedClientCore::dispatch(const std::string& operation, const Args& args) {
    const auto& table = operationTable();
    const auto it = table.find(operation);
    if (it == table.end())
        throw std::runtime_error("unknown extended operation: " + operation);
    if (!loggedIn())
        throw std::runtime_error("extended client is not logged in");

    ExtendedResult result;
    try {
        const auto response =
            request(cloud::common::ext::asMessageType(it->second.request), buildBody(args));
        if (response.header.type != cloud::common::ext::asMessageType(it->second.response))
            throw std::runtime_error("unexpected response type for " + operation);
        if ((response.header.flags & cloud::common::FlagBinary) != 0)
            throw std::runtime_error("unexpected binary response for " + operation);
        const auto object = json::parseObject(bodyAsString(response));
        if (it->second.returnsEntries) {
            for (const auto& raw : json::parseObjectArray(json::optionalString(object, "entries", "[]")))
                result.rows.push_back(parseRow(raw));
        }
        result.fields = object;
    } catch (const ClientError&) {
        throw;
    } catch (...) {
        disconnect();
        throw;
    }
    return result;
}

} // namespace cloud::client
