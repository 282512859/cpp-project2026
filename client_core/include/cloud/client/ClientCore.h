// 负责人：成员3：客户端网络
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

struct RemoteNode {
    std::int64_t id{};
    std::int64_t parentId{};
    std::string name;
    bool directory{};
    std::int64_t size{};
    std::int64_t modifiedAt{};
};

class ClientError : public std::runtime_error {
public:
    ClientError(std::string code, std::string message)
        : std::runtime_error(std::move(message)), code_(std::move(code)) {}
    [[nodiscard]] const std::string& code() const noexcept { return code_; }
private:
    std::string code_;
};

class ClientCore {
public:
    using ProgressCallback = std::function<void(std::int64_t, std::int64_t)>;

    ClientCore(const std::string& host, std::uint16_t port);
    void registerUser(const std::string& username, const std::string& password);
    void login(const std::string& username, const std::string& password);
    void logout();
    [[nodiscard]] bool loggedIn() const noexcept { return !token_.empty(); }

    std::vector<RemoteNode> list(std::int64_t parentId = 0);
    std::int64_t mkdir(std::int64_t parentId, const std::string& name);
    void renameNode(std::int64_t nodeId, const std::string& name);
    void deleteNode(std::int64_t nodeId);
    std::int64_t upload(const std::filesystem::path& localPath,
                        std::int64_t parentId,
                        const std::string& remoteName = {},
                        ProgressCallback progress = {});
    void download(std::int64_t nodeId, const std::filesystem::path& localPath,
                  ProgressCallback progress = {});

private:
    cloud::common::Packet request(cloud::common::MessageType type,
                                  const std::string& json);
    cloud::common::Packet request(cloud::common::Packet packet);
    cloud::common::json::Object jsonResponse(cloud::common::MessageType type,
                                             const std::string& json);
    std::string authJson(std::initializer_list<std::pair<std::string,std::string>> fields) const;

    cloud::common::SocketRuntime socketRuntime_;
    cloud::common::Socket socket_;
    std::uint64_t nextRequestId_{1};
    std::string token_;
};

} // namespace cloud::client
