/**
 * @file Database.cpp
 * @brief SQLite3 数据库封装实现
 *
 * 使用 sqlite3 C API 实现所有数据库操作。
 * 密码哈希使用 Argon2id 算法（通过 <argon2.h> 头文件）。
 * 数据库运行在 WAL 模式下以支持并发读写。
 *
 * 当 SQLite3 不可用时（NEVO_HAS_SQLITE 未定义），提供 stub 实现，
 * 编译可通过但运行时返回错误。
 */

#include "nevo/server/Database.h"
#include "nevo/core/common/Logger.h"

#ifdef NEVO_HAS_SQLITE
#include <sqlite3.h>
#ifdef HAVE_ARGON2
#include <argon2.h>
#endif
#endif

#ifdef NEVO_HAS_SODIUM
#include <sodium.h>
#endif

#include <cstring>
#include <chrono>
#include <random>

namespace nevo {

#ifdef NEVO_HAS_SQLITE

// ============================================================
// 构造 / 析构
// ============================================================

Database::Database() = default;

Database::~Database() {
    if (db_) {
        // 关闭数据库连接（RAII）
        int rc = sqlite3_close(db_);
        if (rc != SQLITE_OK) {
            NEVO_LOG_ERROR("server", "Failed to close database: {}", sqlite3_errmsg(db_));
        }
        db_ = nullptr;
        NEVO_LOG_INFO("server", "Database connection closed");
    }
}

// ============================================================
// 初始化
// ============================================================

Result<void> Database::initialize(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return Err<void>(ResultCode::InvalidRequest, "Database already initialized");
    }

    NEVO_LOG_INFO("server", "Opening database: {}", db_path);

    // 打开数据库文件（如果不存在则创建）
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        NEVO_LOG_ERROR("server", "Failed to open database: {}", err);
        return Err<void>(ResultCode::DatabaseError,
                         "Failed to open database: " + err);
    }

    // 设置 WAL 模式
    auto wal_result = setWalMode();
    if (!wal_result) {
        sqlite3_close(db_);
        db_ = nullptr;
        return wal_result;
    }

    // 创建表（如果不存在）
    auto table_result = createTables();
    if (!table_result) {
        sqlite3_close(db_);
        db_ = nullptr;
        return table_result;
    }

    // 迁移：为旧数据库添加 public_key 列
    {
        char* err_msg = nullptr;
        const char* alter_sql = "ALTER TABLE users ADD COLUMN public_key BLOB DEFAULT NULL";
        int rc = sqlite3_exec(db_, alter_sql, nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            // 忽略 "duplicate column name" 错误（旧版 SQLite 返回 SQLITE_ERROR）
            std::string err = err_msg ? err_msg : "unknown error";
            if (err.find("duplicate column name") == std::string::npos &&
                err.find("already exists") == std::string::npos) {
                NEVO_LOG_WARN("server", "Schema migration warning: {}", err);
            }
            sqlite3_free(err_msg);
        }
    }

    // 迁移：移除 channels 表的外键约束
    // SQLite 不支持 ALTER TABLE DROP CONSTRAINT，需要重建表
    {
        bool need_migration = false;
        {
            sqlite3_exec(db_, "PRAGMA foreign_keys=OFF", nullptr, nullptr, nullptr);
            sqlite3_stmt* check_stmt = nullptr;
            const char* check_sql = "SELECT sql FROM sqlite_master WHERE type='table' AND name='channels'";
            int rc = sqlite3_prepare_v2(db_, check_sql, -1, &check_stmt, nullptr);
            if (rc == SQLITE_OK) {
                rc = sqlite3_step(check_stmt);
                if (rc == SQLITE_ROW) {
                    const char* schema = reinterpret_cast<const char*>(sqlite3_column_text(check_stmt, 0));
                    if (schema && (std::string(schema).find("FOREIGN KEY (created_by)") != std::string::npos ||
                                   std::string(schema).find("FOREIGN KEY (parent_id)") != std::string::npos)) {
                        need_migration = true;
                    }
                }
                sqlite3_finalize(check_stmt);
            }
        }

        if (need_migration) {
            NEVO_LOG_INFO("server", "Migrating channels table: removing foreign key constraints");
            const char* migrate_sqls[] = {
                "ALTER TABLE channels RENAME TO channels_old",
                R"(CREATE TABLE channels (
                    id              INTEGER PRIMARY KEY AUTOINCREMENT,
                    name            TEXT    NOT NULL,
                    parent_id       INTEGER NOT NULL DEFAULT 0,
                    created_by      INTEGER NOT NULL DEFAULT 0,
                    is_permanent    INTEGER NOT NULL DEFAULT 1,
                    created_at      INTEGER NOT NULL DEFAULT 0
                ))",
                "INSERT INTO channels SELECT * FROM channels_old",
                "DROP TABLE channels_old",
                "CREATE INDEX IF NOT EXISTS idx_channels_parent ON channels(parent_id)",
            };

            bool migration_ok = true;
            for (const char* sql : migrate_sqls) {
                char* err_msg = nullptr;
                int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
                if (rc != SQLITE_OK) {
                    std::string err = err_msg ? err_msg : "unknown error";
                    NEVO_LOG_ERROR("server", "Channel migration failed: {}", err);
                    sqlite3_free(err_msg);
                    migration_ok = false;
                    break;
                }
            }
            if (migration_ok) {
                NEVO_LOG_INFO("server", "Channels table migrated successfully");
            }
        }

        // 重新启用外键检查
        sqlite3_exec(db_, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
    }

    initialized_ = true;
    NEVO_LOG_INFO("server", "Database initialized successfully");
    return Ok();
}

