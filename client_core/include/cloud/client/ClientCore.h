// 负责人：成员3：客户端网络
//
// 本文件定义不依赖 Qt 的同步客户端核心。上层既可以像 client_cli 一样直接调用，
// 也可以像 QtClient 一样把调用放到工作线程中。若要重写实现，建议保持本文件中的
// 公共类型和函数签名稳定，这样命令行客户端与 Qt 适配层无需随之修改。
#pragma once

#include "cloud/common/JsonLite.h"
#include "cloud/common/MessageType.h"
#include "cloud/common/Packet.h"
#include "cloud/common/Socket.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cloud::client {

/**
 * @brief 服务端文件树中一个节点的客户端表示。
 *
 * list() 把服务端返回的每个 JSON 条目转换为此结构。目录和普通文件共用该类型；
 * directory 用于区分二者。这里使用固定宽度整数，避免不同平台上 long 宽度不同。
 */
struct RemoteNode {
    std::int64_t id{};          ///< 节点唯一 ID；rename/delete/download 等接口用它定位节点。
    std::int64_t parentId{};    ///< 父目录 ID；根目录约定为 0。
    std::string name;           ///< 节点在父目录中的名称，内容来自服务端 UTF-8 JSON 字符串。
    bool directory{};           ///< true 表示目录，false 表示普通文件。
    std::int64_t size{};        ///< 文件字节数；目录通常为 0，以服务端返回值为准。
    std::int64_t modifiedAt{};  ///< 最后修改时间，单位和纪元由服务端协议定义（当前为 Unix 毫秒）。
};

struct DocumentPreview {
    std::string name;
    std::string content;
    bool markdown{};
    bool truncated{};
};

/**
 * @brief 服务端明确返回错误响应时抛出的异常。
 *
 * 网络失败、文件 I/O 失败和协议不匹配仍使用 std::runtime_error；只有带有
 * FlagError 的服务端响应会被 request(Packet) 转换成 ClientError。调用者因此可以
 * 通过 catch(ClientError&) 展示业务错误码，再用 catch(std::exception&) 处理本地错误。
 */
class ClientError : public std::runtime_error {
public:
    /**
     * @param code 服务端 JSON 的 errorCode 字段，例如 UNAUTHORIZED。
     * @param message 面向用户或日志的错误说明，同时作为 what() 的返回内容。
     */
    ClientError(std::string code, std::string message)
        : std::runtime_error(std::move(message)), code_(std::move(code)) {}

    /**
     * @brief 取得服务端错误码。
     * @return 对异常对象内部字符串的只读引用；异常对象销毁后该引用失效。
     */
    [[nodiscard]] const std::string& code() const noexcept { return code_; }

private:
    std::string code_; ///< 与 std::runtime_error 保存的 message 分开保存的机器可读错误码。
};

/**
 * @brief 云盘协议的同步客户端。
 *
 * 一个对象持有一条 TCP 连接和一个登录 token。所有公开操作都会阻塞当前线程，直到
 * 收到服务端响应或抛出异常；对象本身没有加锁，因此不能被多个线程并发调用。
 * Qt 界面应通过 QtClient/ClientWorker 在专用工作线程中使用它。
 *
 * 典型调用顺序：构造连接 -> registerUser（可选）-> login -> 文件操作 -> logout。
 */
class ClientCore {
public:
    /**
     * @brief 上传/下载进度回调类型。
     * @param done 已成功确认（上传）或写入（下载）的字节数。
     * @param total 文件总字节数；正常情况下 done 位于 [0, total]。
     *
     * 回调在发起传输的同一线程内同步执行，不应在回调中再次调用同一个 ClientCore，
     * 否则会打乱当前连接上的请求—响应顺序。
     */
    using ProgressCallback = std::function<void(std::int64_t done, std::int64_t total)>;

    /**
     * @brief 建立 TCP 连接并完成 HELLO 协议握手。
     * @param host 服务端主机名或 IP 地址，例如 "127.0.0.1"。
     * @param port 服务端 TCP 端口，通常传 cloud::common::kDefaultPort。
     * @throws std::exception 连接、收发或 HELLO 校验失败。
     *
     * 构造成功只表示连接和协议可用，不表示用户已经登录。
     */
    ClientCore(const std::string& host, std::uint16_t port);

