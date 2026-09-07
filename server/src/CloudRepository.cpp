// 负责人：成员2：服务端存储
#include "cloud/server/CloudRepository.h"
#include "cloud/server/ServiceError.h"
#include "cloud/common/ErrorCode.h"
#include "cloud/common/Sha256.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <limits>
#include <stdexcept>

namespace cloud::server {
namespace {

using cloud::common::ErrorCode;

std::int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr) != SQLITE_OK)
            throw ServiceError(ErrorCode::DbError, sqlite3_errmsg(db));
    }
    ~Statement() { if (statement_) sqlite3_finalize(statement_); }
    sqlite3_stmt* get() const { return statement_; }
    void text(int index, const std::string& value) {
        if (sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw ServiceError(ErrorCode::DbError, sqlite3_errmsg(db_));
    }
    void integer(int index, std::int64_t value) {
        if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK)
            throw ServiceError(ErrorCode::DbError, sqlite3_errmsg(db_));
    }
    int step() {
        const int result = sqlite3_step(statement_);
        if (result != SQLITE_ROW && result != SQLITE_DONE)
            throw ServiceError(ErrorCode::DbError, sqlite3_errmsg(db_));
        return result;
    }
private:
    sqlite3* db_{};
    sqlite3_stmt* statement_{};
};

std::string columnText(sqlite3_stmt* s, int column) {
    const auto* value = sqlite3_column_text(s, column);
    return value ? reinterpret_cast<const char*>(value) : std::string{};
}

void ensureSha(const std::string& sha) {
    if (sha.size() != 64 || !std::all_of(sha.begin(), sha.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) throw ServiceError(ErrorCode::BadRequest, "invalid SHA-256 value");
}

} // namespace

CloudRepository::CloudRepository(std::filesystem::path databasePath,
                                 std::filesystem::path storageRoot)
    : storageRoot_(std::move(storageRoot)) {
    std::filesystem::create_directories(databasePath.parent_path());
    std::filesystem::create_directories(storageRoot_ / "temp");
    std::filesystem::create_directories(storageRoot_ / "blobs");
    if (sqlite3_open(databasePath.string().c_str(), &db_) != SQLITE_OK) {
        const std::string error = db_ ? sqlite3_errmsg(db_) : "cannot open SQLite database";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw ServiceError(ErrorCode::DbError, error);
    }
    migrate();
}

CloudRepository::~CloudRepository() { if (db_) sqlite3_close(db_); }

void CloudRepository::exec(const std::string& sql) {
    char* error = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db_);
        if (error) sqlite3_free(error);
        throw ServiceError(ErrorCode::DbError, message);
    }
}

void CloudRepository::migrate() {
    exec("PRAGMA foreign_keys=ON;");
    exec("PRAGMA journal_mode=WAL;");
    exec(R"sql(
CREATE TABLE IF NOT EXISTS users(
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 username TEXT NOT NULL UNIQUE,
 password_hash TEXT NOT NULL,
 salt TEXT NOT NULL,
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
CREATE INDEX IF NOT EXISTS idx_nodes_parent ON nodes(owner_id,parent_id);
PRAGMA user_version=1;
)sql");
}

void CloudRepository::validateName(const std::string& name) const {
    if (name.empty() || name.size() > 255 || name == "." || name == ".." ||
        name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20; }))
        throw ServiceError(ErrorCode::BadRequest, "invalid file or directory name");
}

void CloudRepository::requireParentDirectory(std::int64_t userId, std::int64_t parentId) {
    if (parentId == 0) return;
    Statement query(db_, "SELECT is_directory FROM nodes WHERE id=? AND owner_id=?");
    query.integer(1, parentId); query.integer(2, userId);
    if (query.step() != SQLITE_ROW) throw ServiceError(ErrorCode::NotFound, "parent directory not found");
    if (!sqlite3_column_int(query.get(), 0)) throw ServiceError(ErrorCode::BadRequest, "parent is not a directory");
}