bool Database::isInitialized() const {
    return initialized_;
}

// ============================================================
// 用户管理
// ============================================================

Result<UserId> Database::createUser(const std::string& username,
                                     const std::string& auth_credential) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<UserId>(ResultCode::DatabaseError, "Database not initialized");
    }

    NEVO_LOG_INFO("server", "Creating user: {}", username);

    // 检查用户名是否已存在
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id FROM users WHERE username = ?";
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            NEVO_LOG_ERROR("server", "Failed to prepare statement: {}", sqlite3_errmsg(db_));
            return Err<UserId>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
        }
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

        bool exists = false;
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            exists = true;
        }
        sqlite3_finalize(stmt);

        if (exists) {
            NEVO_LOG_WARN("server", "User already exists: {}", username);
            return Err<UserId>(ResultCode::InvalidRequest, "Username already exists");
        }
    }

    // 使用 Argon2id 哈希密码
    std::string hash = hashPassword(auth_credential);

    // Argon2 参数编码字符串（用于审计和参数记录）
    std::string argon2_params = "m=65536,t=3,p=2";

    // 获取当前时间戳
    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    // 插入用户记录
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO users (username, password_hash, argon2_params, group_id, created_at, last_login) "
                          "VALUES (?, ?, ?, ?, ?, ?)";
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            NEVO_LOG_ERROR("server", "Failed to prepare insert: {}", sqlite3_errmsg(db_));
            return Err<UserId>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
        }

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, argon2_params.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, 3);  // 默认 User 组 (GROUP_USER = 3)
        sqlite3_bind_int64(stmt, 5, timestamp);
        sqlite3_bind_int64(stmt, 6, 0); // last_login 初始为 0

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            NEVO_LOG_ERROR("server", "Failed to insert user: {}", err);
            return Err<UserId>(ResultCode::DatabaseError, err);
        }
        sqlite3_finalize(stmt);
    }

    // 获取新插入用户的 ID
    UserId new_id(sqlite3_last_insert_rowid(db_));
    NEVO_LOG_INFO("server", "User created: {} (id={})", username, new_id.value);
    return Ok(new_id);
}

Result<UserId> Database::verifyUser(const std::string& username,
                                     const std::string& auth_credential) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<UserId>(ResultCode::DatabaseError, "Database not initialized");
    }

    NEVO_LOG_DEBUG("server", "Verifying user: {}", username);

    // 查找用户记录
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, password_hash FROM users WHERE username = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare statement: {}", sqlite3_errmsg(db_));
        return Err<UserId>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        NEVO_LOG_WARN("server", "User not found: {}", username);
        return Err<UserId>(ResultCode::AuthFailed, "User not found");
    }

    UserId user_id(sqlite3_column_int64(stmt, 0));
    const char* hash_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    std::string password_hash(hash_str ? hash_str : "");

    sqlite3_finalize(stmt);

    // 使用 Argon2id 验证密码
    if (!verifyPassword(password_hash, auth_credential)) {
        NEVO_LOG_WARN("server", "Password verification failed for user: {}", username);
        return Err<UserId>(ResultCode::AuthFailed, "Invalid password");
    }

    // 更新 last_login 时间戳
    {
        auto now = std::chrono::system_clock::now();
        int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        sqlite3_stmt* update_stmt = nullptr;
        const char* update_sql = "UPDATE users SET last_login = ? WHERE id = ?";
        rc = sqlite3_prepare_v2(db_, update_sql, -1, &update_stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int64(update_stmt, 1, timestamp);
            sqlite3_bind_int64(update_stmt, 2, user_id.value);
            sqlite3_step(update_stmt);
            sqlite3_finalize(update_stmt);
        }
    }

    NEVO_LOG_INFO("server", "User verified: {} (id={})", username, user_id.value);
    return Ok(user_id);
}