    /**
     * @brief 注册新用户。
     * @param username 要创建的用户名。
     * @param password 明文密码；本层按现有协议直接放入 JSON，请勿在日志中输出。
     * @throws ClientError 服务端拒绝注册（如用户名重复或参数非法）。
     * @throws std::exception 网络、协议或 JSON 处理失败。
     *
     * 注册成功不会自动登录，之后仍需调用 login()。
     */
    void registerUser(const std::string& username, const std::string& password);

    /**
     * @brief 使用用户名和密码登录，并保存服务端返回的会话 token。
     * @param username 已注册用户名。
     * @param password 对应密码。
     * @throws ClientError 认证失败或服务端拒绝请求。
     * @throws std::exception 网络、协议或响应缺少 token。
     *
     * 只有完整成功后才会更新 token_；后续需要认证的接口自动携带该 token。
     */
    void login(const std::string& username, const std::string& password);

    /**
     * @brief 注销当前会话。
     * @throws ClientError token 已失效或服务端拒绝注销。
     * @throws std::exception 网络、协议或 JSON 处理失败。
     *
     * 未登录时该函数直接返回。当前实现仅在服务端确认成功后清空本地 token；若请求
     * 失败，token 保留，方便调用者决定是否重试。
     */
    void logout();

    /**
     * @brief 判断本对象是否保存了登录 token。
     * @return token_ 非空时为 true。
     *
     * 这只是本地状态，不会向服务端验证 token 是否仍有效或是否已经过期。
     */
    [[nodiscard]] bool loggedIn() const noexcept { return !token_.empty(); }

    /**
     * @brief 列出指定远程目录的直接子节点。
     * @param parentId 目录节点 ID；默认值 0 表示根目录。
     * @return 按服务端返回顺序排列的 RemoteNode 列表。
     * @throws ClientError 未登录、目录不存在或无权访问。
     * @throws std::exception 网络、协议或响应 JSON 字段不合法。
     */
    std::vector<RemoteNode> list(std::int64_t parentId = 0);

    /**
     * @brief 在远程目录下创建一个子目录。
     * @param parentId 父目录节点 ID；0 表示根目录。
     * @param name 新目录名称，不是本地路径或完整远程路径。
     * @return 服务端为新目录分配的节点 ID。
     * @throws ClientError 未登录、重名、父目录无效或名称非法。
     * @throws std::exception 网络、协议或响应解析失败。
     */
    std::int64_t mkdir(std::int64_t parentId, const std::string& name);

    /**
     * @brief 修改远程文件或目录的名称。
     * @param nodeId 待修改节点的唯一 ID。
     * @param name 新名称；只改节点名，不移动其父目录。
     * @throws ClientError 未登录、节点不存在、重名或名称非法。
     * @throws std::exception 网络、协议或响应解析失败。
     */
    void renameNode(std::int64_t nodeId, const std::string& name);

    /**
     * @brief 删除远程文件或目录节点。
     * @param nodeId 待删除节点的唯一 ID。
     * @throws ClientError 未登录、节点不存在或服务端不允许删除。
     * @throws std::exception 网络、协议或响应解析失败。
     *
     * 是否递归删除、能否删除非空目录等策略由服务端实现决定。
     */
    void deleteNode(std::int64_t nodeId);
    std::string createShareCode(std::int64_t nodeId);
    std::int64_t claimShareCode(const std::string& code);

    // Converts a remote .docx or .pdf on the server and stores the generated
    // Markdown file beside the source node. An empty outputName uses the
    // source stem plus ".md".
    std::int64_t convertToMarkdown(std::int64_t nodeId,
                                   const std::string& outputName = {});
    DocumentPreview preview(std::int64_t nodeId, std::int64_t maxBytes = 512 * 1024);

