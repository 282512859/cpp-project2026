// 负责人：成员2：服务端存储（扩展功能模块）
//
// 界面扩展功能的端到端测试：需要先启动 cloud_server。
//
//   cloud_server.exe 9100 <临时运行目录>
//   cloud_extended_tests.exe 9100
//
// 覆盖内容：回收站、归档、隔离区、文档库、外链共享、发现共享、屏蔽名单、
// 权限共享与申请审核、流程申请、群组文档、存储配额。

#include "cloud/client/ClientCore.h"
#include "cloud/client/ExtendedClient.h"
#include "cloud/common/JsonLite.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

namespace json = cloud::common::json;
using cloud::client::ClientCore;
using cloud::client::ExtendedClientCore;
using cloud::client::ExtendedResult;

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok)
        ++g_failures;
    std::cout << (ok ? "[ok]   " : "[FAIL] ") << what << '\n';
}

/**
 * 执行一次扩展调用。
 *
 * 单步失败不会中断整套测试：异常会被记录成一条失败项，返回空结果，
 * 这样一次运行就能看清所有问题出在哪一步。
 */
ExtendedResult call(ExtendedClientCore& client, const std::string& operation,
                    const ExtendedClientCore::Args& args,
                    const std::string& label) {
    try {
        return client.dispatch(operation, args);
    } catch (const std::exception& error) {
        ++g_checks;
        ++g_failures;
        std::cout << "[FAIL] " << label << "（" << operation << "）：" << error.what()
                  << '\n';
        return {};
    }
}

ExtendedResult call(ExtendedClientCore& client, const std::string& operation,
                    const std::string& label) {
    return call(client, operation, {}, label);
}

std::string uniqueName(const std::string& prefix) {
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    return prefix + std::to_string(stamp);
}

std::filesystem::path writeSampleFile(const std::filesystem::path& directory,
                                      const std::string& name,
                                      const std::string& content) {
    std::filesystem::create_directories(directory);
    const auto path = directory / name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    return path;
}