std::optional<UserRecord> Database::getUser(UserId userId) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, username, password_hash, argon2_params, group_id, created_at, last_login, public_key "
                      "FROM users WHERE id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare statement: {}", sqlite3_errmsg(db_));
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, userId.value);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    UserRecord record;
    record.id = UserId(sqlite3_column_int64(stmt, 0));
    record.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    record.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    record.argon2_params = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    record.group_id = GroupId(static_cast<uint32_t>(sqlite3_column_int(stmt, 4)));
    record.created_at = sqlite3_column_int64(stmt, 5);
    record.last_login = sqlite3_column_int64(stmt, 6);
    const void* pubkey_blob = sqlite3_column_blob(stmt, 7);
    int pubkey_len = sqlite3_column_bytes(stmt, 7);
    if (pubkey_blob && pubkey_len > 0) {
        record.public_key.assign(static_cast<const uint8_t*>(pubkey_blob),
                                 static_cast<const uint8_t*>(pubkey_blob) + pubkey_len);
    }

    sqlite3_finalize(stmt);
    return record;
}

std::optional<UserRecord> Database::getUserByName(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, username, password_hash, argon2_params, group_id, created_at, last_login, public_key "
                      "FROM users WHERE username = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare statement: {}", sqlite3_errmsg(db_));
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    UserRecord record;
    record.id = UserId(sqlite3_column_int64(stmt, 0));
    record.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    record.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    record.argon2_params = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    record.group_id = GroupId(static_cast<uint32_t>(sqlite3_column_int(stmt, 4)));
    record.created_at = sqlite3_column_int64(stmt, 5);
    record.last_login = sqlite3_column_int64(stmt, 6);
    const void* pubkey_blob = sqlite3_column_blob(stmt, 7);
    int pubkey_len = sqlite3_column_bytes(stmt, 7);
    if (pubkey_blob && pubkey_len > 0) {
        record.public_key.assign(static_cast<const uint8_t*>(pubkey_blob),
                                 static_cast<const uint8_t*>(pubkey_blob) + pubkey_len);
    }

    sqlite3_finalize(stmt);
    return record;
}

Result<void> Database::updateUserPublicKey(UserId user_id,
                                            const std::vector<uint8_t>& public_key) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<void>(ResultCode::DatabaseError, "Database not initialized");
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE users SET public_key = ? WHERE id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare updateUserPublicKey: {}", sqlite3_errmsg(db_));
        return Err<void>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    if (!public_key.empty()) {
        sqlite3_bind_blob(stmt, 1, public_key.data(), static_cast<int>(public_key.size()), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    sqlite3_bind_int64(stmt, 2, user_id.value);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        NEVO_LOG_ERROR("server", "Failed to update user public key: {}", sqlite3_errmsg(db_));
        return Err<void>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    NEVO_LOG_INFO("server", "Updated public key for user id={}", user_id.value);
    return Ok();
}

Result<void> Database::updateUserGroupId(UserId user_id, GroupId group_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<void>(ResultCode::DatabaseError, "Database not initialized");
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE users SET group_id = ? WHERE id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare updateUserGroupId: {}", sqlite3_errmsg(db_));
        return Err<void>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(group_id.value));
    sqlite3_bind_int64(stmt, 2, user_id.value);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        NEVO_LOG_ERROR("server", "Failed to update user group id: {}", sqlite3_errmsg(db_));
        return Err<void>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    if (sqlite3_changes(db_) == 0) {
        NEVO_LOG_WARN("server", "User id={} not found for group update", user_id.value);
        return Err<void>(ResultCode::UserNotFound, "User not found");
    }

    NEVO_LOG_INFO("server", "Updated group_id={} for user id={}", group_id.value, user_id.value);
    return Ok();
}

// ============================================================
// 频道管理
// ============================================================