    /**
     * @brief 把一个本地普通文件分块上传到远程目录。
     * @param localPath 本地源文件路径；必须存在且为普通文件。
     * @param parentId 远程目标目录 ID；0 表示根目录。
     * @param remoteName 远程文件名；为空时采用 localPath 的文件名部分。
     * @param progress 可选进度回调，参数依次为已确认字节数和总字节数。
     * @return 上传完成后服务端创建的远程文件节点 ID。
     * @throws ClientError 未登录、目标无效、重名或服务端校验失败。
     * @throws std::exception 本地文件不可读、网络中断、协议或 JSON 处理失败。
     *
     * 接口先计算整个文件的 SHA-256。若服务端已有相同内容，会执行“秒传”并直接
     * 回调 progress(size, size)；否则按照服务端给出的 chunkSize 逐块发送。
     */
    std::int64_t upload(const std::filesystem::path& localPath,
                        std::int64_t parentId,
                        const std::string& remoteName = {},
                        ProgressCallback progress = {});

    /**
     * @brief 把远程普通文件分块下载到一个新的本地文件。
     * @param nodeId 远程文件节点 ID，不能是目录。
     * @param localPath 最终保存路径；当前实现拒绝覆盖已经存在的路径。
     * @param progress 可选进度回调，参数依次为已写入字节数和总字节数。
     * @throws ClientError 未登录、节点无效或服务端拒绝下载。
     * @throws std::exception 目标文件不可创建/写入、网络中断、大小或 SHA-256 校验失败。
     *
     * 数据先写入“localPath + .download”临时文件。仅在大小和 SHA-256 都正确后才重命名
     * 为 localPath；发生异常时会尽力删除临时文件，避免留下不完整的正式文件。
     */
    void download(std::int64_t nodeId, const std::filesystem::path& localPath,
                  ProgressCallback progress = {});

private:
    /**
     * @brief 用 JSON 文本创建一个新请求包并同步等待响应。
     * @param type 请求的消息类型。
     * @param json 完整 JSON 对象文本，不负责自动加入 token。
     * @return 已通过 request(Packet) 完成 requestId、响应标志和错误检查的响应包。
     *
     * 每次调用消耗一个递增的 nextRequestId_。若重写为异步/并发实现，请继续保证
     * requestId 唯一，并按 ID 将响应交给正确请求。
     */
    cloud::common::Packet request(cloud::common::MessageType type,
                                  const std::string& json);

    /**
     * @brief 发送已构造的数据包并接收与其匹配的单个响应。
     * @param packet 完整请求包，可承载 JSON 或上传二进制块。
     * @return 匹配 requestId 且设置 FlagResponse、同时未设置 FlagError 的响应包。
     * @throws ClientError 服务端返回带 FlagError 的 JSON 错误包。
     * @throws std::exception 收发失败、响应 ID/标志不匹配或错误 JSON 无法解析。
     *
     * 这是连接请求—响应语义的集中入口。当前协议没有在此校验具体响应 MessageType，
     * 各业务方法按需解析或额外校验。
     */
    cloud::common::Packet request(cloud::common::Packet packet);

    /**
     * @brief 发送 JSON 请求并把响应正文解析为 JSON 对象。
     * @param type 请求消息类型。
     * @param json 请求 JSON 对象文本。
     * @return 解析后的键值对象。
     * @throws ClientError 服务端业务错误。
     * @throws std::exception 网络、协议或 JSON 解析失败。
     */
    cloud::common::json::Object jsonResponse(cloud::common::MessageType type,
                                             const std::string& json);

    /**
     * @brief 构造带当前登录 token 的 JSON 对象文本。
     * @param fields 额外字段集合；pair.first 是字段名，pair.second 必须已经是合法 JSON 值
     *               （字符串需先 json::quote，数字可用 std::to_string）。
     * @return 形如 {"token":"...", ...} 的 JSON 文本。
     *
     * 该辅助函数不检查是否已登录；token 为空时仍会发送空字符串，由服务端返回鉴权错误。
     */
    std::string authJson(
        std::initializer_list<std::pair<std::string, std::string>> fields) const;

    // 声明顺序很重要：socketRuntime_ 先构造、最后析构，保证 socket_ 的整个生命周期内
    // 平台网络库（Windows 上为 Winsock）都处于已初始化状态。
    cloud::common::SocketRuntime socketRuntime_; ///< 平台网络运行时的 RAII 守卫。
    cloud::common::Socket socket_;                ///< 与服务端之间唯一、独占的 TCP 连接。
    std::uint64_t nextRequestId_{1};              ///< 下一个非零请求 ID；服务端禁止 requestId 为 0。
    std::string token_;                           ///< login() 获取、logout() 清除的会话 token。
};

} // namespace cloud::client
