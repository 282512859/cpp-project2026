// 负责人：成员2：服务端存储
#pragma once

#include "cloud/server/SqliteApi.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cloud::server {

struct NodeInfo {
    std::int64_t id{};
    std::int64_t parentId{};
    std::string name;
    bool directory{};
    std::int64_t size{};
    std::int64_t modifiedAt{};
};

struct UploadInitResult {
    std::string transferId;
    bool instant{};
    std::int64_t nodeId{};
    std::int64_t received{};
};

struct DownloadInfo {
    std::string transferId;
    std::string name;
    std::string sha256;
    std::int64_t size{};
};

struct StoredFileInfo {
    std::int64_t parentId{};
    std::string name;
    std::filesystem::path blobPath;
    std::int64_t size{};
};

class CloudRepository {
public:
    CloudRepository(std::filesystem::path databasePath,
                    std::filesystem::path storageRoot);
    ~CloudRepository();
    CloudRepository(const CloudRepository&) = delete;
    CloudRepository& operator=(const CloudRepository&) = delete;

    void registerUser(const std::string& username, const std::string& password);
    std::optional<std::int64_t> authenticate(const std::string& username,
                                             const std::string& password);
    std::vector<NodeInfo> list(std::int64_t userId, std::int64_t parentId);
    std::int64_t mkdir(std::int64_t userId, std::int64_t parentId,
                       const std::string& name);
    void renameNode(std::int64_t userId, std::int64_t nodeId,
                    const std::string& name);
    void deleteNode(std::int64_t userId, std::int64_t nodeId);
    std::string createShareCode(std::int64_t userId, std::int64_t nodeId);
    std::int64_t claimShareCode(std::int64_t userId, const std::string& code);

    StoredFileInfo getStoredFile(std::int64_t userId, std::int64_t nodeId);
    std::int64_t importLocalFile(std::int64_t userId, std::int64_t parentId,
                                 const std::string& name,
                                 const std::filesystem::path& localPath);

    UploadInitResult beginUpload(std::int64_t userId, std::int64_t parentId,
                                 const std::string& name, std::int64_t size,
                                 const std::string& sha256);
    std::int64_t appendUpload(std::int64_t userId, const std::string& transferId,
                              std::int64_t offset, const std::uint8_t* data,
                              std::size_t size);
    std::int64_t finishUpload(std::int64_t userId, const std::string& transferId);

    DownloadInfo beginDownload(std::int64_t userId, std::int64_t nodeId);
    std::vector<std::uint8_t> readDownload(std::int64_t userId,
                                           const std::string& transferId,
                                           std::int64_t offset,
                                           std::size_t maxBytes,
                                           bool& final);
    void cleanupExpiredUploads();

    /**
     * 暴露底层 SQLite 连接。
     *
     * 扩展功能模块（ExtendedFeatureService）与仓库位于同一个数据库文件，
     * 因此需要用它设置统一的 busy timeout，避免两条连接同时写入时直接报
     * SQLITE_BUSY。语句串行执行仍分别由各自的互斥量保证。
     */
    [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

private:
    struct UploadState {
        std::int64_t userId{};
        std::int64_t parentId{};
        std::string name;
        std::string sha256;
        std::int64_t expectedSize{};
        std::int64_t received{};
        std::filesystem::path tempPath;
        // One transfer is sequential even when a token is reused from another
        // connection; unrelated transfers must not block on file I/O.
        std::shared_ptr<std::mutex> ioMutex{std::make_shared<std::mutex>()};
    };
    struct DownloadState {
        std::int64_t userId{};
        std::filesystem::path blobPath;
        std::int64_t size{};
    };

    void migrate();
    void exec(const std::string& sql);
    void ensureColumn(const std::string& table, const std::string& column,
                      const std::string& ddl);
    void ensureQuota(std::int64_t userId, std::int64_t extraBytes);
    void requireParentDirectory(std::int64_t userId, std::int64_t parentId);
    void validateName(const std::string& name) const;
    std::filesystem::path blobPath(const std::string& sha256) const;

    sqlite3* db_{};
    std::filesystem::path storageRoot_;
    std::mutex mutex_;
    std::map<std::string, UploadState> uploads_;
    std::map<std::string, DownloadState> downloads_;
};

} // namespace cloud::server