Result<ChannelId> Database::createChannel(const std::string& name,
                                           ChannelId parent_id,
                                           UserId created_by) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<ChannelId>(ResultCode::DatabaseError, "Database not initialized");
    }

    NEVO_LOG_INFO("server", "Creating channel: {} (parent_id={})", name, parent_id.value);

    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO channels (name, parent_id, created_by, is_permanent, created_at) "
                      "VALUES (?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare insert: {}", sqlite3_errmsg(db_));
        return Err<ChannelId>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, parent_id.value);
    sqlite3_bind_int64(stmt, 3, created_by.value);
    sqlite3_bind_int(stmt, 4, 1);  // is_permanent = true
    sqlite3_bind_int64(stmt, 5, timestamp);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        NEVO_LOG_ERROR("server", "Failed to insert channel: {}", err);
        return Err<ChannelId>(ResultCode::DatabaseError, err);
    }
    sqlite3_finalize(stmt);

    ChannelId new_id(sqlite3_last_insert_rowid(db_));
    NEVO_LOG_INFO("server", "Channel created: {} (id={})", name, new_id.value);
    return Ok(new_id);
}

Result<ChannelId> Database::createChannelWithId(ChannelId id,
                                                  const std::string& name,
                                                  ChannelId parent_id,
                                                  UserId created_by) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<ChannelId>(ResultCode::DatabaseError, "Database not initialized");
    }

    NEVO_LOG_INFO("server", "Creating channel: {} (id={}, parent_id={})", name, id.value, parent_id.value);

    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO channels (id, name, parent_id, created_by, is_permanent, created_at) "
                      "VALUES (?, ?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare insert: {}", sqlite3_errmsg(db_));
        return Err<ChannelId>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_int64(stmt, 1, id.value);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, parent_id.value);
    sqlite3_bind_int64(stmt, 4, created_by.value);
    sqlite3_bind_int(stmt, 5, 1);  // is_permanent = true
    sqlite3_bind_int64(stmt, 6, timestamp);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        NEVO_LOG_ERROR("server", "Failed to insert channel: {}", err);
        return Err<ChannelId>(ResultCode::DatabaseError, err);
    }
    sqlite3_finalize(stmt);

    NEVO_LOG_INFO("server", "Channel created: {} (id={})", name, id.value);
    return Ok(id);
}

Result<void> Database::deleteChannel(ChannelId id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<void>(ResultCode::DatabaseError, "Database not initialized");
    }

    NEVO_LOG_INFO("server", "Deleting channel id={}", id.value);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM channels WHERE id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare delete: {}", sqlite3_errmsg(db_));
        return Err<void>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_int64(stmt, 1, id.value);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        NEVO_LOG_ERROR("server", "Failed to delete channel: {}", err);
        return Err<void>(ResultCode::DatabaseError, err);
    }

    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (changes == 0) {
        NEVO_LOG_WARN("server", "Channel not found for deletion: id={}", id.value);
        return Err<void>(ResultCode::ChannelNotFound, "Channel not found");
    }

    NEVO_LOG_INFO("server", "Channel deleted: id={}", id.value);
    return Ok();
}

std::optional<ChannelRecord> Database::getChannel(ChannelId id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, name, parent_id, created_by, is_permanent, created_at "
                      "FROM channels WHERE id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare statement: {}", sqlite3_errmsg(db_));
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, id.value);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    ChannelRecord record;
    record.id = ChannelId(sqlite3_column_int64(stmt, 0));
    record.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    record.parent_id = ChannelId(sqlite3_column_int64(stmt, 2));
    record.created_by = UserId(sqlite3_column_int64(stmt, 3));
    record.is_permanent = sqlite3_column_int(stmt, 4) != 0;
    record.created_at = sqlite3_column_int64(stmt, 5);

    sqlite3_finalize(stmt);
    return record;
}

std::vector<ChannelRecord> Database::getChannelsByParent(ChannelId parent_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return {};
    }

    std::vector<ChannelRecord> results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, name, parent_id, created_by, is_permanent, created_at "
                      "FROM channels WHERE parent_id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare statement: {}", sqlite3_errmsg(db_));
        return results;
    }

    sqlite3_bind_int64(stmt, 1, parent_id.value);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChannelRecord record;
        record.id = ChannelId(sqlite3_column_int64(stmt, 0));
        record.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        record.parent_id = ChannelId(sqlite3_column_int64(stmt, 2));
        record.created_by = UserId(sqlite3_column_int64(stmt, 3));
        record.is_permanent = sqlite3_column_int(stmt, 4) != 0;
        record.created_at = sqlite3_column_int64(stmt, 5);
        results.push_back(std::move(record));
    }

    sqlite3_finalize(stmt);
    return results;
}

