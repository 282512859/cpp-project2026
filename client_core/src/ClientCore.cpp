// 负责人：成员3：客户端网络
//
// ClientCore 的实现分为三层：
//   1. request()/jsonResponse() 负责通用的协议收发与错误转换；
//   2. register/login/list 等函数负责组装各业务接口的 JSON；
//   3. upload/download 在业务接口之上实现分块文件传输。
//
// 当前代码采用严格的“发送一个请求，再接收一个响应”的同步模型。重写某个函数时，
// 应特别注意 requestId、FlagResponse/FlagError、token 字段以及上传二进制前缀等接口约定。
#include "cloud/client/ClientCore.h"
#include "cloud/common/JsonLite.h"
#include "cloud/common/ProtocolConstants.h"
#include "cloud/common/Sha256.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace cloud::client {
namespace {

using namespace cloud::common;

/**
 * @brief 把一个 64 位无符号整数写成 8 字节大端序（网络字节序）。
 * @param bytes 输出缓冲区首地址；调用者必须保证至少有 8 个可写字节。
 * @param value 要编码的整数。
 *
 * 该函数专门用于上传块中 offset 字段的编码。服务端 ServerApp::handle() 使用与之
 * 对称的 read64() 解码，因此改写时必须保持字节序一致。它只写内存，不做边界检查。
 */
void put64(std::uint8_t* bytes, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        bytes[7 - i] = static_cast<std::uint8_t>(value >> (i * 8));
    }
}

} // namespace

/**
 * 构造流程：
 *   1. socketRuntime_ 先由成员声明顺序自动初始化；
 *   2. connectTcp(host, port) 建立 TCP 连接，并把所有权交给 socket_；
 *   3. 发送空 JSON 的 HELLO 请求，确认对端理解本协议；
 *   4. 检查响应消息类型，失败则抛异常，使对象不会以“半连接”状态交给调用者。
 *
 * @param host 服务端主机名或 IP。
 * @param port 服务端监听端口。
 * @throws std::exception DNS/TCP 连接、数据包收发或 HELLO 类型校验失败。
 */
ClientCore::ClientCore(const std::string& host, std::uint16_t port)
    : socket_(connectTcp(host, port)) {
    const auto response = request(MessageType::Hello, "{}");
    if (response.header.type != MessageType::Hello) {
        throw std::runtime_error("server HELLO failed");
    }
}

/**
 * @brief JSON 请求的便捷入口。
 * @param type 请求消息类型，例如 LoginReq 或 ListReq。
 * @param jsonText 已经序列化好的 JSON 对象文本。
 * @return 服务端响应包；通用协议检查已由 request(Packet) 完成。
 *
 * makeJsonPacket() 会把正文编码为字节数组；nextRequestId_ 从 1 起单调递增，随后通过
 * 后置 ++ 预留给本请求。若未来允许并发或处理整数回绕，需要在这里重新设计 ID 分配。
 */
Packet ClientCore::request(MessageType type, const std::string& jsonText) {
    return request(makeJsonPacket(type, nextRequestId_++, jsonText));
}

/**
 * @brief 所有同步协议收发的核心入口。
 * @param packet 已包含消息类型、requestId、flags 和正文的完整请求包。
 * @return 与请求 ID 匹配的成功响应，包括 JSON 响应或二进制下载块。
 * @throws ClientError 对端设置 FlagError 时，使用错误 JSON 中的 errorCode/message 构造。
 * @throws std::exception 网络收发失败，或响应 requestId/FlagResponse 不符合协议。
 *
 * sendPacket()/receivePacket() 负责数据包头的编码、长度检查和完整读写。本函数保存发送
 * 前的 ID，并要求紧接着收到的包属于当前请求。这也是 ClientCore 不能并发调用的根本原因：
 * 并发请求可能让线程收到彼此的响应。服务端错误必须先通过关联性检查，再转成 ClientError。
 */
Packet ClientCore::request(Packet packet) {
    const auto id = packet.header.requestId;
    sendPacket(socket_, packet);
    auto response = receivePacket(socket_);

    if (response.header.requestId != id ||
        (response.header.flags & FlagResponse) == 0) {
        throw std::runtime_error("server returned an unmatched response");
    }

    if ((response.header.flags & FlagError) != 0) {
        const auto object = json::parseObject(bodyAsString(response));
        // optionalString 提供兜底值，避免服务端错误包缺少可选说明时丢失原始失败语义。
        throw ClientError(
            json::optionalString(object, "errorCode", "INTERNAL_ERROR"),
            json::optionalString(object, "message", "server request failed"));
    }
    return response;
}