void CloudRepository::registerUser(const std::string& username, const std::string& password) {
    if (username.size() < 3 || username.size() > 32)
        throw ServiceError(ErrorCode::BadRequest, "username must contain 3-32 UTF-8 bytes");
    if (password.size() < 8 || password.size() > 128)
        throw ServiceError(ErrorCode::BadRequest, "password must contain 8-128 UTF-8 bytes");
    std::lock_guard lock(mutex_);
    const auto salt = cloud::common::randomHex(16);
    const auto hash = cloud::common::sha256Hex(salt + password);
    try {
        Statement insert(db_, "INSERT INTO users(username,password_hash,salt,created_at) VALUES(?,?,?,?)");
        insert.text(1, username); insert.text(2, hash); insert.text(3, salt); insert.integer(4, nowMillis());
        insert.step();
    } catch (const ServiceError& e) {
        if (std::string(e.what()).find("UNIQUE") != std::string::npos)
            throw ServiceError(ErrorCode::NameConflict, "username already exists");
        throw;
    }
}

std::optional<std::int64_t> CloudRepository::authenticate(const std::string& username,
                                                           const std::string& password) {
    std::lock_guard lock(mutex_);
    Statement query(db_, "SELECT id,password_hash,salt FROM users WHERE username=?");
    query.text(1, username);
    if (query.step() != SQLITE_ROW) return std::nullopt;
    const auto id = sqlite3_column_int64(query.get(), 0);
    const auto expected = columnText(query.get(), 1);
    const auto salt = columnText(query.get(), 2);
    return cloud::common::sha256Hex(salt + password) == expected
        ? std::optional<std::int64_t>(id) : std::nullopt;
}

std::vector<NodeInfo> CloudRepository::list(std::int64_t userId, std::int64_t parentId) {
    std::lock_guard lock(mutex_);
    requireParentDirectory(userId, parentId);
    Statement query(db_, "SELECT id,parent_id,name,is_directory,size,modified_at FROM nodes WHERE owner_id=? AND parent_id=? ORDER BY is_directory DESC,name COLLATE NOCASE");
    query.integer(1, userId); query.integer(2, parentId);
    std::vector<NodeInfo> result;
    while (query.step() == SQLITE_ROW) {
        result.push_back({sqlite3_column_int64(query.get(),0), sqlite3_column_int64(query.get(),1),
                          columnText(query.get(),2), sqlite3_column_int(query.get(),3)!=0,
                          sqlite3_column_int64(query.get(),4), sqlite3_column_int64(query.get(),5)});
    }
    return result;
}

std::int64_t CloudRepository::mkdir(std::int64_t userId, std::int64_t parentId,
                                    const std::string& name) {
    validateName(name);
    std::lock_guard lock(mutex_);
    requireParentDirectory(userId, parentId);
    try {
        Statement insert(db_, "INSERT INTO nodes(owner_id,parent_id,name,is_directory,size,modified_at) VALUES(?,?,?,1,0,?)");
        insert.integer(1,userId); insert.integer(2,parentId); insert.text(3,name); insert.integer(4,nowMillis()); insert.step();
        return sqlite3_last_insert_rowid(db_);
    } catch (const ServiceError& e) {
        if (std::string(e.what()).find("UNIQUE") != std::string::npos)
            throw ServiceError(ErrorCode::NameConflict, "name already exists in this directory");
        throw;
    }
}

void CloudRepository::renameNode(std::int64_t userId, std::int64_t nodeId,
                                 const std::string& name) {
    validateName(name);
    std::lock_guard lock(mutex_);
    try {
        Statement update(db_, "UPDATE nodes SET name=?,modified_at=? WHERE id=? AND owner_id=?");
        update.text(1,name); update.integer(2,nowMillis()); update.integer(3,nodeId); update.integer(4,userId); update.step();
        if (sqlite3_changes(db_) == 0) throw ServiceError(ErrorCode::NotFound, "node not found");
    } catch (const ServiceError& e) {
        if (std::string(e.what()).find("UNIQUE") != std::string::npos)
            throw ServiceError(ErrorCode::NameConflict, "name already exists in this directory");
        throw;
    }
}