Result<void> Database::renameChannel(ChannelId id, const std::string& new_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<void>(ResultCode::DatabaseError, "Database not initialized");
    }

    NEVO_LOG_INFO("server", "Renaming channel id={} to '{}'", id.value, new_name);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE channels SET name = ? WHERE id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare rename: {}", sqlite3_errmsg(db_));
        return Err<void>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, new_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, id.value);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        NEVO_LOG_ERROR("server", "Failed to rename channel: {}", err);
        return Err<void>(ResultCode::DatabaseError, err);
    }

    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (changes == 0) {
        NEVO_LOG_WARN("server", "Channel not found for rename: id={}", id.value);
        return Err<void>(ResultCode::ChannelNotFound, "Channel not found");
    }

    NEVO_LOG_INFO("server", "Channel renamed: id={} to '{}'", id.value, new_name);
    return Ok();
}

// ============================================================
// 服务器配置
// ============================================================

std::optional<std::string> Database::getConfig(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT value FROM server_config WHERE key = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare statement: {}", sqlite3_errmsg(db_));
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::string result(val ? val : "");
    sqlite3_finalize(stmt);

    return result;
}

Result<void> Database::setConfig(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<void>(ResultCode::DatabaseError, "Database not initialized");
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO server_config (key, value) VALUES (?, ?)";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare statement: {}", sqlite3_errmsg(db_));
        return Err<void>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return Err<void>(ResultCode::DatabaseError, err);
    }

    sqlite3_finalize(stmt);
    return Ok();
}

// ============================================================
// 封禁管理
// ============================================================

Result<void> Database::addBan(UserId user_id, const std::string& ip_address,
                               const std::string& reason, int64_t expires_at) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<void>(ResultCode::DatabaseError, "Database not initialized");
    }

    NEVO_LOG_INFO("server", "Adding ban: user_id={}, ip={}, reason={}, expires_at={}",
                  user_id.value, ip_address, reason, expires_at);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO bans (user_id, ip_address, reason, expires_at) VALUES (?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare insert: {}", sqlite3_errmsg(db_));
        return Err<void>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_int64(stmt, 1, user_id.value);
    sqlite3_bind_text(stmt, 2, ip_address.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, expires_at);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        NEVO_LOG_ERROR("server", "Failed to insert ban: {}", err);
        return Err<void>(ResultCode::DatabaseError, err);
    }

    sqlite3_finalize(stmt);
    NEVO_LOG_INFO("server", "Ban added successfully");
    return Ok();
}

bool Database::isBanned(UserId user_id, const std::string& ip_address) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return false;
    }

    // 获取当前时间戳
    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    // 检查用户 ID 是否被封禁（未过期或永久封禁）
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT COUNT(*) FROM bans WHERE user_id = ? AND (expires_at = 0 OR expires_at > ?)";
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, user_id.value);
            sqlite3_bind_int64(stmt, 2, timestamp);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                int count = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
                if (count > 0) {
                    return true;
                }
            } else {
                sqlite3_finalize(stmt);
            }
        } else {
            sqlite3_finalize(stmt);
        }
    }

    // 检查 IP 地址是否被封禁（未过期或永久封禁）
    if (!ip_address.empty()) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT COUNT(*) FROM bans WHERE ip_address = ? AND (expires_at = 0 OR expires_at > ?)";
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, ip_address.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, timestamp);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                int count = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
                if (count > 0) {
                    return true;
                }
            } else {
                sqlite3_finalize(stmt);
            }
        } else {
            sqlite3_finalize(stmt);
        }
    }

    return false;
}

// ============================================================
// 文件管理
// ============================================================

