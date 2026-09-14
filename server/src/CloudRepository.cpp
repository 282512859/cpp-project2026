// 负责人：成员2：服务端存储
//
// 本文件实现云盘服务端的核心存储功能，包括：
// 1. 用户注册与登录验证；
// 2. 文件夹创建、文件列表查询、重命名和删除；
// 3. 文件上传、断点续传和本地文件导入；
// 4. 文件下载和分块读取；
// 5. 文件分享码的创建与领取；
// 6. SQLite 数据库表结构创建与升级；
// 7. 文件物理存储、哈希去重和用户配额管理。
//
// 重要设计：
// - SQLite 数据库保存“文件元数据”，不直接保存文件内容；
// - 文件内容保存在 storageRoot_/blobs 目录中；
// - blobs 表记录物理文件；
// - nodes 表记录用户看到的文件树；
// - 多个用户的文件节点可以引用同一个 blob，实现文件去重。

#include "cloud/server/CloudRepository.h"
#include "cloud/server/ServiceError.h"
#include "cloud/common/ErrorCode.h"
#include "cloud/common/Sha256.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace cloud::server {
namespace {

using cloud::common::ErrorCode;

/**
 * 获取当前时间。
 *
 * 返回值：
 * - 从 Unix Epoch，即 1970-01-01 00:00:00 UTC 开始计算的毫秒数。
 *
 * 用途：
 * - 记录用户创建时间；
 * - 记录文件修改时间；
 * - 记录分享码创建和过期时间；
 * - 记录上传会话的过期时间。
 */
std::int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// 每个新用户默认拥有 10 GiB 存储空间。
constexpr std::int64_t kDefaultQuotaBytes = 10LL * 1024 * 1024 * 1024;

// 上传会话在最后一次活动后的有效时间：24 小时。
// 超过该时间后，后台清理逻辑会删除对应的临时文件。
constexpr std::int64_t kUploadSessionTtlMillis = 24LL * 60 * 60 * 1000;

/**
 * SQLite 预编译语句封装类。
 *
 * SQL 执行通常分为两步：
 *
 * 1. sqlite3_prepare_v2：
 *    把 SQL 字符串编译成 sqlite3_stmt。
 *
 * 2. sqlite3_step：
 *    真正执行这条 SQL。
 *
 * 该类使用 RAII 管理 sqlite3_stmt：
 * - 构造时编译 SQL；
 * - 析构时自动释放 SQL 语句；
 * - 如果绑定参数或执行失败，则抛出 ServiceError。
 *
 * 这样可以避免忘记调用 sqlite3_finalize()。
 */
class Statement {
private:
    // 当前 SQL 语句所使用的数据库连接。
    sqlite3* db_{};

    // SQLite 编译后的 SQL 语句对象。
    sqlite3_stmt* statement_{};

public:
    /**
     * 构造并编译一条 SQL 语句。
     *
     * 参数：
     * - db：SQLite 数据库连接；
     * - sql：需要执行的 SQL 字符串。
     *
     * 例如：
     *     Statement query(db_, "SELECT id FROM users WHERE username=?");
     */
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr) != SQLITE_OK)
            throw ServiceError(ErrorCode::DbError, sqlite3_errmsg(db));
    }

    /**
     * 析构 SQL 语句对象。
     *
     * sqlite3_finalize() 会释放 SQLite 为该语句分配的资源。
     */
    ~Statement() {
        if (statement_)
            sqlite3_finalize(statement_);
    }

    /**
     * 获取底层 SQLite 语句指针。
     *
     * 返回值：
     * - sqlite3_stmt*，可传给 sqlite3_column_int64、
     *   sqlite3_column_text 等 SQLite 函数。
     */
    sqlite3_stmt* get() const {
        return statement_;
    }

    /**
     * 给 SQL 中的指定占位符绑定字符串。
     *
     * SQL 中的 ? 是参数占位符，编号从 1 开始。
     *
     * 例如：
     *     SELECT id FROM users WHERE username=?
     *
     * 对应：
     *     query.text(1, username);
     *
     * 参数：
     * - index：占位符编号，从 1 开始；
     * - value：需要绑定的字符串。
     *
     * SQLITE_TRANSIENT 表示 SQLite 会复制字符串内容，
     * 因此不依赖 value 的生命周期。
     */
    void text(int index, const std::string& value) {
        if (sqlite3_bind_text(
                statement_,
                index,
                value.data(),
                static_cast<int>(value.size()),
                SQLITE_TRANSIENT) != SQLITE_OK) {
            throw ServiceError(ErrorCode::DbError, sqlite3_errmsg(db_));
        }
    }

    /**
     * 给 SQL 中的指定占位符绑定 64 位整数。
     *
     * 常用于绑定：
     * - 用户 ID；
     * - 文件 ID；
     * - 文件大小；
     * - 时间戳；
     * - 目录 ID。
     *
     * 参数：
     * - index：占位符编号，从 1 开始；
     * - value：要绑定的整数。
     */
    void integer(int index, std::int64_t value) {
        if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK)
            throw ServiceError(ErrorCode::DbError, sqlite3_errmsg(db_));
    }

    /**
     * 执行一次 SQL。
     *
     * 返回值：
     * - SQLITE_ROW：SELECT 查询得到一行结果；
     * - SQLITE_DONE：SQL 执行完成，但没有返回行。
     *
     * 对于 INSERT、UPDATE、DELETE，通常返回 SQLITE_DONE。
     * 对于 SELECT，需要重复调用 step()，直到返回 SQLITE_DONE。
     */
    int step() {
        const int result = sqlite3_step(statement_);

        if (result != SQLITE_ROW && result != SQLITE_DONE)
            throw ServiceError(ErrorCode::DbError, sqlite3_errmsg(db_));

        return result;
    }
};

/**
 * 读取 SQL 查询结果中的字符串列。
 *
 * 参数：
 * - s：已经执行过 step() 的 SQL 语句；
 * - column：要读取的列编号，从 0 开始。
 *
 * 返回值：
 * - 如果列不是 NULL，返回列中的字符串；
 * - 如果列是 NULL，返回空字符串。
 */
std::string columnText(sqlite3_stmt* s, int column) {
    const auto* value = sqlite3_column_text(s, column);
    return value
        ? reinterpret_cast<const char*>(value)
        : std::string{};
}

/**
 * 检查字符串是否是合法的 SHA-256 哈希值。
 *
 * 一个 SHA-256 的十六进制字符串必须满足：
 * - 长度为 64；
 * - 每个字符只能是 0-9 或 a-f；
 * - 当前实现要求使用小写字母。
 *
 * 参数：
 * - sha：待检查的哈希字符串。
 *
 * 异常：
 * - 如果格式错误，抛出 BadRequest。
 */
void ensureSha(const std::string& sha) {
    if (sha.size() != 64 ||
        !std::all_of(
            sha.begin(),
            sha.end(),
            [](unsigned char c) {
                return (c >= '0' && c <= '9') ||
                       (c >= 'a' && c <= 'f');
            })) {
        throw ServiceError(
            ErrorCode::BadRequest,
            "invalid SHA-256 value");
    }
}

// PBKDF2-HMAC-SHA-256 的迭代次数。
// 迭代次数越高，密码验证越慢，离线暴力破解的成本也越高。
constexpr std::uint32_t kPbkdf2Iterations = 10000;

// 把二进制字节转换为小写十六进制字符串。
std::string bytesToHex(const std::string& bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);

    for (const auto byte : bytes) {
        const auto value = static_cast<unsigned char>(byte);
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0x0f]);
    }

    return result;
}

// 使用项目已有的 SHA-256 接口生成二进制摘要。
// sha256Hex() 返回 64 个十六进制字符，这里将其还原为 32 字节。
std::string sha256Bytes(const std::string& data) {
    const auto hex = cloud::common::sha256Hex(data);
    std::string bytes;
    bytes.reserve(hex.size() / 2);

    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const auto high = static_cast<unsigned char>(hex[i]);
        const auto low = static_cast<unsigned char>(hex[i + 1]);

        const auto decode = [](unsigned char c) -> unsigned char {
            if (c >= '0' && c <= '9')
                return static_cast<unsigned char>(c - '0');
            return static_cast<unsigned char>(c - 'a' + 10);
        };

        bytes.push_back(static_cast<char>(
            (decode(high) << 4) | decode(low)));
    }

    return bytes;
}