/**
 * @brief 组合“发送 JSON 请求 + 把响应正文解析为 JSON 对象”两个步骤。
 * @param type 请求消息类型。
 * @param jsonText 完整请求 JSON 文本。
 * @return 响应正文解析得到的 json::Object。
 * @throws ClientError 服务端返回业务错误。
 * @throws std::exception 收发失败，或成功响应正文不是合法 JSON 对象。
 *
 * 上传/下载的数据块不使用此函数，因为它们至少有一方的正文是二进制数据。
 */
json::Object ClientCore::jsonResponse(MessageType type,
                                      const std::string& jsonText) {
    return json::parseObject(bodyAsString(request(type, jsonText)));
}

/**
 * @brief 生成所有“登录后接口”共用的鉴权 JSON。
 * @param fields 需要附加的字段；字段名传原始文本，字段值必须提前编码为合法 JSON。
 * @return 以 token 为第一个字段的完整 JSON 对象文本。
 *
 * 例如 { {"name", json::quote(name)}, {"size", std::to_string(size)} } 会得到包含
 * token、name、size 的对象。此设计区分“JSON 字符串”和“JSON 数字”：若忘记对字符串
 * 调用 json::quote()，可能生成无效 JSON。函数只负责组装，不验证 token_ 是否为空。
 */
std::string ClientCore::authJson(
    std::initializer_list<std::pair<std::string, std::string>> fields) const {
    std::string out = "{\"token\":" + json::quote(token_);
    for (const auto& [key, value] : fields) {
        out += "," + json::quote(key) + ":" + value;
    }
    return out + "}";
}

/**
 * @brief 调用 REGISTER 接口创建账号。
 * @param username 用户名，将经过 json::quote() 转义。
 * @param password 密码，将经过 json::quote() 转义。
 *
 * 请求 JSON 接口：{"username": string, "password": string}。
 * 成功响应当前只包含 ok 字段，本客户端仅以“请求未抛异常”作为成功标志。注册接口不需要
 * token，且不会修改本对象的登录状态。
 */
void ClientCore::registerUser(const std::string& username,
                              const std::string& password) {
    jsonResponse(
        MessageType::RegisterReq,
        json::object({{"username", json::quote(username)},
                      {"password", json::quote(password)}}));
}

/**
 * @brief 调用 LOGIN 接口，并保存成功响应中的会话凭据。
 * @param username 用户名。
 * @param password 密码。
 *
 * 请求 JSON 接口：{"username": string, "password": string}；
 * 响应 JSON 接口：至少包含 {"token": string}。requireString() 同时完成存在性和类型
 * 检查，所以格式异常不会写入 token_。重新登录成功时会覆盖此前保存的 token。
 */
void ClientCore::login(const std::string& username,
                       const std::string& password) {
    const auto response = jsonResponse(
        MessageType::LoginReq,
        json::object({{"username", json::quote(username)},
                      {"password", json::quote(password)}}));
    token_ = json::requireString(response, "token");
}

/**
 * @brief 注销当前会话，并在服务端确认后清除本地 token。
 *
 * 请求 JSON 接口：{"token": string}。token_ 为空代表本地未登录，此时函数幂等地直接
 * 返回，不发送网络请求。clear() 放在 jsonResponse() 之后是刻意的：若服务端请求失败，
 * 本地仍保留 token，调用者可以重试或检查错误。
 */
void ClientCore::logout() {
    if (token_.empty()) {
        return;
    }
    jsonResponse(MessageType::LogoutReq, authJson({}));
    token_.clear();
}

/**
 * @brief 获取一个远程目录下的直接子节点。
 * @param parentId 远程父目录 ID，0 为根目录。
 * @return 由响应 entries 数组转换成的 RemoteNode 列表。
 *
 * 请求 JSON 接口：{"token": string, "parentId": integer}。
 * 响应中的 entries 在 JsonLite 协议里先作为数组文本取出，再由 parseObjectArray() 拆成
 * 对象；每个对象的六个字段都是必需字段，缺失或类型不匹配会立即抛出解析异常。
 */
std::vector<RemoteNode> ClientCore::list(std::int64_t parentId) {
    const auto response = jsonResponse(
        MessageType::ListReq,
        authJson({{"parentId", std::to_string(parentId)}}));

    std::vector<RemoteNode> nodes;
    for (const auto& item :
         json::parseObjectArray(json::requireString(response, "entries"))) {
        nodes.push_back({json::requireInt(item, "id"),
                         json::requireInt(item, "parentId"),
                         json::requireString(item, "name"),
                         json::requireBool(item, "directory"),
                         json::requireInt(item, "size"),
                         json::requireInt(item, "modifiedAt")});
    }
    return nodes;
}