Result<int64_t> Database::addFileRecord(int64_t channel_id, int64_t uploader_id,
                                        const std::string& filename, const std::string& file_path,
                                        int64_t file_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<int64_t>(ResultCode::DatabaseError, "Database not initialized");
    }

    NEVO_LOG_INFO("server", "Adding file record: filename={}, channel_id={}, size={}",
                  filename, channel_id, file_size);

    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO files (channel_id, uploader_id, filename, file_path, file_size, upload_time) "
                      "VALUES (?, ?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare addFileRecord: {}", sqlite3_errmsg(db_));
        return Err<int64_t>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_int64(stmt, 1, channel_id);
    sqlite3_bind_int64(stmt, 2, uploader_id);
    sqlite3_bind_text(stmt, 3, filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, file_size);
    sqlite3_bind_int64(stmt, 6, timestamp);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        NEVO_LOG_ERROR("server", "Failed to insert file record: {}", err);
        return Err<int64_t>(ResultCode::DatabaseError, err);
    }
    sqlite3_finalize(stmt);

    int64_t new_id = sqlite3_last_insert_rowid(db_);
    NEVO_LOG_INFO("server", "File record added: id={}, filename={}", new_id, filename);
    return Ok(new_id);
}

std::vector<FileRecord> Database::getFileList(int64_t channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return {};
    }

    std::vector<FileRecord> results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, channel_id, uploader_id, filename, file_path, file_size, upload_time "
                      "FROM files WHERE channel_id = ? ORDER BY upload_time DESC";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare getFileList: {}", sqlite3_errmsg(db_));
        return results;
    }

    sqlite3_bind_int64(stmt, 1, channel_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileRecord record;
        record.id = sqlite3_column_int64(stmt, 0);
        record.channel_id = sqlite3_column_int64(stmt, 1);
        record.uploader_id = sqlite3_column_int64(stmt, 2);
        record.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        record.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        record.file_size = sqlite3_column_int64(stmt, 5);
        record.upload_time = sqlite3_column_int64(stmt, 6);
        results.push_back(std::move(record));
    }

    sqlite3_finalize(stmt);
    return results;
}

Result<void> Database::deleteFile(int64_t file_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<void>(ResultCode::DatabaseError, "Database not initialized");
    }

    NEVO_LOG_INFO("server", "Deleting file record: id={}", file_id);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM files WHERE id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare deleteFile: {}", sqlite3_errmsg(db_));
        return Err<void>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_int64(stmt, 1, file_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        NEVO_LOG_ERROR("server", "Failed to delete file record: {}", err);
        return Err<void>(ResultCode::DatabaseError, err);
    }

    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (changes == 0) {
        NEVO_LOG_WARN("server", "File record not found for deletion: id={}", file_id);
        return Err<void>(ResultCode::InvalidRequest, "File not found");
    }

    NEVO_LOG_INFO("server", "File record deleted: id={}", file_id);
    return Ok();
}

Result<FileRecord> Database::getFile(int64_t file_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return Err<FileRecord>(ResultCode::DatabaseError, "Database not initialized");
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, channel_id, uploader_id, filename, file_path, file_size, upload_time "
                      "FROM files WHERE id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to prepare getFile: {}", sqlite3_errmsg(db_));
        return Err<FileRecord>(ResultCode::DatabaseError, sqlite3_errmsg(db_));
    }

    sqlite3_bind_int64(stmt, 1, file_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        NEVO_LOG_WARN("server", "File record not found: id={}", file_id);
        return Err<FileRecord>(ResultCode::InvalidRequest, "File not found");
    }

    FileRecord record;
    record.id = sqlite3_column_int64(stmt, 0);
    record.channel_id = sqlite3_column_int64(stmt, 1);
    record.uploader_id = sqlite3_column_int64(stmt, 2);
    record.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    record.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    record.file_size = sqlite3_column_int64(stmt, 5);
    record.upload_time = sqlite3_column_int64(stmt, 6);

    sqlite3_finalize(stmt);
    return Ok(record);
}

// ============================================================
// 内部方法
// ============================================================