// 计算 HMAC-SHA-256。
// HMAC 的核心是使用同一个密钥分别计算内外两层哈希：
// H((K xor opad) || H((K xor ipad) || message))。
std::string hmacSha256(
    const std::string& key,
    const std::string& message)
    {
    constexpr std::size_t blockSize = 64;
    std::string normalizedKey = key;

    if (normalizedKey.size() > blockSize)
        normalizedKey = sha256Bytes(normalizedKey);

    normalizedKey.resize(blockSize, '\0');

    std::string innerPad(blockSize, '\x36');
    std::string outerPad(blockSize, '\x5c');

    for (std::size_t i = 0; i < blockSize; ++i) {
        innerPad[i] = static_cast<char>(
            static_cast<unsigned char>(innerPad[i]) ^
            static_cast<unsigned char>(normalizedKey[i]));
        outerPad[i] = static_cast<char>(
            static_cast<unsigned char>(outerPad[i]) ^
            static_cast<unsigned char>(normalizedKey[i]));
    }

    return sha256Bytes(outerPad + sha256Bytes(innerPad + message));
}

// PBKDF2-HMAC-SHA-256，派生 32 字节密码验证值。
// salt 使用数据库中的随机盐字符串；每个块的编号按大端序编码。
std::string pbkdf2Sha256(
    const std::string& password,
    const std::string& salt,
    std::uint32_t iterations) {
    std::string blockInput = salt;
    blockInput.append("\0\0\0\1", 4);

    auto block = hmacSha256(password, blockInput);
    std::string result = block;

    for (std::uint32_t i = 1; i < iterations; ++i) {
        block = hmacSha256(password, block);

        for (std::size_t j = 0; j < result.size(); ++j) {
            result[j] = static_cast<char>(
                static_cast<unsigned char>(result[j]) ^
                static_cast<unsigned char>(block[j]));
        }
    }

    return result;
}

// 固定时间比较两个字符串，避免比较过程泄露哈希前缀信息。
bool constantTimeEqual(
    const std::string& left,
    const std::string& right) {
    if (left.size() != right.size())
        return false;

    unsigned char difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        difference = static_cast<unsigned char>(
            difference |
            (static_cast<unsigned char>(left[i]) ^
             static_cast<unsigned char>(right[i])));
    }

    return difference == 0;
}

// 生成带有算法和迭代次数元数据的密码哈希。
// 格式：pbkdf2_sha256$迭代次数$派生值十六进制字符串。
std::string makePasswordHash(
    const std::string& password,
    const std::string& salt) {
    return "pbkdf2_sha256$" +
        std::to_string(kPbkdf2Iterations) + "$" +
        bytesToHex(pbkdf2Sha256(
            password,
            salt,
            kPbkdf2Iterations));
}

// 验证 PBKDF2 哈希格式并重新派生密码验证值。
bool verifyPbkdf2Password(
    const std::string& password,
    const std::string& salt,
    const std::string& storedHash) {
    constexpr std::string_view prefix = "pbkdf2_sha256$";

    if (storedHash.compare(0, prefix.size(), prefix) != 0)
        return false;

    const auto separator = storedHash.find('$', prefix.size());
    if (separator == std::string::npos)
        return false;

    std::uint32_t iterations = 0;
    try {
        const auto text = storedHash.substr(prefix.size(),
                                            separator - prefix.size());
        const auto parsed = std::stoul(text);
        if (parsed == 0 || parsed > std::numeric_limits<std::uint32_t>::max())
            return false;
        iterations = static_cast<std::uint32_t>(parsed);
    } catch (const std::exception&) {
        return false;
    }

    const auto expectedHex = storedHash.substr(separator + 1);
    const auto actualHex = bytesToHex(pbkdf2Sha256(
        password,
        salt,
        iterations));

    return constantTimeEqual(actualHex, expectedHex);
}

} // namespace

/**
 * 构造 CloudRepository 对象。
 *
 * 参数：
 * - databasePath：SQLite 数据库文件路径；
 * - storageRoot：文件实际保存的根目录。
 *
 * 执行流程：
 * 1. 创建数据库所在的父目录；
 * 2. 创建 temp 临时上传目录；
 * 3. 创建 blobs 正式文件目录；
 * 4. 打开 SQLite 数据库；
 * 5. 调用 migrate() 创建或升级数据库表。
 *
 * 注意：
 * - 数据库保存文件元数据；
 * - storageRoot 保存真实文件内容。
 */
CloudRepository::CloudRepository(
    std::filesystem::path databasePath,
    std::filesystem::path storageRoot)
    : storageRoot_(std::move(storageRoot)) {
    // 只有包含父目录时才创建目录；例如 databasePath 为 "cloud.db" 时，
    // parent_path() 是空路径，不能直接交给 create_directories()。
    if (!databasePath.parent_path().empty())
        std::filesystem::create_directories(databasePath.parent_path());

    std::filesystem::create_directories(storageRoot_ / "temp");
    std::filesystem::create_directories(storageRoot_ / "blobs");

    if (sqlite3_open(databasePath.string().c_str(), &db_) != SQLITE_OK) {
        const std::string error =
            db_ ? sqlite3_errmsg(db_) : "cannot open SQLite database";

        if (db_)
            sqlite3_close(db_);

        db_ = nullptr;
        throw ServiceError(ErrorCode::DbError, error);
    }

    migrate();
}

/**
 * 析构 CloudRepository。
 *
 * 关闭 SQLite 数据库连接，释放数据库相关资源。
 *
 * 临时上传文件由上传完成、上传失败或 cleanupExpiredUploads()
 * 负责清理。
 */
CloudRepository::~CloudRepository() {
    if (db_)
        sqlite3_close(db_);
}

/**
 * 执行一条不返回查询结果的 SQL。
 *
 * 参数：
 * - sql：完整 SQL 字符串。
 *
 * 适用场景：
 * - CREATE TABLE；
 * - CREATE INDEX；
 * - ALTER TABLE；
 * - BEGIN；
 * - COMMIT；
 * - ROLLBACK；
 * - PRAGMA。
 *
 * 如果执行失败，将 SQLite 错误转换为 ServiceError。
 */
void CloudRepository::exec(const std::string& sql) {
    char* error = nullptr;

    if (sqlite3_exec(
            db_,
            sql.c_str(),
            nullptr,
            nullptr,
            &error) != SQLITE_OK) {
        const std::string message =
            error ? error : sqlite3_errmsg(db_);

        if (error)
            sqlite3_free(error);

        throw ServiceError(ErrorCode::DbError, message);
    }
}

/**
 * 创建或升级数据库结构。
 *
 * 主要创建以下数据表：
 *
 * users：
 * - 保存用户名、密码哈希、盐值、配额和已用空间。
 *
 * blobs：
 * - 保存实际文件内容的 SHA-256、大小和物理路径；
 * - ref_count 表示有多少个文件节点引用该内容。
 *
 * nodes：
 * - 保存用户看到的文件和目录；
 * - parent_id 用于构建目录树；
 * - blob_id 指向实际文件内容。
 *
 * shares：
 * - 保存文件分享码；
 * - 记录分享文件、创建时间、过期时间和领取状态。
 *
 * upload_session：
 * - 保存断点续传信息；
 * - 记录临时文件路径和已经接收的字节数。
 *
 * 另外：
 * - PRAGMA foreign_keys=ON：启用外键约束；
 * - PRAGMA journal_mode=WAL：启用 SQLite WAL 模式；
 * - ensureColumn()：为旧版本数据库补充新增列。
 */