/**
 * @brief 在指定远程父目录下创建目录。
 * @param parentId 父目录 ID，0 为根目录。
 * @param name 新目录名；json::quote() 会处理引号、反斜杠等 JSON 特殊字符。
 * @return 响应 nodeId，即新建目录的唯一 ID。
 *
 * 请求 JSON 接口：{"token": string, "parentId": integer, "name": string}；
 * 响应 JSON 接口：{"nodeId": integer}。
 */
std::int64_t ClientCore::mkdir(std::int64_t parentId,
                               const std::string& name) {
    const auto response = jsonResponse(
        MessageType::MkdirReq,
        authJson({{"parentId", std::to_string(parentId)},
                  {"name", json::quote(name)}}));
    return json::requireInt(response, "nodeId");
}

/**
 * @brief 修改节点名称，不改变节点的父目录。
 * @param nodeId 文件或目录的节点 ID。
 * @param name 新节点名。
 *
 * 请求 JSON 接口：{"token": string, "nodeId": integer, "name": string}。
 * 成功响应没有本函数需要返回的字段；无异常即表示服务端已经接受修改。
 */
void ClientCore::renameNode(std::int64_t nodeId,
                            const std::string& name) {
    jsonResponse(
        MessageType::RenameReq,
        authJson({{"nodeId", std::to_string(nodeId)},
                  {"name", json::quote(name)}}));
}

/**
 * @brief 请求服务端删除指定节点。
 * @param nodeId 文件或目录的节点 ID。
 *
 * 请求 JSON 接口：{"token": string, "nodeId": integer}。删除规则完全由服务端仓库层
 * 决定；客户端不预先判断节点类型，也不在本地维护文件树缓存。
 */
void ClientCore::deleteNode(std::int64_t nodeId) {
    jsonResponse(MessageType::DeleteReq,
                 authJson({{"nodeId", std::to_string(nodeId)}}));
}

std::int64_t ClientCore::convertToMarkdown(std::int64_t nodeId,
                                           const std::string& outputName) {
    const auto response = jsonResponse(
        MessageType::ConvertReq,
        authJson({{"nodeId", std::to_string(nodeId)},
                  {"outputName", json::quote(outputName)}}));
    return json::requireInt(response, "nodeId");
}

/**
 * @brief 完成本地文件检查、秒传协商、分块发送和上传确认。
 * @param localPath 本地源文件路径，必须指向普通文件。
 * @param parentId 远程目标目录 ID。
 * @param remoteName 指定远程文件名；空字符串表示使用本地文件名。
 * @param progress 可选同步回调，收到每个块的 ACK 后调用一次。
 * @return 服务端最终分配或复用的远程节点 ID。
 *
 * 协议分为三阶段：
 *   1. UploadInitReq 发送 token/parentId/name/size/sha256；服务端可能返回 instant=true；
 *   2. 非秒传时循环发送 UploadChunk 二进制包，并以 ACK 的 received 字段推进 offset；
 *   3. UploadFinishReq 携带 transferId，让服务端校验并提交文件节点。
 *
 * UploadChunk 正文布局是固定协议，不能随意改变：
 *   [0, 64)   64 字节 token ASCII 文本
 *   [64, 96)  32 字节 transferId ASCII 文本
 *   [96, 104) 8 字节大端序 offset
 *   [104, ...) 当前文件块的原始二进制数据
 * token 和 transferId 的固定长度由服务端会话/传输 ID 生成规则保证。若将来改变其编码，
 * 客户端与服务端必须同步修改此布局。
 */