Result<void> Database::createTables() {
    // 创建用户表
    const char* create_users_sql = R"(
        CREATE TABLE IF NOT EXISTS users (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            username        TEXT    NOT NULL UNIQUE,
            password_hash   TEXT    NOT NULL,
            argon2_params   TEXT    NOT NULL DEFAULT 'm=65536,t=3,p=2',
            group_id        INTEGER NOT NULL DEFAULT 3,
            created_at      INTEGER NOT NULL DEFAULT 0,
            last_login      INTEGER NOT NULL DEFAULT 0,
            public_key      BLOB    DEFAULT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
    )";

    // 创建频道表
    // 注意：不设外键约束。created_by=0 表示系统创建，parent_id=0 表示根频道（无父级）
    // 频道的父子关系和创建者由 ChannelManager 应用层逻辑保证
    const char* create_channels_sql = R"(
        CREATE TABLE IF NOT EXISTS channels (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            name            TEXT    NOT NULL,
            parent_id       INTEGER NOT NULL DEFAULT 0,
            created_by      INTEGER NOT NULL DEFAULT 0,
            is_permanent    INTEGER NOT NULL DEFAULT 1,
            created_at      INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_channels_parent ON channels(parent_id);
    )";

    // 创建服务器配置表
    const char* create_config_sql = R"(
        CREATE TABLE IF NOT EXISTS server_config (
            key             TEXT    PRIMARY KEY,
            value           TEXT    NOT NULL
        );
    )";

    // 创建封禁记录表
    const char* create_bans_sql = R"(
        CREATE TABLE IF NOT EXISTS bans (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id         INTEGER NOT NULL DEFAULT 0,
            ip_address      TEXT    NOT NULL DEFAULT '',
            reason          TEXT    NOT NULL DEFAULT '',
            expires_at      INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY (user_id) REFERENCES users(id)
        );
        CREATE INDEX IF NOT EXISTS idx_bans_user ON bans(user_id);
        CREATE INDEX IF NOT EXISTS idx_bans_ip ON bans(ip_address);
    )";

    // 创建文件记录表
    const char* create_files_sql = R"(
        CREATE TABLE IF NOT EXISTS files (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            channel_id      INTEGER NOT NULL,
            uploader_id     INTEGER NOT NULL,
            filename        TEXT    NOT NULL,
            file_path       TEXT    NOT NULL,
            file_size       INTEGER NOT NULL,
            upload_time     INTEGER NOT NULL,
            FOREIGN KEY (channel_id) REFERENCES channels(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_files_channel ON files(channel_id);
    )";

    auto result = executeSql(create_users_sql);
    if (!result) return result;

    result = executeSql(create_channels_sql);
    if (!result) return result;

    result = executeSql(create_config_sql);
    if (!result) return result;

    result = executeSql(create_bans_sql);
    if (!result) return result;

    result = executeSql(create_files_sql);
    if (!result) return result;

    NEVO_LOG_INFO("server", "Database tables created/verified");
    return Ok();
}

Result<void> Database::setWalMode() {
    // 设置 WAL 模式以提升并发读写性能
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "PRAGMA journal_mode=WAL";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        NEVO_LOG_ERROR("server", "Failed to set WAL mode: {}", sqlite3_errmsg(db_));
        return Err<void>(ResultCode::DatabaseError,
                         "Failed to set WAL mode: " + std::string(sqlite3_errmsg(db_)));
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        NEVO_LOG_ERROR("server", "WAL mode execution failed: {}", err);
        return Err<void>(ResultCode::DatabaseError, err);
    }

    // 验证 WAL 模式是否生效
    const char* mode = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    bool is_wal = (mode && std::strcmp(mode, "wal") == 0);
    sqlite3_finalize(stmt);

    if (!is_wal) {
        NEVO_LOG_WARN("server", "WAL mode not activated (got: {})", mode ? mode : "null");
    } else {
        NEVO_LOG_INFO("server", "WAL mode activated");
    }

    // 设置外键约束
    char* err_msg = nullptr;
    rc = sqlite3_exec(db_, "PRAGMA foreign_keys=ON", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "unknown";
        sqlite3_free(err_msg);
        NEVO_LOG_WARN("server", "Failed to enable foreign keys: {}", err);
    }

    return Ok();
}

std::string Database::hashPassword(const std::string& password) {
#ifdef NEVO_HAS_SODIUM
    // Use libsodium's crypto_pwhash_str for secure password hashing (Argon2id)
    char hashed_password[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hashed_password,
                         password.c_str(), password.size(),
                         crypto_pwhash_OPSLIMIT_MODERATE,
                         crypto_pwhash_MEMLIMIT_MODERATE) != 0) {
        NEVO_LOG_ERROR("server", "Failed to hash password (crypto_pwhash_str)");
        return "";
    }
    return std::string(hashed_password);
#else
    // No secure password hashing available!
    NEVO_LOG_ERROR("server", "Cannot hash password: libsodium not available!");
    return "";
#endif
}

bool Database::verifyPassword(const std::string& hash, const std::string& password) {
#ifdef NEVO_HAS_SODIUM
    // Use libsodium's crypto_pwhash_str_verify
    if (crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.size()) == 0) {
        return true;
    }
    return false;
#else
    NEVO_LOG_ERROR("server", "Cannot verify password: libsodium not available!");
    return false;
#endif
}