void CloudRepository::deleteNode(std::int64_t userId, std::int64_t nodeId) {
    std::lock_guard lock(mutex_);
    Statement exists(db_, "SELECT 1 FROM nodes WHERE id=? AND owner_id=?");
    exists.integer(1,nodeId); exists.integer(2,userId);
    if (exists.step()!=SQLITE_ROW) throw ServiceError(ErrorCode::NotFound, "node not found");
    std::vector<std::int64_t> blobIds;
    Statement blobs(db_, "WITH RECURSIVE tree(id,blob_id) AS (SELECT id,blob_id FROM nodes WHERE id=? AND owner_id=? UNION ALL SELECT n.id,n.blob_id FROM nodes n JOIN tree t ON n.parent_id=t.id WHERE n.owner_id=?) SELECT blob_id FROM tree WHERE blob_id IS NOT NULL");
    blobs.integer(1,nodeId); blobs.integer(2,userId); blobs.integer(3,userId);
    while (blobs.step()==SQLITE_ROW) blobIds.push_back(sqlite3_column_int64(blobs.get(),0));
    exec("BEGIN IMMEDIATE");
    try {
        Statement remove(db_, "WITH RECURSIVE tree(id) AS (SELECT id FROM nodes WHERE id=? AND owner_id=? UNION ALL SELECT n.id FROM nodes n JOIN tree t ON n.parent_id=t.id WHERE n.owner_id=?) DELETE FROM nodes WHERE id IN (SELECT id FROM tree)");
        remove.integer(1,nodeId); remove.integer(2,userId); remove.integer(3,userId); remove.step();
        for (auto id : blobIds) { Statement dec(db_, "UPDATE blobs SET ref_count=ref_count-1 WHERE id=?"); dec.integer(1,id); dec.step(); }
        exec("COMMIT");
    } catch (...) { exec("ROLLBACK"); throw; }
    Statement zero(db_, "SELECT id,relative_path FROM blobs WHERE ref_count<=0");
    std::vector<std::pair<std::int64_t,std::string>> unused;
    while (zero.step()==SQLITE_ROW) unused.emplace_back(sqlite3_column_int64(zero.get(),0),columnText(zero.get(),1));
    for (const auto& [id,path] : unused) {
        std::error_code ignored; std::filesystem::remove(storageRoot_/path,ignored);
        Statement del(db_, "DELETE FROM blobs WHERE id=? AND ref_count<=0"); del.integer(1,id); del.step();
    }
}

StoredFileInfo CloudRepository::getStoredFile(std::int64_t userId,
                                               std::int64_t nodeId) {
    std::lock_guard lock(mutex_);
    Statement query(db_,
        "SELECT n.parent_id,n.name,b.relative_path,n.size "
        "FROM nodes n JOIN blobs b ON n.blob_id=b.id "
        "WHERE n.id=? AND n.owner_id=? AND n.is_directory=0");
    query.integer(1,nodeId); query.integer(2,userId);
    if(query.step()!=SQLITE_ROW)
        throw ServiceError(ErrorCode::NotFound,"file not found");
    return {sqlite3_column_int64(query.get(),0), columnText(query.get(),1),
            storageRoot_/columnText(query.get(),2),
            sqlite3_column_int64(query.get(),3)};
}

std::int64_t CloudRepository::importLocalFile(
    std::int64_t userId, std::int64_t parentId, const std::string& name,
    const std::filesystem::path& localPath) {
    std::error_code sizeError;
    const auto rawSize=std::filesystem::file_size(localPath,sizeError);
    if(sizeError || rawSize>static_cast<std::uintmax_t>(
                         std::numeric_limits<std::int64_t>::max()))
        throw ServiceError(ErrorCode::IoError,"cannot read converted file size");
    const auto size=static_cast<std::int64_t>(rawSize);
    const auto sha=cloud::common::sha256File(localPath);
    const auto init=beginUpload(userId,parentId,name,size,sha);
    if(init.instant) return init.nodeId;

    try {
        std::ifstream input(localPath,std::ios::binary);
        if(!input) throw ServiceError(ErrorCode::IoError,"cannot open converted file");
        std::vector<std::uint8_t> block(256U*1024U);
        std::int64_t offset=0;
        while(input) {
            input.read(reinterpret_cast<char*>(block.data()),
                       static_cast<std::streamsize>(block.size()));
            const auto count=static_cast<std::size_t>(input.gcount());
            if(count==0) break;
            offset=appendUpload(userId,init.transferId,offset,block.data(),count);
        }
        if(offset!=size)
            throw ServiceError(ErrorCode::IoError,"cannot read complete converted file");
        return finishUpload(userId,init.transferId);
    } catch(...) {
        std::lock_guard lock(mutex_);
        const auto it=uploads_.find(init.transferId);
        if(it!=uploads_.end()) {
            std::error_code ignored;
            std::filesystem::remove(it->second.tempPath,ignored);
            uploads_.erase(it);
        }
        throw;
    }
}