void CloudRepository::migrate() {
    exec("PRAGMA foreign_keys=ON;");
    exec("PRAGMA journal_mode=WAL;");

    exec(R"sql(
CREATE TABLE IF NOT EXISTS users(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 username TEXT NOT NULL UNIQUE,
 password_hash TEXT NOT NULL,
 salt TEXT NOT NULL,
 quota INTEGER NOT NULL DEFAULT 10737418240,
 storage_used INTEGER NOT NULL DEFAULT 0,
 created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS blobs(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 sha256 TEXT NOT NULL,
 size INTEGER NOT NULL,
 relative_path TEXT NOT NULL,
 ref_count INTEGER NOT NULL,
 created_at INTEGER NOT NULL,
 UNIQUE(sha256,size)
);

CREATE TABLE IF NOT EXISTS nodes(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
 parent_id INTEGER NOT NULL DEFAULT 0,
 name TEXT NOT NULL,
 is_directory INTEGER NOT NULL,
 blob_id INTEGER REFERENCES blobs(id),
 size INTEGER NOT NULL DEFAULT 0,
 modified_at INTEGER NOT NULL,
 UNIQUE(owner_id,parent_id,name)
);

CREATE INDEX IF NOT EXISTS idx_nodes_parent
ON nodes(owner_id,parent_id);

CREATE TABLE IF NOT EXISTS shares(
 code TEXT PRIMARY KEY COLLATE NOCASE,
 owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
 node_id INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
 created_at INTEGER NOT NULL,
 expires_at INTEGER NOT NULL,
 claimed_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_shares_expires
ON shares(expires_at);

CREATE TABLE IF NOT EXISTS upload_session(
 transfer_id TEXT PRIMARY KEY,
 user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
 parent_id INTEGER NOT NULL,
 name TEXT NOT NULL,
 sha256 TEXT NOT NULL,
 expected_size INTEGER NOT NULL,
 received INTEGER NOT NULL DEFAULT 0,
 temp_path TEXT NOT NULL,
 created_at INTEGER NOT NULL,
 expires_at INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_upload_resume
ON upload_session(user_id,parent_id,name,sha256,expected_size);

PRAGMA user_version=3;
)sql");

    // CREATE TABLE IF NOT EXISTS 不会修改已经存在的旧表。
    // 因此这里需要显式检查并补充新字段。
    ensureColumn(
        "users",
        "quota",
        "INTEGER NOT NULL DEFAULT 10737418240");

    ensureColumn(
        "users",
        "storage_used",
        "INTEGER NOT NULL DEFAULT 0");

    ensureColumn(
        "upload_session",
        "expires_at",
        "INTEGER NOT NULL DEFAULT 0");
}

/**
 * 验证文件名或目录名是否合法。
 *
 * 参数：
 * - name：用户提交的文件名或目录名。
 *
 * 规则：
 * 1. 不能是空字符串；
 * 2. 长度不能超过 255 个字节；
 * 3. 不能是 "." 或 ".."；
 * 4. 不能包含 '/'；
 * 5. 不能包含 '\\'；
 * 6. 不能包含 ASCII 控制字符。
 *
 * 这样可以防止：
 * - 路径穿越；
 * - 非法路径；
 * - 使用目录特殊名称；
 * - 生成无法正常访问的文件。
 */
void CloudRepository::validateName(const std::string& name) const {
    if (name.empty() ||
        name.size() > 255 ||
        name == "." ||
        name == ".." ||
        name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos ||
        std::any_of(
            name.begin(),
            name.end(),
            [](unsigned char c) {
                return c < 0x20;
            })) {
        throw ServiceError(
            ErrorCode::BadRequest,
            "invalid file or directory name");
    }
}

/**
 * 检查父目录是否存在并且属于指定用户。
 *
 * 参数：
 * - userId：当前用户 ID；
 * - parentId：父目录 ID，0 表示用户根目录。
 *
 * 检查逻辑：
 * 1. 如果 parentId 为 0，表示根目录，直接通过；
 * 2. 否则从 nodes 表中查询该节点；
 * 3. 检查该节点是否属于当前用户；
 * 4. 检查该节点是否确实是目录。
 */
void CloudRepository::requireParentDirectory(
    std::int64_t userId,
    std::int64_t parentId)
{
    if (parentId == 0)
        return;

    Statement query(
        db_,
        "SELECT is_directory "
        "FROM nodes "
        "WHERE id=? AND owner_id=?");

    query.integer(1, parentId);
    query.integer(2, userId);

    if (query.step() != SQLITE_ROW)
        throw ServiceError(
            ErrorCode::NotFound,
            "parent directory not found");

    if (!sqlite3_column_int(query.get(), 0))
        throw ServiceError(
            ErrorCode::BadRequest,
            "parent is not a directory");
}

/**
 * 注册新用户。
 *
 * 参数：
 * - username：用户名，长度要求为 3 到 32 个字节；
 * - password：明文密码，长度要求为 8 到 128 个字节。
 *
 * 密码处理：
 * 1. 生成随机 salt；
 * 2. 使用 PBKDF2-HMAC-SHA-256 对密码进行多轮派生；
 * 3. password_hash 保存算法名称、迭代次数和派生结果；
 * 4. salt 单独保存，数据库不保存明文密码。
 *
 * SQL 中的 ? 是参数占位符，可以避免直接拼接 SQL，
 * 从而降低 SQL 注入风险。
 *
 * username 字段具有 UNIQUE 约束，
 * 如果用户名已经存在，则转换为 NameConflict。
 */
void CloudRepository::registerUser(
    const std::string& username,
    const std::string& password) {
    if (username.size() < 3 || username.size() > 32)
        throw ServiceError(
            ErrorCode::BadRequest,
            "username must contain 3-32 UTF-8 bytes");

    if (password.size() < 8 || password.size() > 128)
        throw ServiceError(
            ErrorCode::BadRequest,
            "password must contain 8-128 UTF-8 bytes");

    std::lock_guard lock(mutex_);

    const auto salt = cloud::common::randomHex(16);
    const auto hash = makePasswordHash(password, salt);

    try {
        Statement insert(
            db_,
            "INSERT INTO users("
            "username,password_hash,salt,quota,storage_used,created_at"
            ") VALUES(?,?,?,?,?,?)");

        insert.text(1, username);
        insert.text(2, hash);
        insert.text(3, salt);
        insert.integer(4, kDefaultQuotaBytes);
        insert.integer(5, 0);
        insert.integer(6, nowMillis());
        insert.step();
    } catch (const ServiceError& e) {
        if (std::string(e.what()).find("UNIQUE") != std::string::npos)
            throw ServiceError(
                ErrorCode::NameConflict,
                "username already exists");

        throw;
    }
}

/**
 * 验证用户登录信息。
 *
 * 参数：
 * - username：用户输入的用户名；
 * - password：用户输入的明文密码。
 *
 * 返回值：
 * - 登录成功：返回用户 ID；
 * - 用户不存在或密码错误：返回 std::nullopt。
 *
 * 验证过程：
 * 1. 根据 username 查询数据库；
 * 2. 读取保存的 password_hash 和 salt；
 * 3. 如果是 PBKDF2 哈希，按保存的迭代次数重新派生；
 * 4. 使用固定时间比较派生值；
 * 5. 如果是旧版 SHA-256 哈希且验证成功，自动升级为 PBKDF2。
 */
std::optional<std::int64_t> CloudRepository::authenticate(
    const std::string& username,
    const std::string& password) {
    std::lock_guard lock(mutex_);

    Statement query(
        db_,
        "SELECT id,password_hash,salt "
        "FROM users "
        "WHERE username=?");

    query.text(1, username);

    if (query.step() != SQLITE_ROW)
        return std::nullopt;

    const auto id = sqlite3_column_int64(query.get(), 0);
    const auto expected = columnText(query.get(), 1);
    const auto salt = columnText(query.get(), 2);

    bool valid = verifyPbkdf2Password(password, salt, expected);

    // 兼容第一版数据库：旧账号仍使用 sha256(salt + password)。
    // 登录成功后立即升级，后续登录不再使用旧算法。
    const bool legacyHash =
        expected.compare(0, std::string_view("pbkdf2_sha256$").size(),
                         "pbkdf2_sha256$") != 0;

    if (!valid && legacyHash) {
        valid = constantTimeEqual(
            cloud::common::sha256Hex(salt + password),
            expected);

        if (valid) {
            Statement upgrade(
                db_,
                "UPDATE users "
                "SET password_hash=? "
                "WHERE id=? AND password_hash=?");

            upgrade.text(1, makePasswordHash(password, salt));
            upgrade.integer(2, id);
            upgrade.text(3, expected);
            upgrade.step();
        }
    }

    return valid
        ? std::optional<std::int64_t>(id)
        : std::nullopt;
}

/**
 * 查询指定目录下的直接子节点。
 *
 * 参数：
 * - userId：当前用户 ID；
 * - parentId：父目录 ID，0 表示根目录。
 *
 * 返回值：
 * - 当前目录下的 NodeInfo 列表。
 *
 * SQL 查询含义：
 * - owner_id=?：只查询当前用户的文件；
 * - parent_id=?：只查询指定目录的直接子项；
 * - is_directory DESC：目录排在文件前面；
 * - name COLLATE NOCASE：按名称进行大小写不敏感排序。
 */
std::vector<NodeInfo> CloudRepository::list(
    std::int64_t userId,
    std::int64_t parentId) {
    std::lock_guard lock(mutex_);

    requireParentDirectory(userId, parentId);

    Statement query(
        db_,
        "SELECT id,parent_id,name,is_directory,size,modified_at "
        "FROM nodes "
        "WHERE owner_id=? AND parent_id=? "
        "ORDER BY is_directory DESC,name COLLATE NOCASE");

    query.integer(1, userId);
    query.integer(2, parentId);

    std::vector<NodeInfo> result;

    while (query.step() == SQLITE_ROW) {
        result.push_back({
            sqlite3_column_int64(query.get(), 0),
            sqlite3_column_int64(query.get(), 1),
            columnText(query.get(), 2),
            sqlite3_column_int(query.get(), 3) != 0,
            sqlite3_column_int64(query.get(), 4),
            sqlite3_column_int64(query.get(), 5)
        });
    }

    return result;
}

/**
 * 创建一个新目录。
 *
 * 参数：
 * - userId：目录所有者；
 * - parentId：父目录 ID，0 表示根目录；
 * - name：新目录名称。
 *
 * 返回值：
 * - 新目录在 nodes 表中的自增 ID。
 *
 * 数据库字段：
 * - is_directory=1：表示目录；
 * - size=0：目录不占用文件内容空间；
 * - blob_id 为 NULL：目录没有对应的文件内容。
 *
 * UNIQUE(owner_id,parent_id,name) 用于防止同一目录下重名。
 */
std::int64_t CloudRepository::mkdir(
    std::int64_t userId,
    std::int64_t parentId,
    const std::string& name) {
    validateName(name);

    std::lock_guard lock(mutex_);

    requireParentDirectory(userId, parentId);

    try {
        Statement insert(
            db_,
            "INSERT INTO nodes("
            "owner_id,parent_id,name,is_directory,size,modified_at"
            ") VALUES(?,?,?,1,0,?)");

        insert.integer(1, userId);
        insert.integer(2, parentId);
        insert.text(3, name);
        insert.integer(4, nowMillis());
        insert.step();

        return sqlite3_last_insert_rowid(db_);
    } catch (const ServiceError& e) {
        if (std::string(e.what()).find("UNIQUE") != std::string::npos)
            throw ServiceError(
                ErrorCode::NameConflict,
                "name already exists in this directory");

        throw;
    }
}

/**
 * 修改文件或目录名称。
 *
 * 参数：
 * - userId：当前用户 ID；
 * - nodeId：需要重命名的节点 ID；
 * - name：新的文件名或目录名。
 *
 * SQL 中的 owner_id=? 很重要：
 * 它可以确保用户不能修改其他用户的节点。
 */
void CloudRepository::renameNode(
    std::int64_t userId,
    std::int64_t nodeId,
    const std::string& name) {
    validateName(name);

    std::lock_guard lock(mutex_);

    try {
        Statement update(
            db_,
            "UPDATE nodes "
            "SET name=?,modified_at=? "
            "WHERE id=? AND owner_id=?");

        update.text(1, name);
        update.integer(2, nowMillis());
        update.integer(3, nodeId);
        update.integer(4, userId);
        update.step();

        if (sqlite3_changes(db_) == 0)
            throw ServiceError(
                ErrorCode::NotFound,
                "node not found");
    } catch (const ServiceError& e) {
        if (std::string(e.what()).find("UNIQUE") != std::string::npos)
            throw ServiceError(
                ErrorCode::NameConflict,
                "name already exists in this directory");

        throw;
    }
}

/**
 * 删除文件或目录。
 *
 * 参数：
 * - userId：当前用户 ID；
 * - nodeId：待删除的文件或目录 ID。
 *
 * 如果 nodeId 指向目录，则递归删除整个子树。
 *
 * 删除流程：
 * 1. 检查节点是否存在且属于用户；
 * 2. 使用 WITH RECURSIVE 找到所有子节点；
 * 3. 收集这些节点引用的 blob；
 * 4. 开启事务；
 * 5. 删除 nodes 记录；
 * 6. 减少 blobs.ref_count；
 * 7. 减少 users.storage_used；
 * 8. 提交事务；
 * 9. 删除不再被引用的物理文件。
 *
 * ref_count 归零后，物理文件才会真正删除。
 */
void CloudRepository::deleteNode(
    std::int64_t userId,
    std::int64_t nodeId) {
    std::lock_guard lock(mutex_);

    Statement exists(
        db_,
        "SELECT 1 "
        "FROM nodes "
        "WHERE id=? AND owner_id=?");

    exists.integer(1, nodeId);
    exists.integer(2, userId);

    if (exists.step() != SQLITE_ROW)
        throw ServiceError(
            ErrorCode::NotFound,
            "node not found");

    std::vector<std::int64_t> blobIds;
    std::int64_t freedBytes = 0;

    Statement blobs(
        db_,
        "WITH RECURSIVE tree(id,blob_id,size) AS ("
        "SELECT id,blob_id,size "
        "FROM nodes "
        "WHERE id=? AND owner_id=? "
        "UNION ALL "
        "SELECT n.id,n.blob_id,n.size "
        "FROM nodes n "
        "JOIN tree t ON n.parent_id=t.id "
        "WHERE n.owner_id=?"
        ") "
        "SELECT blob_id,size "
        "FROM tree "
        "WHERE blob_id IS NOT NULL");

    blobs.integer(1, nodeId);
    blobs.integer(2, userId);
    blobs.integer(3, userId);

    while (blobs.step() == SQLITE_ROW) {
        blobIds.push_back(sqlite3_column_int64(blobs.get(), 0));
        freedBytes += sqlite3_column_int64(blobs.get(), 1);
    }

    exec("BEGIN IMMEDIATE");

    try {
        Statement remove(
            db_,
            "WITH RECURSIVE tree(id) AS ("
            "SELECT id "
            "FROM nodes "
            "WHERE id=? AND owner_id=? "
            "UNION ALL "
            "SELECT n.id "
            "FROM nodes n "
            "JOIN tree t ON n.parent_id=t.id "
            "WHERE n.owner_id=?"
            ") "
            "DELETE FROM nodes "
            "WHERE id IN (SELECT id FROM tree)");

        remove.integer(1, nodeId);
        remove.integer(2, userId);
        remove.integer(3, userId);
        remove.step();

        for (const auto id : blobIds) {
            Statement dec(
                db_,
                "UPDATE blobs "
                "SET ref_count=ref_count-1 "
                "WHERE id=?");

            dec.integer(1, id);
            dec.step();
        }

        if (freedBytes > 0) {
            Statement quc(
                db_,
                "UPDATE users "
                "SET storage_used=MAX(storage_used-?,0) "
                "WHERE id=?");

            quc.integer(1, freedBytes);
            quc.integer(2, userId);
            quc.step();
        }

        exec("COMMIT");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }

    Statement zero(
        db_,
        "SELECT id,relative_path "
        "FROM blobs "
        "WHERE ref_count<=0");

    std::vector<std::pair<std::int64_t, std::string>> unused;

    while (zero.step() == SQLITE_ROW) {
        unused.emplace_back(
            sqlite3_column_int64(zero.get(), 0),
            columnText(zero.get(), 1));
    }

    for (const auto& [id, path] : unused) {
        std::error_code ignored;

        std::filesystem::remove(
            storageRoot_ / path,
            ignored);

        Statement del(
            db_,
            "DELETE FROM blobs "
            "WHERE id=? AND ref_count<=0");

        del.integer(1, id);
        del.step();
    }
}

/**
 * 为一个文件创建一次性分享码。
 *
 * 参数：
 * - userId：文件所有者；
 * - nodeId：要分享的文件节点 ID。
 *
 * 返回值：
 * - 8 个十六进制字符组成的分享码。
 *
 * 分享码：
 * - 有效期为 24 小时；
 * - 只能领取一次；
 * - 只能分享普通文件，不能分享目录；
 * - 数据库中的 claimed_at 为 NULL 表示尚未领取。
 *
 * 如果随机生成的分享码发生碰撞，则最多重试 10 次。
 */
std::string CloudRepository::createShareCode(
    std::int64_t userId,
    std::int64_t nodeId) {
    std::lock_guard lock(mutex_);

    Statement file(
        db_,
        "SELECT 1 "
        "FROM nodes "
        "WHERE id=? AND owner_id=? AND is_directory=0");

    file.integer(1, nodeId);
    file.integer(2, userId);

    if (file.step() != SQLITE_ROW)
        throw ServiceError(
            ErrorCode::NotFound,
            "file not found");

    constexpr auto lifetimeMillis =
        std::int64_t{24} * 60 * 60 * 1000;

    for (int attempt = 0; attempt < 10; ++attempt)
    {
        const auto code = cloud::common::randomHex(4);

        try {
            Statement insert(
                db_,
                "INSERT INTO shares("
                "code,owner_id,node_id,created_at,expires_at"
                ") VALUES(?,?,?,?,?)");

            insert.text(1, code);
            insert.integer(2, userId);
            insert.integer(3, nodeId);
            insert.integer(4, nowMillis());
            insert.integer(5, nowMillis() + lifetimeMillis);
            insert.step();

            return code;
        } catch (const ServiceError& e) {
            if (std::string(e.what()).find("UNIQUE") ==
                std::string::npos) {
                throw;
            }
        }
    }

    throw ServiceError(
        ErrorCode::InternalError,
        "cannot allocate extraction code");
}

/**
 * 使用分享码将文件复制到当前用户的根目录。
 *
 * 参数：
 * - userId：领取分享文件的用户 ID；
 * - code：分享码。
 *
 * 返回值：
 * - 当前用户新建的文件节点 ID。
 *
 * 处理流程：
 * 1. 检查分享码格式；
 * 2. 查询分享码对应的文件；
 * 3. 检查分享码是否过期；
 * 4. 检查分享码是否已经领取；
 * 5. 禁止文件所有者领取自己的分享码；
 * 6. 检查目标用户根目录是否重名；
 * 7. 增加 blob.ref_count；
 * 8. 创建新的 nodes 记录；
 * 9. 增加领取者的已用空间；
 * 10. 原子地标记分享码为已领取；
 * 11. 提交事务。
 *
 * 注意：
 * - 该操作不会复制物理文件；
 * - 只是让新的 nodes 记录引用同一个 blob；
 * - 数据库事务保证引用计数、文件节点、配额和分享码状态同步成功。
 */
std::int64_t CloudRepository::claimShareCode(
    std::int64_t userId,
    const std::string& code) {
    if (code.size() != 8 ||
        !std::all_of(
            code.begin(),
            code.end(),
            [](unsigned char c) {
                return std::isxdigit(c) != 0;
            })) {
        throw ServiceError(
            ErrorCode::BadRequest,
            "extraction code must contain 8 hexadecimal characters");
    }

    std::lock_guard lock(mutex_);

    Statement share(
        db_,
        R"sql(
SELECT s.owner_id,n.name,n.blob_id,n.size
FROM shares s
JOIN nodes n ON n.id=s.node_id
WHERE s.code=?
  AND s.claimed_at IS NULL
  AND s.expires_at>=?
  AND n.is_directory=0
)sql");

    share.text(1, code);
    share.integer(2, nowMillis());

    if (share.step() != SQLITE_ROW)
        throw ServiceError(
            ErrorCode::NotFound,
            "extraction code is invalid, expired, or already used");

    const auto ownerId = sqlite3_column_int64(share.get(), 0);
    const auto name = columnText(share.get(), 1);
    const auto blobId = sqlite3_column_int64(share.get(), 2);
    const auto size = sqlite3_column_int64(share.get(), 3);

    if (ownerId == userId)
        throw ServiceError(
            ErrorCode::Forbidden,
            "the owner cannot claim their own extraction code");

    Statement conflict(
        db_,
        "SELECT 1 "
        "FROM nodes "
        "WHERE owner_id=? AND parent_id=0 AND name=?");

    conflict.integer(1, userId);
    conflict.text(2, name);

    if (conflict.step() == SQLITE_ROW)
        throw ServiceError(
            ErrorCode::NameConflict,
            "a file with this name already exists in the root directory");

    exec("BEGIN IMMEDIATE");

    try {
        Statement increment(
            db_,
            "UPDATE blobs "
            "SET ref_count=ref_count+1 "
            "WHERE id=?");

        increment.integer(1, blobId);
        increment.step();

        Statement add(
            db_,
            "INSERT INTO nodes("
            "owner_id,parent_id,name,is_directory,blob_id,size,modified_at"
            ") VALUES(?,0,?,0,?,?,?)");

        add.integer(1, userId);
        add.text(2, name);
        add.integer(3, blobId);
        add.integer(4, size);
        add.integer(5, nowMillis());
        add.step();

        const auto nodeId = sqlite3_last_insert_rowid(db_);

        // 分享文件虽然复用了物理 blob，但对领取者来说仍然新增了一个
        // 文件节点，因此按照本项目的逻辑配额规则增加其已用空间。
        Statement qinc(
            db_,
            "UPDATE users "
            "SET storage_used=storage_used+? "
            "WHERE id=? "
            "AND (quota<=0 OR storage_used+?<=quota)");

        qinc.integer(1, size);
        qinc.integer(2, userId);
        qinc.integer(3, size);
        qinc.step();

        if (sqlite3_changes(db_) == 0)
            throw ServiceError(
                ErrorCode::QuotaExceeded,
                "storage quota exceeded");

        Statement claim(
            db_,
            "UPDATE shares "
            "SET claimed_at=? "
            "WHERE code=? AND claimed_at IS NULL");

        claim.integer(1, nowMillis());
        claim.text(2, code);
        claim.step();

        // WHERE claimed_at IS NULL 使领取操作具有并发保护。
        // 如果分享码已被其他请求领取，当前更新影响行数为 0，
        // 整个事务回滚，避免重复创建节点和增加配额。
        if (sqlite3_changes(db_) != 1)
            throw ServiceError(
                ErrorCode::NotFound,
                "extraction code was already claimed");

        exec("COMMIT");
        return nodeId;
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}

/**
 * 根据文件 SHA-256 计算物理存储路径。
 *
 * 参数：
 * - sha：文件的 SHA-256 哈希值。
 *
 * 返回路径示例：
 *
 *     blobs/ab/cd/abcdef......
 *
 * 前两位和接下来两位被用作目录名，
 * 可以避免所有文件都堆放在同一个目录中。
 */
std::filesystem::path CloudRepository::blobPath(
    const std::string& sha) const {
    return storageRoot_
        / "blobs"
        / sha.substr(0, 2)
        / sha.substr(2, 2)
        / sha;
}

/**
 * 检查数据库表是否包含指定列。
 *
 * 参数：
 * - table：表名；
 * - column：列名；
 * - ddl：如果列不存在，用于 ALTER TABLE 的字段定义。
 *
 * 执行流程：
 * 1. 执行 PRAGMA table_info(table)；
 * 2. 遍历查询结果；
 * 3. 如果找到目标列，直接返回；
 * 4. 如果没有找到，执行 ALTER TABLE ADD COLUMN。
 *
 * 用途：
 * - 兼容旧版本数据库；
 * - 为旧表增加 quota、storage_used 等新字段。
 */
void CloudRepository::ensureColumn(
    const std::string& table,
    const std::string& column,
    const std::string& ddl) {
    Statement info(
        db_,
        ("PRAGMA table_info(" + table + ")").c_str());

    while (info.step() == SQLITE_ROW) {
        // PRAGMA table_info 的第 1 列是字段名称。
        if (columnText(info.get(), 1) == column)
            return;
    }

    exec(
        "ALTER TABLE " + table +
        " ADD COLUMN " + column + " " + ddl);
}

/**
 * 检查用户剩余存储空间是否足够。
 *
 * 参数：
 * - userId：用户 ID；
 * - extraBytes：本次操作计划增加的字节数。
 *
 * 检查逻辑：
 *
 *     storage_used + extraBytes <= quota
 *
 * quota 小于等于 0 时表示不限制配额。
 */
void CloudRepository::ensureQuota(
    std::int64_t userId,
    std::int64_t extraBytes) {
    Statement q(
        db_,
        "SELECT storage_used, quota "
        "FROM users "
        "WHERE id=?");

    q.integer(1, userId);

    if (q.step() != SQLITE_ROW)
        throw ServiceError(
            ErrorCode::NotFound,
            "user not found");

    const auto used = sqlite3_column_int64(q.get(), 0);
    const auto quota = sqlite3_column_int64(q.get(), 1);

    // 先检查数据库中的计数和输入是否为非负数，再检查加法是否会溢出。
    // quota <= 0 仍表示不限制配额，但 used 和 extraBytes 不能为负数。
    if (used < 0 ||
        extraBytes < 0 ||
        extraBytes > std::numeric_limits<std::int64_t>::max() - used ||
        (quota > 0 && used + extraBytes > quota))
        throw ServiceError(
            ErrorCode::QuotaExceeded,
            "storage quota exceeded");
}

/**
 * 清理已经过期的上传会话。
 *
 * 作用：
 * 1. 查询 expires_at 小于当前时间的上传会话；
 * 2. 从内存 uploads_ 中移除；
 * 3. 删除对应的临时 .part 文件；
 * 4. 删除 upload_session 数据库记录。
 *
 * 该函数通常由后台定时任务周期性调用。
 */
void CloudRepository::cleanupExpiredUploads() {
    std::lock_guard lock(mutex_);

    const auto now = nowMillis();
    std::vector<std::string> ids;
    std::vector<std::string> paths;

    Statement sel(
        db_,
        "SELECT transfer_id,temp_path "
        "FROM upload_session "
        "WHERE expires_at<>0 AND expires_at<?");

    sel.integer(1, now);

    while (sel.step() == SQLITE_ROW) {
        ids.push_back(columnText(sel.get(), 0));
        paths.push_back(columnText(sel.get(), 1));
    }

    for (std::size_t i = 0; i < ids.size(); ++i) {
        uploads_.erase(ids[i]);

        std::error_code ignored;

        std::filesystem::remove(
            std::filesystem::path(paths[i]),
            ignored);

        Statement del(
            db_,
            "DELETE FROM upload_session "
            "WHERE transfer_id=?");

        del.text(1, ids[i]);
        del.step();
    }
}

/**
 * 获取用户指定文件的实际存储信息。
 *
 * 参数：
 * - userId：当前用户 ID；
 * - nodeId：要获取的文件节点 ID。
 *
 * 返回值：
 * - StoredFileInfo，通常包含：
 *   - 父目录 ID；
 *   - 文件名；
 *   - 物理文件路径；
 *   - 文件大小。
 *
 * 该函数只允许读取：
 * - 属于当前用户的节点；
 * - 普通文件，而不是目录。
 */
StoredFileInfo CloudRepository::getStoredFile(
    std::int64_t userId,
    std::int64_t nodeId) {
    std::lock_guard lock(mutex_);

    Statement query(
        db_,
        "SELECT n.parent_id,n.name,b.relative_path,n.size "
        "FROM nodes n "
        "JOIN blobs b ON n.blob_id=b.id "
        "WHERE n.id=? "
        "AND n.owner_id=? "
        "AND n.is_directory=0");

    query.integer(1, nodeId);
    query.integer(2, userId);

    if (query.step() != SQLITE_ROW)
        throw ServiceError(
            ErrorCode::NotFound,
            "file not found");

    return {
        sqlite3_column_int64(query.get(), 0),
        columnText(query.get(), 1),
        storageRoot_ / columnText(query.get(), 2),
        sqlite3_column_int64(query.get(), 3)
    };
}

/**
 * 将服务器本地已有文件导入云盘。
 *
 * 参数：
 * - userId：文件所有者；
 * - parentId：目标父目录 ID；
 * - name：导入后的文件名；
 * - localPath：服务器本地源文件路径。
 *
 * 执行流程：
 * 1. 获取本地文件大小；
 * 2. 计算本地文件 SHA-256；
 * 3. 调用 beginUpload() 创建上传会话；
 * 4. 如果相同文件已经存在，直接返回节点 ID；
 * 5. 否则按 256 KiB 分块读取本地文件；
 * 6. 调用 appendUpload() 写入临时文件；
 * 7. 调用 finishUpload() 完成上传；
 * 8. 如果过程中失败，删除上传状态和临时文件。
 */
std::int64_t CloudRepository::importLocalFile(
    std::int64_t userId,
    std::int64_t parentId,
    const std::string& name,
    const std::filesystem::path& localPath) {
    std::error_code sizeError;

    const auto rawSize =
        std::filesystem::file_size(localPath, sizeError);

    if (sizeError ||
        rawSize > static_cast<std::uintmax_t>(
            std::numeric_limits<std::int64_t>::max())) {
        throw ServiceError(
            ErrorCode::IoError,
            "cannot read converted file size");
    }

    const auto size = static_cast<std::int64_t>(rawSize);
    const auto sha = cloud::common::sha256File(localPath);

    const auto init =
        beginUpload(userId, parentId, name, size, sha);

    // 如果数据库中已经存在相同内容，
    // beginUpload() 会直接创建节点，无需再次读取文件。
    if (init.instant)
        return init.nodeId;

    try {
        std::ifstream input(localPath, std::ios::binary);

        if (!input)
            throw ServiceError(
                ErrorCode::IoError,
                "cannot open converted file");

        std::vector<std::uint8_t> block(256U * 1024U);
        std::int64_t offset = init.received;

        // 如果是断点续传，从已经接收的位置开始读取。
        input.seekg(offset);

        if (!input)
            throw ServiceError(
                ErrorCode::IoError,
                "cannot resume converted file");

        while (input) {
            input.read(
                reinterpret_cast<char*>(block.data()),
                static_cast<std::streamsize>(block.size()));

            const auto count =
                static_cast<std::size_t>(input.gcount());

            if (count == 0)
                break;

            offset = appendUpload(
                userId,
                init.transferId,
                offset,
                block.data(),
                count);
        }

        if (offset != size)
            throw ServiceError(
                ErrorCode::IoError,
                "cannot read complete converted file");

        return finishUpload(userId, init.transferId);
    } catch (...) {
        std::lock_guard lock(mutex_);

        const auto it = uploads_.find(init.transferId);

        if (it != uploads_.end()) {
            std::error_code ignored;

            std::filesystem::remove(
                it->second.tempPath,
                ignored);

            uploads_.erase(it);
        }

        Statement drop(
            db_,
            "DELETE FROM upload_session "
            "WHERE transfer_id=?");

        drop.text(1, init.transferId);
        drop.step();

        throw;
    }
}

/**
 * 开始一次上传或断点续传。
 *
 * 参数：
 * - userId：上传者 ID；
 * - parentId：目标目录 ID；
 * - name：文件名；
 * - size：文件总大小，单位为字节；
 * - sha：完整文件的 SHA-256 哈希。
 *
 * 返回值：
 * - UploadInitResult。
 *
 * 主要情况：
 *
 * 1. 相同文件已经存在：
 *    - 不重新上传；
 *    - 直接增加 blob 引用；
 *    - 直接创建新的 nodes 记录。
 *
 * 2. 存在相同文件的未完成上传：
 *    - 返回原来的 transferId；
 *    - 返回已经接收的字节数；
 *    - 从断点继续上传。
 *
 * 3. 没有任何旧记录：
 *    - 创建新的 .part 临时文件；
 *    - 创建新的 upload_session 记录。
 */
UploadInitResult CloudRepository::beginUpload(
    std::int64_t userId,
    std::int64_t parentId,
    const std::string& name,
    std::int64_t size,
    const std::string& sha) {
    validateName(name);
    ensureSha(sha);

    if (size < 0)
        throw ServiceError(
            ErrorCode::BadRequest,
            "negative file size");

    std::lock_guard lock(mutex_);

    ensureQuota(userId, size);
    requireParentDirectory(userId, parentId);

    Statement conflict(
        db_,
        "SELECT 1 "
        "FROM nodes "
        "WHERE owner_id=? AND parent_id=? AND name=?");

    conflict.integer(1, userId);
    conflict.integer(2, parentId);
    conflict.text(3, name);

    if (conflict.step() == SQLITE_ROW)
        throw ServiceError(
            ErrorCode::NameConflict,
            "name already exists in this directory");

    // 先检查内容是否已经存在。
    // 通过 SHA-256 和文件大小判断内容是否相同。
    Statement existing(
        db_,
        "SELECT id "
        "FROM blobs "
        "WHERE sha256=? AND size=?");

    existing.text(1, sha);
    existing.integer(2, size);

    if (existing.step() == SQLITE_ROW) {
        const auto blobId =
            sqlite3_column_int64(existing.get(), 0);

        exec("BEGIN IMMEDIATE");

        try {
            Statement inc(
                db_,
                "UPDATE blobs "
                "SET ref_count=ref_count+1 "
                "WHERE id=?");

            inc.integer(1, blobId);
            inc.step();

            Statement add(
                db_,
                "INSERT INTO nodes("
                "owner_id,parent_id,name,is_directory,"
                "blob_id,size,modified_at"
                ") VALUES(?,?,?,0,?,?,?)");

            add.integer(1, userId);
            add.integer(2, parentId);
            add.text(3, name);
            add.integer(4, blobId);
            add.integer(5, size);
            add.integer(6, nowMillis());
            add.step();

            Statement qinc(
                db_,
                "UPDATE users "
                "SET storage_used=storage_used+? "
                "WHERE id=? "
                "AND storage_used+?<=quota");

            qinc.integer(1, size);
            qinc.integer(2, userId);
            qinc.integer(3, size);
            qinc.step();

            if (sqlite3_changes(db_) == 0)
                throw ServiceError(
                    ErrorCode::QuotaExceeded,
                    "storage quota exceeded");

            const auto nodeId =
                sqlite3_last_insert_rowid(db_);

            exec("COMMIT");

            return {"", true, nodeId};
        } catch (...) {
            exec("ROLLBACK");
            throw;
        }
    }

    // 检查是否存在可以复用的断点续传会话。
    {
        Statement resume(
            db_,
            "SELECT transfer_id,received,temp_path "
            "FROM upload_session "
            "WHERE user_id=? "
            "AND parent_id=? "
            "AND name=? "
            "AND sha256=? "
            "AND expected_size=? "
            "AND received<expected_size "
            "LIMIT 1");

        resume.integer(1, userId);
        resume.integer(2, parentId);
        resume.text(3, name);
        resume.text(4, sha);
        resume.integer(5, size);

        if (resume.step() == SQLITE_ROW) {
            const auto tid =
                columnText(resume.get(), 0);

            auto received =
                sqlite3_column_int64(resume.get(), 1);

            auto tmp =
                std::filesystem::path(
                    columnText(resume.get(), 2));

            // 如果临时文件已经不存在，则旧进度失效，
            // 重新创建空文件并从零开始。
            if (!std::filesystem::exists(tmp)) {
                std::ofstream create(
                    tmp,
                    std::ios::binary | std::ios::trunc);

                if (!create)
                    throw ServiceError(
                        ErrorCode::IoError,
                        "cannot recreate upload temp file");

                received = 0;
            }

            uploads_[tid] = {
                userId,
                parentId,
                name,
                sha,
                size,
                received,
                tmp
            };

            Statement touch(
                db_,
                "UPDATE upload_session "
                "SET expires_at=? "
                "WHERE transfer_id=?");

            touch.integer(
                1,
                nowMillis() + kUploadSessionTtlMillis);

            touch.text(2, tid);
            touch.step();

            return {tid, false, 0, received};
        }
    }

    // 没有已存在的文件，也没有可恢复的上传会话，
    // 创建一个全新的上传任务。
    const auto id = cloud::common::randomHex(16);
    const auto path =
        storageRoot_ / "temp" / (id + ".part");

    {
        std::ofstream create(
            path,
            std::ios::binary | std::ios::trunc);

        if (!create)
            throw ServiceError(
                ErrorCode::IoError,
                "cannot create upload temp file");
    }

    uploads_[id] = {
        userId,
        parentId,
        name,
        sha,
        size,
        0,
        path
    };

    Statement insert(
        db_,
        "INSERT INTO upload_session("
        "transfer_id,user_id,parent_id,name,sha256,"
        "expected_size,received,temp_path,created_at,expires_at"
        ") VALUES(?,?,?,?,?,?,?,?,?,?)");

    insert.text(1, id);
    insert.integer(2, userId);
    insert.integer(3, parentId);
    insert.text(4, name);
    insert.text(5, sha);
    insert.integer(6, size);
    insert.integer(7, 0);
    insert.text(8, path.generic_string());
    insert.integer(9, nowMillis());
    insert.integer(
        10,
        nowMillis() + kUploadSessionTtlMillis);
    insert.step();

    return {id, false, 0, 0};
}

/**
 * 向上传临时文件追加一块数据。
 *
 * 参数：
 * - userId：上传用户 ID；
 * - id：上传会话 ID；
 * - offset：客户端认为本次数据的起始偏移；
 * - data：待写入数据的指针；
 * - size：本次数据长度。
 *
 * 返回值：
 * - 写入完成后的新偏移量。
 *
 * 处理流程：
 * 1. 检查上传会话是否存在；
 * 2. 检查上传会话是否属于当前用户；
 * 3. 检查 offset 是否等于服务器当前进度；
 * 4. 检查写入后是否超过声明的文件大小；
 * 5. 追加写入临时文件；
 * 6. 更新内存中的 received；
 * 7. 更新数据库中的 received 和 expires_at。
 *
 * offset 校验可以防止：
 * - 重复写入；
 * - 数据乱序；
 * - 网络重试导致文件内容错位。
 */
std::int64_t CloudRepository::appendUpload(std::int64_t userId, const std::string& id,
                                           std::int64_t offset, const std::uint8_t* data,
                                           std::size_t size) {
    std::shared_ptr<std::mutex> transferMutex;
    {
        std::lock_guard lock(mutex_);
        const auto it=uploads_.find(id);
        if(it==uploads_.end()||it->second.userId!=userId)
            throw ServiceError(ErrorCode::TransferExpired,"upload transfer not found");
        transferMutex=it->second.ioMutex;
    }
    std::lock_guard transferLock(*transferMutex);

    UploadState snapshot;
    {
        std::lock_guard lock(mutex_);
        const auto it=uploads_.find(id);
        if(it==uploads_.end()||it->second.userId!=userId||it->second.ioMutex!=transferMutex)
            throw ServiceError(ErrorCode::TransferExpired,"upload transfer not found");
        snapshot=it->second;
        if(offset!=snapshot.received) throw ServiceError(ErrorCode::BadRequest,"unexpected upload offset");
        if(snapshot.received+static_cast<std::int64_t>(size)>snapshot.expectedSize)
            throw ServiceError(ErrorCode::BadRequest,"upload exceeds declared size");
    }

    // Disk I/O is intentionally outside the repository-wide SQLite/map lock.
    std::ofstream out(snapshot.tempPath,std::ios::binary|std::ios::app);
    out.write(reinterpret_cast<const char*>(data),static_cast<std::streamsize>(size));
    if(!out) throw ServiceError(ErrorCode::IoError,"cannot write upload block");
    out.close();

    std::lock_guard lock(mutex_);
    const auto it=uploads_.find(id);
    if(it==uploads_.end()||it->second.userId!=userId||it->second.ioMutex!=transferMutex)
        throw ServiceError(ErrorCode::TransferExpired,"upload transfer not found");
    auto& state=it->second;
    if(state.received!=offset)
        throw ServiceError(ErrorCode::BadRequest,"unexpected upload offset");
    state.received+=static_cast<std::int64_t>(size);
    Statement progress(db_,"UPDATE upload_session SET received=?, expires_at=? WHERE transfer_id=?");
    progress.integer(1,state.received); progress.integer(2,nowMillis()+kUploadSessionTtlMillis); progress.text(3,id); progress.step();
    return state.received;
}

/**
 * 完成一次上传。
 *
 * 参数：
 * - userId：上传用户 ID；
 * - id：上传会话 ID。
 *
 * 返回值：
 * - 新建文件节点的 ID。
 *
 * 处理流程：
 * 1. 查询并验证上传会话；
 * 2. 检查实际接收大小是否等于预期大小；
 * 3. 重新计算临时文件的 SHA-256；
 * 4. 校验文件内容是否被篡改；
 * 5. 如果 blob 已存在，则删除临时文件并复用 blob；
 * 6. 如果 blob 不存在，则把临时文件移动到 blobs 目录；
 * 7. 在 blobs 表中插入文件记录；
 * 8. 在事务中增加 blob 引用计数；
 * 9. 创建 nodes 文件节点；
 * 10. 增加用户已使用空间；
 * 11. 提交事务并删除上传会话记录。
 *
 * 如果数据库提交阶段失败，会回滚数据库操作，并删除已经移动到
 * blobs 目录但尚未被节点引用的物理文件，避免产生孤儿文件。
 */
std::int64_t CloudRepository::finishUpload(std::int64_t userId, const std::string& id) {
    std::shared_ptr<std::mutex> transferMutex;
    {
        std::lock_guard lock(mutex_);
        const auto it=uploads_.find(id);
        if(it==uploads_.end()||it->second.userId!=userId)
            throw ServiceError(ErrorCode::TransferExpired,"upload transfer not found");
        transferMutex=it->second.ioMutex;
    }
    std::lock_guard transferLock(*transferMutex);
    UploadState state;
    {
        std::lock_guard lock(mutex_);
        const auto it=uploads_.find(id);
        if(it==uploads_.end()||it->second.userId!=userId||it->second.ioMutex!=transferMutex)
            throw ServiceError(ErrorCode::TransferExpired,"upload transfer not found");
        state=it->second;
        if(state.received!=state.expectedSize)
            throw ServiceError(ErrorCode::BadRequest,"upload is incomplete");
    }

    // SHA-256 can be expensive for large files; other repository operations
    // continue while this transfer is verified.
    if(cloud::common::sha256File(state.tempPath)!=state.sha256) {
        std::filesystem::remove(state.tempPath);
        std::lock_guard lock(mutex_);
        const auto it=uploads_.find(id);
        if(it==uploads_.end()||it->second.ioMutex!=transferMutex)
            throw ServiceError(ErrorCode::TransferExpired,"upload transfer not found");
        uploads_.erase(it);
        Statement drop(db_,"DELETE FROM upload_session WHERE transfer_id=?"); drop.text(1,id); drop.step();
        throw ServiceError(ErrorCode::HashMismatch,"uploaded file hash does not match");
    }
    std::lock_guard lock(mutex_);
    const auto it=uploads_.find(id);
    if(it==uploads_.end()||it->second.userId!=userId||it->second.ioMutex!=transferMutex)
        throw ServiceError(ErrorCode::TransferExpired,"upload transfer not found");
    const auto finalPath = blobPath(state.sha256);

    std::filesystem::create_directories(
        finalPath.parent_path());

    std::int64_t blobId = 0;

    // 检查是否在上传完成前已经有其他任务创建了相同 blob。
    Statement existing(
        db_,
        "SELECT id "
        "FROM blobs "
        "WHERE sha256=? AND size=?");

    existing.text(1, state.sha256);
    existing.integer(2, state.expectedSize);

    if (existing.step() == SQLITE_ROW) {
        blobId = sqlite3_column_int64(existing.get(), 0);

        // 物理内容已经存在，临时文件可以删除。
        std::filesystem::remove(state.tempPath);
    } else {
        // 将临时文件移动到正式 blob 路径。
        std::error_code error;

        std::filesystem::rename(
            state.tempPath,
            finalPath,
            error);

        if (error)
            throw ServiceError(
                ErrorCode::IoError,
                "cannot commit uploaded file: " +
                error.message());

        const auto relative =
            std::filesystem::relative(
                finalPath,
                storageRoot_).generic_string();

        Statement addBlob(
            db_,
            "INSERT INTO blobs("
            "sha256,size,relative_path,ref_count,created_at"
            ") VALUES(?,?,?,0,?)");

        addBlob.text(1, state.sha256);
        addBlob.integer(2, state.expectedSize);
        addBlob.text(3, relative);
        addBlob.integer(4, nowMillis());
        addBlob.step();

        blobId = sqlite3_last_insert_rowid(db_);
    }

    exec("BEGIN IMMEDIATE");

    try {
        Statement inc(
            db_,
            "UPDATE blobs "
            "SET ref_count=ref_count+1 "
            "WHERE id=?");

        inc.integer(1, blobId);
        inc.step();

        Statement add(
            db_,
            "INSERT INTO nodes("
            "owner_id,parent_id,name,is_directory,"
            "blob_id,size,modified_at"
            ") VALUES(?,?,?,0,?,?,?)");

        add.integer(1, state.userId);
        add.integer(2, state.parentId);
        add.text(3, state.name);
        add.integer(4, blobId);
        add.integer(5, state.expectedSize);
        add.integer(6, nowMillis());
        add.step();

        Statement qinc(
            db_,
            "UPDATE users "
            "SET storage_used=storage_used+? "
            "WHERE id=? "
            "AND storage_used+?<=quota");

        qinc.integer(1, state.expectedSize);
        qinc.integer(2, state.userId);
        qinc.integer(3, state.expectedSize);
        qinc.step();

        if (sqlite3_changes(db_) == 0)
            throw ServiceError(
                ErrorCode::QuotaExceeded,
                "storage quota exceeded");

        const auto nodeId =
            sqlite3_last_insert_rowid(db_);

        exec("COMMIT");

        uploads_.erase(it);

        Statement cleanup(
            db_,
            "DELETE FROM upload_session "
            "WHERE transfer_id=?");

        cleanup.text(1, id);
        cleanup.step();

        return nodeId;
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}

/**
 * 开始下载一个文件。
 *
 * 参数：
 * - userId：下载用户 ID；
 * - nodeId：需要下载的文件节点 ID。
 *
 * 返回值：
 * - DownloadInfo，包含：
 *   - 下载传输 ID；
 *   - 文件名；
 *   - 文件 SHA-256；
 *   - 文件大小。
 *
 * 同时会在 downloads_ 内存表中保存下载状态，
 * 后续 readDownload() 根据传输 ID 分块读取文件。
 */
DownloadInfo CloudRepository::beginDownload(
    std::int64_t userId,
    std::int64_t nodeId) {
    std::lock_guard lock(mutex_);

    Statement q(
        db_,
        "SELECT n.name,n.size,b.sha256,b.relative_path "
        "FROM nodes n "
        "JOIN blobs b ON n.blob_id=b.id "
        "WHERE n.id=? "
        "AND n.owner_id=? "
        "AND n.is_directory=0");

    q.integer(1, nodeId);
    q.integer(2, userId);

    if (q.step() != SQLITE_ROW)
        throw ServiceError(
            ErrorCode::NotFound,
            "file not found");

    const auto id = cloud::common::randomHex(16);

    DownloadInfo info{
        id,
        columnText(q.get(), 0),
        columnText(q.get(), 2),
        sqlite3_column_int64(q.get(), 1)
    };

    downloads_[id] = {
        userId,
        storageRoot_ / columnText(q.get(), 3),
        info.size
    };

    return info;
}

/**
 * 读取下载文件的一段数据。
 *
 * 参数：
 * - userId：当前下载用户 ID；
 * - id：beginDownload() 返回的下载传输 ID；
 * - offset：本次读取的起始偏移量；
 * - maxBytes：本次最多读取的字节数；
 * - final：输出参数，函数返回时表示是否已经读到文件末尾。
 *
 * 返回值：
 * - 读取到的二进制数据。
 *
 * 处理流程：
 * 1. 验证下载会话存在且属于当前用户；
 * 2. 验证 offset 位于合法范围；
 * 3. 计算本次实际读取长度；
 * 4. 打开物理文件；
 * 5. 跳转到 offset；
 * 6. 读取数据；
 * 7. 判断是否到达文件末尾；
 * 8. 如果读取完成，删除 downloads_ 中的会话。
 *
 * 采用分块读取，避免一次性把大文件全部加载到内存。
 */
std::vector<std::uint8_t> CloudRepository::readDownload(std::int64_t userId,
                                                        const std::string& id,
                                                        std::int64_t offset,
                                                        std::size_t maxBytes,
                                                        bool& final) {
    DownloadState state;
    {
        std::lock_guard lock(mutex_);
        const auto it=downloads_.find(id);
        if(it==downloads_.end()||it->second.userId!=userId)
            throw ServiceError(ErrorCode::TransferExpired,"download transfer not found");
        state=it->second;
    }
    if(offset<0||offset>state.size) throw ServiceError(ErrorCode::BadRequest,"invalid download offset");
    const auto count=static_cast<std::size_t>(std::min<std::int64_t>(static_cast<std::int64_t>(maxBytes),state.size-offset));
    std::vector<std::uint8_t> bytes(count);
    // Blob reads are independent and must not serialize metadata operations.
    std::ifstream in(state.blobPath,std::ios::binary);
    in.seekg(offset); in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(static_cast<std::size_t>(in.gcount())!=bytes.size()) throw ServiceError(ErrorCode::IoError,"cannot read stored blob");
    final=offset+static_cast<std::int64_t>(bytes.size())==state.size;
    if(final) {
        std::lock_guard lock(mutex_);
        const auto it=downloads_.find(id);
        if(it!=downloads_.end()&&it->second.userId==userId) downloads_.erase(it);
    }

    return bytes;
}

} // namespace cloud::server