Result<void> Database::executeSql(const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);

    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "unknown error";
        sqlite3_free(err_msg);
        NEVO_LOG_ERROR("server", "SQL execution error: {}", err);
        return Err<void>(ResultCode::DatabaseError, err);
    }

    return Ok();
}

std::optional<int64_t> Database::getAutoIncrementValue(const std::string& table_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT seq FROM sqlite_sequence WHERE name = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, table_name.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    std::optional<int64_t> result;
    if (rc == SQLITE_ROW) {
        result = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}

#else // !NEVO_HAS_SQLITE

// ============================================================
// Stub implementation when SQLite3 is not available
// ============================================================

Database::Database() = default;

Database::~Database() = default;

Result<void> Database::initialize(const std::string& /*db_path*/) {
    NEVO_LOG_ERROR("server", "Database::initialize: SQLite3 not available (built without SQLite3 support)");
    return Err<void>(ResultCode::DatabaseError, "SQLite3 not available");
}

bool Database::isInitialized() const {
    return false;
}

Result<UserId> Database::createUser(const std::string& /*username*/,
                                     const std::string& /*auth_credential*/) {
    return Err<UserId>(ResultCode::DatabaseError, "SQLite3 not available");
}

Result<UserId> Database::verifyUser(const std::string& /*username*/,
                                     const std::string& /*auth_credential*/) {
    return Err<UserId>(ResultCode::DatabaseError, "SQLite3 not available");
}

std::optional<UserRecord> Database::getUser(UserId /*userId*/) {
    return std::nullopt;
}

std::optional<UserRecord> Database::getUserByName(const std::string& /*username*/) {
    return std::nullopt;
}

Result<void> Database::updateUserPublicKey(UserId /*user_id*/,
                                            const std::vector<uint8_t>& /*public_key*/) {
    return Err<void>(ResultCode::DatabaseError, "SQLite3 not available");
}

Result<void> Database::updateUserGroupId(UserId /*user_id*/, GroupId /*group_id*/) {
    return Err<void>(ResultCode::DatabaseError, "SQLite3 not available");
}

Result<ChannelId> Database::createChannel(const std::string& /*name*/,
                                           ChannelId /*parent_id*/,
                                           UserId /*created_by*/) {
    return Err<ChannelId>(ResultCode::DatabaseError, "SQLite3 not available");
}

Result<void> Database::deleteChannel(ChannelId /*id*/) {
    return Err<void>(ResultCode::DatabaseError, "SQLite3 not available");
}

std::optional<ChannelRecord> Database::getChannel(ChannelId /*id*/) {
    return std::nullopt;
}

std::vector<ChannelRecord> Database::getChannelsByParent(ChannelId /*parent_id*/) {
    return {};
}

Result<void> Database::renameChannel(ChannelId /*id*/, const std::string& /*new_name*/) {
    return Err<void>(ResultCode::DatabaseError, "SQLite3 not available");
}

std::optional<std::string> Database::getConfig(const std::string& /*key*/) {
    return std::nullopt;
}

Result<void> Database::setConfig(const std::string& /*key*/, const std::string& /*value*/) {
    return Err<void>(ResultCode::DatabaseError, "SQLite3 not available");
}

Result<void> Database::addBan(UserId /*user_id*/, const std::string& /*ip_address*/,
                               const std::string& /*reason*/, int64_t /*expires_at*/) {
    return Err<void>(ResultCode::DatabaseError, "SQLite3 not available");
}

bool Database::isBanned(UserId /*user_id*/, const std::string& /*ip_address*/) {
    return false;
}

Result<int64_t> Database::addFileRecord(int64_t /*channel_id*/, int64_t /*uploader_id*/,
                                        const std::string& /*filename*/, const std::string& /*file_path*/,
                                        int64_t /*file_size*/) {
    return Err<int64_t>(ResultCode::DatabaseError, "SQLite3 not available");
}

std::vector<FileRecord> Database::getFileList(int64_t /*channel_id*/) {
    return {};
}

Result<void> Database::deleteFile(int64_t /*file_id*/) {
    return Err<void>(ResultCode::DatabaseError, "SQLite3 not available");
}

Result<FileRecord> Database::getFile(int64_t /*file_id*/) {
    return Err<FileRecord>(ResultCode::DatabaseError, "SQLite3 not available");
}

std::optional<int64_t> Database::getAutoIncrementValue(const std::string& /*table_name*/) {
    return std::nullopt;
}

#endif // NEVO_HAS_SQLITE

} // namespace nevo