std::filesystem::path CloudRepository::blobPath(const std::string& sha) const {
    return storageRoot_ / "blobs" / sha.substr(0,2) / sha.substr(2,2) / sha;
}

UploadInitResult CloudRepository::beginUpload(std::int64_t userId, std::int64_t parentId,
                                              const std::string& name, std::int64_t size,
                                              const std::string& sha) {
    validateName(name); ensureSha(sha);
    if (size < 0) throw ServiceError(ErrorCode::BadRequest, "negative file size");
    std::lock_guard lock(mutex_);
    requireParentDirectory(userId,parentId);
    Statement conflict(db_, "SELECT 1 FROM nodes WHERE owner_id=? AND parent_id=? AND name=?");
    conflict.integer(1,userId); conflict.integer(2,parentId); conflict.text(3,name);
    if (conflict.step()==SQLITE_ROW) throw ServiceError(ErrorCode::NameConflict,"name already exists in this directory");
    Statement existing(db_, "SELECT id FROM blobs WHERE sha256=? AND size=?");
    existing.text(1,sha); existing.integer(2,size);
    if (existing.step()==SQLITE_ROW) {
        const auto blobId=sqlite3_column_int64(existing.get(),0);
        exec("BEGIN IMMEDIATE");
        try {
            Statement inc(db_,"UPDATE blobs SET ref_count=ref_count+1 WHERE id=?"); inc.integer(1,blobId); inc.step();
            Statement add(db_,"INSERT INTO nodes(owner_id,parent_id,name,is_directory,blob_id,size,modified_at) VALUES(?,?,?,0,?,?,?)");
            add.integer(1,userId); add.integer(2,parentId); add.text(3,name); add.integer(4,blobId); add.integer(5,size); add.integer(6,nowMillis()); add.step();
            const auto nodeId=sqlite3_last_insert_rowid(db_); exec("COMMIT");
            return {"",true,nodeId};
        } catch (...) { exec("ROLLBACK"); throw; }
    }
    const auto id=cloud::common::randomHex(16);
    const auto path=storageRoot_/"temp"/(id+".part");
    { std::ofstream create(path,std::ios::binary|std::ios::trunc); if(!create) throw ServiceError(ErrorCode::IoError,"cannot create upload temp file"); }
    uploads_[id]={userId,parentId,name,sha,size,0,path};
    return {id,false,0};
}

std::int64_t CloudRepository::appendUpload(std::int64_t userId, const std::string& id,
                                           std::int64_t offset, const std::uint8_t* data,
                                           std::size_t size) {
    std::lock_guard lock(mutex_);
    const auto it=uploads_.find(id);
    if(it==uploads_.end()||it->second.userId!=userId) throw ServiceError(ErrorCode::TransferExpired,"upload transfer not found");
    auto& state=it->second;
    if(offset!=state.received) throw ServiceError(ErrorCode::BadRequest,"unexpected upload offset");
    if(state.received+static_cast<std::int64_t>(size)>state.expectedSize) throw ServiceError(ErrorCode::BadRequest,"upload exceeds declared size");
    std::ofstream out(state.tempPath,std::ios::binary|std::ios::app);
    out.write(reinterpret_cast<const char*>(data),static_cast<std::streamsize>(size));
    if(!out) throw ServiceError(ErrorCode::IoError,"cannot write upload block");
    state.received+=static_cast<std::int64_t>(size);
    return state.received;
}

