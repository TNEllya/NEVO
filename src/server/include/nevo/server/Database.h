#pragma once
/**
 * @file Database.h
 * @brief SQLite3 数据库封装
 *
 * 为 NEVO 服务端提供持久化存储能力，包括用户认证、频道管理、
 * 服务器配置和封禁记录。所有密码均使用 Argon2id 算法哈希存储。
 *
 * 数据库表结构：
 *   - users:         用户表（id, username, password_hash, argon2_params, group_id, created_at, last_login）
 *   - channels:      频道表（id, name, parent_id, created_by, is_permanent, created_at）
 *   - server_config: 服务器配置键值对（key, value）
 *   - bans:          封禁记录表（id, user_id, ip_address, reason, expires_at）
 *
 * 线程安全说明：
 *   - 所有公开方法内部通过 mutex 串行化对 sqlite3 的访问，
 *     保证多线程环境下的数据一致性。
 *   - WAL 模式允许并发读写，但同一写事务仍需互斥。
 *
 * 使用方式：
 * @code
 *   nevo::Database db;
 *   db.initialize("nevo_server.db");
 *
 *   auto user_result = db.createUser("alice", "password123");
 *   if (user_result) { UserId uid = user_result.value(); }
 *
 *   auto verify = db.verifyUser("alice", "password123");
 * @endcode
 */

#include "nevo/core/common/Types.h"
#include "nevo/core/common/Result.h"

#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <mutex>

// sqlite3 前向声明，避免头文件污染
struct sqlite3;

namespace nevo {

// ============================================================
// 数据记录结构
// ============================================================

/// 用户数据库记录
struct UserRecord {
    UserId id;                       ///< 用户 ID
    std::string username;            ///< 用户名
    std::string password_hash;       ///< Argon2id 哈希字符串（含参数编码）
    std::string argon2_params;       ///< Argon2 参数（m=65536,t=3,p=2 格式）
    GroupId group_id{3};             ///< 权限组 ID，默认 User 组
    int64_t created_at = 0;          ///< 创建时间戳（epoch 秒）
    int64_t last_login = 0;          ///< 最后登录时间戳（epoch 秒）
    std::vector<uint8_t> public_key; ///< 客户端 Curve25519 公钥（32 字节）
};

/// 频道数据库记录
struct ChannelRecord {
    ChannelId id;                    ///< 频道 ID
    std::string name;                ///< 频道名称
    ChannelId parent_id;             ///< 父频道 ID（0 表示根频道）
    UserId created_by;               ///< 创建者 ID
    bool is_permanent = true;        ///< 是否永久频道
    int64_t created_at = 0;          ///< 创建时间戳（epoch 秒）
};

/// 封禁记录
struct BanRecord {
    uint64_t id = 0;                 ///< 封禁记录 ID
    UserId user_id;                  ///< 被封禁用户 ID
    std::string ip_address;          ///< 被封禁 IP 地址
    std::string reason;              ///< 封禁原因
    int64_t expires_at = 0;          ///< 过期时间戳（0 表示永久封禁）
};

// ============================================================
// Database 类
// ============================================================

/**
 * @class Database
 * @brief SQLite3 数据库封装
 *
 * RAII 管理 sqlite3 连接生命周期，析构时自动关闭数据库。
 * 使用 WAL 模式提升并发读写性能。
 */
class Database {
public:
    /// 构造函数
    Database();

    /// 析构函数：关闭数据库连接（RAII）
    ~Database();

    // 禁止拷贝和移动（持有 sqlite3* 和 mutex）
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    // ============================================================
    // 初始化
    // ============================================================

    /**
     * @brief 初始化数据库
     *
     * 打开 SQLite3 数据库文件，设置 WAL 模式，创建缺失的表。
     * 如果数据库文件不存在，将自动创建。
     *
     * @param db_path 数据库文件路径
     * @return Result<void> 成功或错误信息
     */
    Result<void> initialize(const std::string& db_path);

    /**
     * @brief 检查数据库是否已初始化
     * @return true 表示数据库已成功打开并初始化
     */
    bool isInitialized() const;

    // ============================================================
    // 用户管理
    // ============================================================

    /**
     * @brief 创建新用户
     *
     * 使用 Argon2id 对密码进行哈希后存入数据库。
     * Argon2id 参数：m=65536（64MB 内存），t=3（3 次迭代），p=2（2 并行度）。
     *
     * @param username       用户名（需唯一）
     * @param auth_credential 认证凭证（明文密码）
     * @return Result<UserId> 新用户的 ID，或错误
     */
    Result<UserId> createUser(const std::string& username,
                              const std::string& auth_credential);

    /**
     * @brief 验证用户凭证
     *
     * 从数据库读取 Argon2id 哈希，使用 argon2id_verify 验证密码。
     * 验证成功时更新 last_login 时间戳。
     *
     * @param username        用户名
     * @param auth_credential 待验证的凭证
     * @return Result<UserId> 验证成功返回用户 ID，失败返回 AuthFailed 错误
     */
    Result<UserId> verifyUser(const std::string& username,
                              const std::string& auth_credential);