std::int64_t ClientCore::upload(const std::filesystem::path& localPath,
                                std::int64_t parentId,
                                const std::string& remoteName,
                                ProgressCallback progress) {
    // 在计算大小和散列前明确拒绝目录、设备等非普通文件路径。
    if (!std::filesystem::is_regular_file(localPath)) {
        throw std::runtime_error(
            "local upload path is not a regular file");
    }

    const auto size =
        static_cast<std::int64_t>(std::filesystem::file_size(localPath));
    const auto sha = sha256File(localPath);
    const auto name =
        remoteName.empty() ? localPath.filename().string() : remoteName;

    const auto init = jsonResponse(
        MessageType::UploadInitReq,
        authJson({{"parentId", std::to_string(parentId)},
                  {"name", json::quote(name)},
                  {"size", std::to_string(size)},
                  {"sha256", json::quote(sha)}}));

    // 秒传路径无需打开文件或发送数据块，但仍把进度一次性报告为完成。
    if (json::requireBool(init, "instant")) {
        if (progress) {
            progress(size, size);
        }
        return json::requireInt(init, "nodeId");
    }

    const auto transfer = json::requireString(init, "transferId");
    const auto chunkSize =
        static_cast<std::size_t>(json::requireInt(init, "chunkSize"));
    std::ifstream in(localPath, std::ios::binary);
    std::int64_t offset = 0;

    while (offset < size) {
        const auto count = static_cast<std::size_t>(
            std::min<std::int64_t>(chunkSize, size - offset));

        // 104 字节协议前缀之后紧跟 count 字节文件内容。
        std::vector<std::uint8_t> body(64 + 32 + 8 + count);
        std::copy(token_.begin(), token_.end(), body.begin());
        std::copy(transfer.begin(), transfer.end(), body.begin() + 64);
        put64(body.data() + 96, static_cast<std::uint64_t>(offset));

        in.read(reinterpret_cast<char*>(body.data() + 104),
                static_cast<std::streamsize>(count));
        if (static_cast<std::size_t>(in.gcount()) != count) {
            throw std::runtime_error("cannot read local upload file");
        }

        // FlagBinary 告诉服务端不要把正文按 JSON 解析。
        const auto response = request(makePacket(
            MessageType::UploadChunk, nextRequestId_++, std::move(body),
            FlagBinary));
        const auto ack = json::parseObject(bodyAsString(response));

        // 使用服务端确认值而不是简单 offset += count，使客户端状态与服务端实际接收量一致。
        offset = json::requireInt(ack, "received");
        if (progress) {
            progress(offset, size);
        }
    }

    const auto finish = jsonResponse(
        MessageType::UploadFinishReq,
        authJson({{"transferId", json::quote(transfer)}}));
    return json::requireInt(finish, "nodeId");
}

/**
 * @brief 初始化下载、循环拉取二进制块，并原子式提交经过校验的本地文件。
 * @param nodeId 要下载的远程文件节点 ID。
 * @param localPath 最终保存路径，必须尚不存在。
 * @param progress 可选同步回调，每成功写入一个块后调用。
 *
 * DownloadInitResp 提供 transferId、size、sha256、chunkSize。随后每个 DownloadChunkReq
 * 都携带 token/transferId/offset/maxBytes，成功响应必须为 DownloadChunk + FlagBinary；
 * FlagFinal 表示服务端已发送最后一块。即使 size==0，也必须至少请求一次空数据块以取得
 * FlagFinal，因此循环条件包含“|| size == 0”，并依赖 break 退出零字节文件的循环。
 *
 * 写入过程使用 localPath + ".download"，最后验证实际字节数和 SHA-256。只有两项均匹配
 * 才重命名为正式路径；try 块内任何异常都会进入 catch，关闭并尽力删除临时文件后原样抛出。
 */
void ClientCore::download(std::int64_t nodeId,
                          const std::filesystem::path& localPath,
                          ProgressCallback progress) {
    // 明确禁止覆盖任何已有文件或目录，避免用户数据被 std::ios::trunc 截断。
    if (std::filesystem::exists(localPath)) {
        throw std::runtime_error("download target already exists");
    }

    const auto init = jsonResponse(
        MessageType::DownloadInitReq,
        authJson({{"nodeId", std::to_string(nodeId)}}));
    const auto transfer = json::requireString(init, "transferId");
    const auto size = json::requireInt(init, "size");
    const auto expected = json::requireString(init, "sha256");
    const auto chunkSize = json::requireInt(init, "chunkSize");

    auto temporary = localPath;
    temporary += ".download";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot create local download file");
    }

    std::int64_t offset = 0;
    try {
        while (offset < size || size == 0) {
            auto response = request(
                MessageType::DownloadChunkReq,
                authJson({{"transferId", json::quote(transfer)},
                          {"offset", std::to_string(offset)},
                          {"maxBytes", std::to_string(chunkSize)}}));

            if (response.header.type != MessageType::DownloadChunk ||
                (response.header.flags & FlagBinary) == 0) {
                throw std::runtime_error("invalid download block response");
            }

            out.write(
                reinterpret_cast<const char*>(response.body.data()),
                static_cast<std::streamsize>(response.body.size()));
            if (!out) {
                throw std::runtime_error("cannot write local download file");
            }

            offset += static_cast<std::int64_t>(response.body.size());
            if (progress) {
                progress(offset, size);
            }
            if ((response.header.flags & FlagFinal) != 0) {
                break;
            }
        }

        // 必须先 close() 刷新缓冲区，随后 sha256File() 才能看到全部已写入内容。
        out.close();
        if (offset != size || sha256File(temporary) != expected) {
            throw std::runtime_error(
                "download size or SHA-256 verification failed");
        }
        std::filesystem::rename(temporary, localPath);
    } catch (...) {
        out.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

} // namespace cloud::client
