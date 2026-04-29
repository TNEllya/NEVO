#pragma once
/**
 * @file ChannelManager.h
 * @brief 树状频道管理器
 *
 * 管理服务端的频道树结构，包括频道的创建、删除和用户在频道间的移动。
 * 频道采用树状层级结构，每个频道可有多个子频道，根频道为服务器默认频道。
 *
 * 数据一致性说明：
 *   - 频道的创建/删除操作同时修改内存中的频道树和持久化到数据库
 *   - 用户移动操作仅修改内存状态（用户状态不持久化）
 *
 * 使用方式：
 * @code
 *   auto db = std::make_shared<Database>();
 *   db->initialize("server.db");
 *   ChannelManager mgr(db);
 *   mgr.initialize();  // 从数据库加载频道
 *   mgr.createChannel(ChannelId(0), "General", UserId(1));
 * @endcode
 */

#include "nevo/core/common/Types.h"
#include "nevo/core/common/Result.h"
#include "nevo/core/model/Channel.h"
#include "nevo/core/model/User.h"
#include "nevo/server/Database.h"

#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>

namespace nevo {

// ============================================================
// 频道树节点信息（用于广播）
// ============================================================

/// 频道及其中用户的信息（用于向客户端发送频道列表）
struct ChannelWithUsers {
    ChannelId channel_id;            ///< 频道 ID
    std::string channel_name;        ///< 频道名称
    ChannelId parent_id;             ///< 父频道 ID
    std::vector<UserId> user_ids;    ///< 频道中的用户列表
};

// ============================================================
// ChannelManager 类
// ============================================================

/**
 * @class ChannelManager
 * @brief 树状频道管理器
 *
 * 维护频道树的内存结构，并与数据库保持同步。
 * 所有公开方法线程安全（内部 mutex 保护）。
 */
class ChannelManager {
public:
    /**
     * @brief 构造函数
     * @param db 数据库指针（需已初始化）
     */
    explicit ChannelManager(std::shared_ptr<Database> db);

    /// 析构函数
    ~ChannelManager() = default;

    // 禁止拷贝
    ChannelManager(const ChannelManager&) = delete;
    ChannelManager& operator=(const ChannelManager&) = delete;

    // ============================================================
    // 初始化
    // ============================================================

    /**
     * @brief 初始化频道管理器
     *
     * 从数据库加载已有频道，构建内存中的频道树。
     * 如果数据库中没有任何频道，创建根频道 "Root" 和默认频道 "Lobby"。
     *
     * @return Result<void> 成功或错误
     */
    Result<void> initialize();

    // ============================================================
    // 频道操作
    // ============================================================

    /**
     * @brief 创建新频道
     *
     * 在指定父频道下创建新频道，同时更新内存树和数据库。
     *
     * @param parent_id  父频道 ID（ChannelId(0) 表示作为根频道的子频道）
     * @param name       频道名称
     * @param created_by 创建者用户 ID
     * @return Result<ChannelId> 新频道的 ID，或错误
     */
    Result<ChannelId> createChannel(ChannelId parent_id,
                                    const std::string& name,
                                    UserId created_by);

    /**
     * @brief 删除频道
     *
     * 递归删除指定频道及其所有子频道，同时将频道中的用户移至默认频道。
     * 从内存树和数据库中同时移除。
     * 注意：不能删除根频道和默认频道（Lobby）。
     *
     * @param id 要删除的频道 ID
     * @return Result<void> 成功或错误
     */
    Result<void> deleteChannel(ChannelId id);

    /**
     * @brief 重命名频道
     *
     * 修改指定频道的名称，同时更新内存树和数据库。
     * 注意：不能重命名根频道。
     *
     * @param id       频道 ID
     * @param new_name 新频道名称
     * @return Result<void> 成功或错误
     */
    Result<void> renameChannel(ChannelId id, const std::string& new_name);

    /**
     * @brief 移动用户到指定频道
     *
     * 将用户从当前所在频道移除，添加到目标频道。
     * 同时更新 User 对象的 currentChannel。
     *
     * @param user_id    用户 ID
     * @param channel_id 目标频道 ID
     * @return Result<void> 成功或错误
     */
    Result<void> moveUserToChannel(UserId user_id, ChannelId channel_id);

    /**
     * @brief 从频道中移除用户（不加入新频道）
     *
     * 用于用户断开连接时清理频道状态。
     *
     * @param user_id 用户 ID
     */
    void removeUserFromChannel(UserId user_id);

    // ============================================================
    // 查询
    // ============================================================

    /**
     * @brief 获取频道指针
     * @param id 频道 ID
     * @return 频道指针，不存在返回 nullptr
     */
    Channel* getChannel(ChannelId id);

    /**
     * @brief 获取根频道
     * @return 根频道指针，始终非空（初始化后）
     */
    Channel* getRootChannel();

    /**
     * @brief 获取默认频道（Lobby）
     *
     * 新用户连接后默认加入此频道。
     *
     * @return 默认频道指针，始终非空（初始化后）
     */
    Channel* getDefaultChannel();

    /**
     * @brief 查找用户当前所在的频道
     * @param user_id 用户 ID
     * @return 频道指针，用户不在线返回 nullptr
     */
    Channel* getUserChannel(UserId user_id);

    /**
     * @brief 获取所有频道及其用户信息
     *
     * 用于向客户端广播频道列表。
     *
     * @return 频道列表（含用户信息）
     */
    std::vector<ChannelWithUsers> getChannelsWithUsers();

    /**
     * @brief 获取所有频道（仅频道信息，不含用户）
     * @return 所有频道的指针列表
     */
    std::vector<Channel*> getAllChannels();

private:
    // ============================================================
    // 内部方法
    // ============================================================

    /**
     * @brief 递归删除频道及其子频道
     * @param channel 要删除的频道
     */
    void deleteChannelRecursive(Channel* channel);

    /**
     * @brief 从数据库加载频道并构建频道树
     * @return Result<void> 成功或错误
     */
    Result<void> loadChannelsFromDb();

    /**
     * @brief 查找新频道 ID 的父频道指针
     *
     * 如果 parent_id 为 0 或无效，返回根频道作为父频道。
     *
     * @param parent_id 父频道 ID
     * @return 父频道指针，找不到返回根频道
     */
    Channel* findParentChannel(ChannelId parent_id);

    // ============================================================
    // 成员变量
    // ============================================================

    /// 数据库指针
    std::shared_ptr<Database> db_;

    /// 频道 ID -> Channel 对象（拥有所有权）
    std::unordered_map<ChannelId, std::unique_ptr<Channel>> channels_;

    /// 用户 ID -> 所在频道 ID 的映射
    std::unordered_map<UserId, ChannelId> user_channel_map_;

    /// 根频道指针（始终存在）
    Channel* root_channel_ = nullptr;

    /// 默认频道指针（Lobby，始终存在）
    Channel* default_channel_ = nullptr;

    /// 下一个频道 ID（用于在内存中创建频道后同步数据库）
    ChannelId next_channel_id_{1};

    /// 互斥锁
    std::mutex mutex_;
};

} // namespace nevo