/// 在目录列表里找某个名字，返回节点 ID（找不到返回 0）。
std::int64_t findInList(ClientCore& core, std::int64_t parentId, const std::string& name) {
    for (const auto& node : core.list(parentId)) {
        if (node.name == name)
            return node.id;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::uint16_t port =
        argc > 1 ? static_cast<std::uint16_t>(std::stoi(argv[1])) : 9100;
    const std::string host = "127.0.0.1";

    const auto alice = uniqueName("alice_");
    const auto bob = uniqueName("bob_");
    const std::string password = "password123";
    const auto workspace = std::filesystem::temp_directory_path() / "lancloud_ext_tests";

    try {
        ClientCore aliceCore(host, port);
        aliceCore.registerUser(alice, password);
        aliceCore.login(alice, password);

        ClientCore bobCore(host, port);
        bobCore.registerUser(bob, password);
        bobCore.login(bob, password);

        ExtendedClientCore aliceExt(host, port);
        aliceExt.login(alice, password);
        ExtendedClientCore bobExt(host, port);
        bobExt.login(bob, password);

        check(aliceExt.loggedIn() && bobExt.loggedIn(), "扩展客户端登录成功");

        // ---------- 上传两个文件，作为后续所有功能的对象 ----------
        const auto reportPath =
            writeSampleFile(workspace, "report_" + alice + ".txt", "hello lanclouddrive");
        const auto reportName = reportPath.filename().string();
        const auto reportId = aliceCore.upload(reportPath, 0, reportName);
        check(reportId > 0, "上传普通文件");

        const auto dumpPath = writeSampleFile(workspace, "payload_" + alice + ".exe", "MZ fake exe");
        const auto dumpName = dumpPath.filename().string();
        const auto dumpId = aliceCore.upload(dumpPath, 0, dumpName);
        check(dumpId > 0, "上传可执行文件（用于隔离区）");

        // ---------- 配额 ----------
        const auto quota = aliceExt.dispatch("quota");
        check(std::stoll(quota.fields.at("used")) > 0, "配额接口返回已用空间");
        check(std::stoll(quota.fields.at("quota")) > std::stoll(quota.fields.at("used")),
              "配额总量大于已用空间");

        // ---------- 文档库 ----------
        const auto library = aliceExt.dispatch("library.list");
        check(!library.rows.empty(), "文档库列出全部文件");

        // ---------- 回收站 ----------
        aliceExt.dispatch("trash.add", {{"nodeId", std::to_string(reportId)}});
        check(findInList(aliceCore, 0, reportName) == 0, "移入回收站后目录中不再显示");
        const auto trashed = aliceExt.dispatch("trash.list");
        check(trashed.rows.size() == 1 && trashed.rows.front().name == reportName,
              "回收站列出被删除的文件");
        aliceExt.dispatch("trash.restore", {{"nodeId", std::to_string(reportId)}});
        check(findInList(aliceCore, 0, reportName) == reportId, "从回收站恢复到原目录");

        // ---------- 归档库 ----------
        aliceExt.dispatch("archive.add", {{"nodeId", std::to_string(reportId)}});
        const auto archived = aliceExt.dispatch("archive.list");
        check(archived.rows.size() == 1, "加入归档后可在归档库看到");
        aliceExt.dispatch("archive.remove", {{"nodeId", std::to_string(reportId)}});
        check(aliceExt.dispatch("archive.list").rows.empty(), "取消归档后归档库清空");

        // ---------- 隔离区 ----------
        const auto scan = aliceExt.dispatch("quarantine.scan");
        check(std::stoll(scan.fields.at("count")) >= 1, "隔离区扫描发现可执行文件");
        check(findInList(aliceCore, 0, dumpName) == 0, "命中隔离的文件从目录中移出");
        const auto quarantined = aliceExt.dispatch("quarantine.list");
        check(!quarantined.rows.empty(), "隔离区列出可疑文件");
        aliceExt.dispatch("quarantine.release", {{"nodeId", std::to_string(dumpId)}});
        check(findInList(aliceCore, 0, dumpName) == dumpId, "解除隔离后文件回到原目录");
        // 再次隔离后彻底删除，覆盖“删除隔离项”这条路径。
        aliceExt.dispatch("quarantine.scan");
        aliceExt.dispatch("quarantine.purge", {{"nodeId", std::to_string(dumpId)}});
        check(findInList(aliceCore, 0, dumpName) == 0, "彻底删除被隔离的文件");
        check(aliceExt.dispatch("quarantine.list").rows.empty(), "隔离区已清空");

        // ---------- 权限共享 ----------
        aliceExt.dispatch("permission.grant", {{"nodeId", std::to_string(reportId)},
                                               {"grantee", json::quote(bob)},
                                               {"canWrite", "false"}});
        const auto granted = aliceExt.dispatch("permission.list");
        check(!granted.rows.empty(), "授权后权限共享列表有记录");
        const auto sharedWithBob = bobExt.dispatch("share.list");
        check(sharedWithBob.rows.size() == 1 && sharedWithBob.rows.front().name == reportName,
              "被授权用户在共享文档中看到该文件");

        // ---------- 外链共享 / 发现共享 / 屏蔽 ----------
        const auto created = aliceExt.dispatch("link.create",
                                               {{"nodeId", std::to_string(reportId)},
                                                {"hours", "24"},
                                                {"password", json::quote("")},
                                                {"discoverable", "true"}});
        const auto token = created.fields.at("token");
        check(token.size() >= 8, "创建外链返回分享码");
        const auto links = aliceExt.dispatch("link.list");
        check(links.rows.size() == 1, "外链共享列表显示新建的外链");

        const auto discovered = call(bobExt, "discover.list", {{"keyword", json::quote("")}},
                                     "发现共享列表");
        check(!discovered.rows.empty(), "发现共享能看到公开外链");

        call(bobExt, "block.add",
             {{"shareToken", json::quote(token)}, {"reason", json::quote("测试屏蔽")}},
             "屏蔽分享者");
        check(call(bobExt, "discover.list", "屏蔽后重新拉取发现共享").rows.empty(),
              "屏蔽后不再看到该分享者的内容");
        const auto blocks = call(bobExt, "block.list", "屏蔽名单");
        check(blocks.rows.size() == 1, "屏蔽名单记录成功");
        if (!blocks.rows.empty()) {
            call(bobExt, "block.remove",
                 {{"blockId", std::to_string(blocks.rows.front().id)}}, "解除屏蔽");
        }

        const auto opened = call(bobExt, "link.open",
                                 {{"shareToken", json::quote(token)},
                                  {"password", json::quote("")}},
                                 "外链领取");
        const auto receivedId = std::stoll(opened.fields.at("nodeId"));
        check(findInList(bobCore, 0, reportName) == receivedId, "通过外链领取到自己的文档");

        // 权限共享同样可以领取到自己的文档（复用同一套秒传复制逻辑）。
        const auto claimedShare = call(bobExt, "share.claim",
                                       {{"nodeId", std::to_string(reportId)}},
                                       "领取权限共享的文件");
        check(!claimedShare.fields.empty() &&
                  std::stoll(claimedShare.fields.at("nodeId")) > 0,
              "领取权限共享文件成功");

        const auto transfers = call(aliceExt, "transfer.list", "收发任务");
        check(!transfers.rows.empty(), "发送方在收发任务中看到发出的外链");
        check(!call(bobExt, "transfer.list", "接收方收发任务").rows.empty(),
              "接收方在收发任务中看到通过外链领取的记录");

        // ---------- 群组文档 ----------
        const auto group =
            call(aliceExt, "group.create", {{"name", json::quote("课程组")}}, "创建群组");
        const auto groupId = std::stoll(group.fields.at("groupId"));
        check(groupId > 0, "创建群组");
        call(aliceExt, "group.member.add",
             {{"groupId", std::to_string(groupId)}, {"member", json::quote(bob)}}, "邀请成员");
        call(aliceExt, "group.item.add",
             {{"groupId", std::to_string(groupId)}, {"nodeId", std::to_string(reportId)}},
             "把文件加入群组");
        check(call(aliceExt, "group.list", "群组列表").rows.size() == 1,
              "群组列表显示新建群组");
        const auto items =
            call(bobExt, "group.items", {{"groupId", std::to_string(groupId)}}, "群组条目");
        check(items.rows.size() == 1, "成员可以看到群组共享条目");
        if (!items.rows.empty()) {
            call(bobExt, "group.item.claim",
                 {{"groupId", std::to_string(groupId)},
                  {"itemId", std::to_string(items.rows.front().id)}},
                 "领取群组条目");
        }

        // ---------- 权限申请与审核 ----------
        call(bobExt, "request.create",
             {{"nodeId", std::to_string(reportId)},
              {"canWrite", "true"},
              {"reason", json::quote("需要共同编辑")}},
             "提交权限申请");
        const auto myRequests = call(bobExt, "request.list", "我的权限申请");
        check(myRequests.rows.size() == 1, "申请人能看到自己的申请");
        const auto reviews = call(aliceExt, "review.list", "待审核列表");
        check(!reviews.rows.empty() && reviews.rows.front().kind == "review-acl",
              "文件所有者能在权限审核中看到申请");
        if (!reviews.rows.empty()) {
            call(aliceExt, "review.decide",
                 {{"kind", json::quote("acl")},
                  {"reviewId", std::to_string(reviews.rows.front().id)},
                  {"approve", "true"}},
                 "批准权限申请");
        }
        const auto approvedRequests = call(bobExt, "request.list", "批准后的申请状态");
        check(!approvedRequests.rows.empty() &&
                  approvedRequests.rows.front().detail.find("已批准") != std::string::npos,
              "批准后申请状态变为已批准");
        const auto sharedAfterApproval = call(bobExt, "share.list", "批准后的共享文档");
        check(!sharedAfterApproval.rows.empty() &&
                  sharedAfterApproval.rows.front().detail.find("可读写") != std::string::npos,
              "批准后授权升级为可读写");

        // ---------- 流程申请 ----------
        call(bobExt, "workflow.create",
             {{"kind", json::quote("扩容申请")},
              {"title", json::quote("申请扩容到 20GB")},
              {"detail", json::quote("课程资料较多")}},
             "提交流程申请");
        check(call(bobExt, "workflow.list", "我的流程申请").rows.size() == 1,
              "流程申请列表显示申请");

        // ---------- 权限配置（单节点授权列表） ----------
        const auto nodePermissions =
            call(aliceExt, "permission.node", {{"nodeId", std::to_string(reportId)}},
                 "单节点授权列表");
        check(!nodePermissions.rows.empty(), "权限配置对话框能读取单节点授权");
        call(aliceExt, "permission.revoke",
             {{"nodeId", std::to_string(reportId)}, {"grantee", json::quote(bob)}},
             "取消授权");

        // ---------- 清空回收站 ----------
        call(aliceExt, "trash.add", {{"nodeId", std::to_string(reportId)}}, "移入回收站");
        const auto emptied = call(aliceExt, "trash.empty", "清空回收站");
        check(std::stoll(emptied.fields.at("removed")) >= 1, "清空回收站");
        check(call(aliceExt, "trash.list", "清空后的回收站").rows.empty(), "清空后回收站为空");
    } catch (const std::exception& error) {
        std::cout << "[FAIL] 未捕获异常: " << error.what() << '\n';
        ++g_failures;
    }

    std::cout << "\n检查 " << g_checks << " 项，失败 " << g_failures << " 项\n";
    return g_failures == 0 ? 0 : 1;
}