std::int64_t CloudRepository::finishUpload(std::int64_t userId, const std::string& id) {
    std::lock_guard lock(mutex_);
    const auto it=uploads_.find(id);
    if(it==uploads_.end()||it->second.userId!=userId) throw ServiceError(ErrorCode::TransferExpired,"upload transfer not found");
    const auto state=it->second;
    if(state.received!=state.expectedSize) throw ServiceError(ErrorCode::BadRequest,"upload is incomplete");
    if(cloud::common::sha256File(state.tempPath)!=state.sha256) {
        std::filesystem::remove(state.tempPath); uploads_.erase(it);
        throw ServiceError(ErrorCode::HashMismatch,"uploaded file hash does not match");
    }
    auto finalPath=blobPath(state.sha256);
    std::filesystem::create_directories(finalPath.parent_path());
    std::int64_t blobId=0;
    Statement existing(db_,"SELECT id FROM blobs WHERE sha256=? AND size=?"); existing.text(1,state.sha256); existing.integer(2,state.expectedSize);
    if(existing.step()==SQLITE_ROW) { blobId=sqlite3_column_int64(existing.get(),0); std::filesystem::remove(state.tempPath); }
    else {
        std::error_code error; std::filesystem::rename(state.tempPath,finalPath,error);
        if(error) throw ServiceError(ErrorCode::IoError,"cannot commit uploaded file: "+error.message());
        const auto rel=std::filesystem::relative(finalPath,storageRoot_).generic_string();
        Statement addBlob(db_,"INSERT INTO blobs(sha256,size,relative_path,ref_count,created_at) VALUES(?,?,?,0,?)");
        addBlob.text(1,state.sha256); addBlob.integer(2,state.expectedSize); addBlob.text(3,rel); addBlob.integer(4,nowMillis()); addBlob.step();
        blobId=sqlite3_last_insert_rowid(db_);
    }
    exec("BEGIN IMMEDIATE");
    try {
        Statement inc(db_,"UPDATE blobs SET ref_count=ref_count+1 WHERE id=?"); inc.integer(1,blobId); inc.step();
        Statement add(db_,"INSERT INTO nodes(owner_id,parent_id,name,is_directory,blob_id,size,modified_at) VALUES(?,?,?,0,?,?,?)");
        add.integer(1,state.userId); add.integer(2,state.parentId); add.text(3,state.name); add.integer(4,blobId); add.integer(5,state.expectedSize); add.integer(6,nowMillis()); add.step();
        const auto nodeId=sqlite3_last_insert_rowid(db_); exec("COMMIT"); uploads_.erase(it); return nodeId;
    } catch (...) { exec("ROLLBACK"); throw; }
}

DownloadInfo CloudRepository::beginDownload(std::int64_t userId, std::int64_t nodeId) {
    std::lock_guard lock(mutex_);
    Statement q(db_,"SELECT n.name,n.size,b.sha256,b.relative_path FROM nodes n JOIN blobs b ON n.blob_id=b.id WHERE n.id=? AND n.owner_id=? AND n.is_directory=0");
    q.integer(1,nodeId); q.integer(2,userId);
    if(q.step()!=SQLITE_ROW) throw ServiceError(ErrorCode::NotFound,"file not found");
    const auto id=cloud::common::randomHex(16);
    DownloadInfo info{id,columnText(q.get(),0),columnText(q.get(),2),sqlite3_column_int64(q.get(),1)};
    downloads_[id]={userId,storageRoot_/columnText(q.get(),3),info.size};
    return info;
}

std::vector<std::uint8_t> CloudRepository::readDownload(std::int64_t userId,
                                                        const std::string& id,
                                                        std::int64_t offset,
                                                        std::size_t maxBytes,
                                                        bool& final) {
    std::lock_guard lock(mutex_);
    const auto it=downloads_.find(id);
    if(it==downloads_.end()||it->second.userId!=userId) throw ServiceError(ErrorCode::TransferExpired,"download transfer not found");
    if(offset<0||offset>it->second.size) throw ServiceError(ErrorCode::BadRequest,"invalid download offset");
    const auto count=static_cast<std::size_t>(std::min<std::int64_t>(static_cast<std::int64_t>(maxBytes),it->second.size-offset));
    std::vector<std::uint8_t> bytes(count);
    std::ifstream in(it->second.blobPath,std::ios::binary);
    in.seekg(offset); in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(static_cast<std::size_t>(in.gcount())!=bytes.size()) throw ServiceError(ErrorCode::IoError,"cannot read stored blob");
    final=offset+static_cast<std::int64_t>(bytes.size())==it->second.size;
    if(final) downloads_.erase(it);
    return bytes;
}

} // namespace cloud::server