    /**
     * @brief 根据 ID 获取用户记录
     * @param userId 用户 ID
     * @return 用户记录，不存在返回 std::nullopt
     */
    std::optional<UserRecord> getUser(UserId userId);

    /**
     * @brief 根据用户名获取用户记录
     * @param username 用户名
     * @return 用户记录，不存在返回 std::nullopt
     */
    std::optional<UserRecord> getUserByName(const std::string& username);

    /**
     * @brief 更新用户公钥
     *
     * 客户端登录时上传其 Curve25519 公钥，服务端存储用于后续
     * 会话密钥的加密下发。
     *
     * @param user_id    用户 ID
     * @param public_key 公钥字节（应为 32 字节）
     * @return Result<void> 成功或错误
     */
    Result<void> updateUserPublicKey(UserId user_id,
                                     const std::vector<uint8_t>& public_key);

    /**
     * @brief 更新用户权限组
     * @param user_id  用户 ID
     * @param group_id 新的权限组 ID
     * @return Result<void> 成功或错误
     */
    Result<void> updateUserGroupId(UserId user_id, GroupId group_id);

    // ============================================================
    // 频道管理
    // ============================================================

    /**
     * @brief 创建新频道
     *
     * @param name       频道名称
     * @param parent_id  父频道 ID（0 表示根频道）
     * @param created_by 创建者用户 ID
     * @return Result<ChannelId> 新频道的 ID，或错误
     */
    Result<ChannelId> createChannel(const std::string& name,
                                    ChannelId parent_id,
                                    UserId created_by);

    /**
     * @brief 删除频道
     *
     * 仅删除指定频道记录，不递归删除子频道（子频道由 ChannelManager 协调）。
     *
     * @param id 频道 ID
     * @return Result<void> 成功或错误
     */
    Result<void> deleteChannel(ChannelId id);

    /**
     * @brief 根据 ID 获取频道记录
     * @param id 频道 ID
     * @return 频道记录，不存在返回 std::nullopt
     */
    std::optional<ChannelRecord> getChannel(ChannelId id);

    /**
     * @brief 获取指定父频道下的所有子频道
     * @param parent_id 父频道 ID
     * @return 子频道记录列表
     */
    std::vector<ChannelRecord> getChannelsByParent(ChannelId parent_id);

    /**
     * @brief 重命名频道
     *
     * 更新指定频道的名称。
     *
     * @param id       频道 ID
     * @param new_name 新频道名称
     * @return Result<void> 成功或错误
     */
    Result<void> renameChannel(ChannelId id, const std::string& new_name);

    // ============================================================
    // 服务器配置
    // ============================================================

    /**
     * @brief 获取配置值
     * @param key 配置键
     * @return 配置值，不存在返回 std::nullopt
     */
    std::optional<std::string> getConfig(const std::string& key);

    /**
     * @brief 设置配置值（INSERT OR REPLACE）
     * @param key   配置键
     * @param value 配置值
     * @return Result<void> 成功或错误
     */
    Result<void> setConfig(const std::string& key, const std::string& value);

    // ============================================================
    // 封禁管理
    // ============================================================

    /**
     * @brief 添加封禁记录
     * @param user_id    被封禁用户 ID（可为 0 表示仅 IP 封禁）
     * @param ip_address 被封禁 IP 地址（可为空表示仅用户封禁）
     * @param reason     封禁原因
     * @param expires_at 过期时间戳（0 表示永久封禁）
     * @return Result<void> 成功或错误
     */
    Result<void> addBan(UserId user_id, const std::string& ip_address,
                        const std::string& reason, int64_t expires_at);

    /**
     * @brief 检查用户是否被封禁
     *
     * 同时检查用户 ID 和 IP 地址。如果任一匹配且未过期，返回 true。
     *
     * @param user_id    用户 ID
     * @param ip_address IP 地址
     * @return true 表示用户已被封禁
     */
    bool isBanned(UserId user_id, const std::string& ip_address = "");

private:
    // ============================================================
    // 内部方法
    // ============================================================

    /**
     * @brief 创建数据库表（如果不存在）
     * @return Result<void> 成功或错误
     */
    Result<void> createTables();

    /**
     * @brief 设置 WAL 模式
     * @return Result<void> 成功或错误
     */
    Result<void> setWalMode();

    /**
     * @brief 使用 Argon2id 对密码进行哈希
     * @param password 明文密码
     * @return 哈希后的编码字符串（含盐值和参数）
     */
    static std::string hashPassword(const std::string& password);

    /**
     * @brief 使用 Argon2id 验证密码
     * @param hash     数据库中存储的哈希字符串
     * @param password 待验证的明文密码
     * @return true 表示密码匹配
     */
    static bool verifyPassword(const std::string& hash, const std::string& password);

    /**
     * @brief 执行 SQL 语句（无返回值）
     * @param sql SQL 语句
     * @return Result<void> 成功或错误
     */
    Result<void> executeSql(const std::string& sql);

    // ============================================================
    // 成员变量
    // ============================================================

    /// SQLite3 数据库连接句柄
    sqlite3* db_ = nullptr;

    /// 互斥锁，保证线程安全
    std::mutex mutex_;

    /// 是否已初始化
    bool initialized_ = false;
};

} // namespace nevo
