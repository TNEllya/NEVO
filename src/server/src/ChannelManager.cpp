/**
 * @file ChannelManager.cpp
 * @brief 树状频道管理器实现
 *
 * 管理频道树的创建、删除和用户移动操作。
 * 频道树在内存中维护，通过 Database 持久化。
 */

#include "nevo/server/ChannelManager.h"
#include "nevo/core/common/Logger.h"

namespace nevo {

// ============================================================
// 构造函数
// ============================================================

ChannelManager::ChannelManager(std::shared_ptr<Database> db)
    : db_(std::move(db))
{
}

// ============================================================
// 初始化
// ============================================================

Result<void> ChannelManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    NEVO_LOG_INFO("server", "Initializing ChannelManager");

    // 从数据库加载频道
    auto load_result = loadChannelsFromDb();
    if (!load_result) {
        return load_result;
    }

    // 如果数据库中没有任何频道，创建默认频道结构
    if (channels_.empty()) {
        NEVO_LOG_INFO("server", "No channels found, creating default channel structure");

        // 创建根频道 "Root"
        auto root = std::make_unique<Channel>(ChannelId(1), "Root", nullptr);
        root_channel_ = root.get();
        channels_[ChannelId(1)] = std::move(root);
        next_channel_id_ = ChannelId(2);

        // 创建默认频道 "Lobby"
        auto lobby = std::make_unique<Channel>(ChannelId(2), "Lobby", root_channel_);
        default_channel_ = lobby.get();
        root_channel_->addChild(default_channel_);
        channels_[ChannelId(2)] = std::move(lobby);
        next_channel_id_ = ChannelId(3);

        // 持久化到数据库
        if (db_) {
            auto r1 = db_->createChannel("Root", ChannelId(0), UserId(0));
            if (!r1) {
                NEVO_LOG_ERROR("server", "Failed to create root channel in DB: {}", r1.error().message());
            }
            auto r2 = db_->createChannel("Lobby", ChannelId(1), UserId(0));
            if (!r2) {
                NEVO_LOG_ERROR("server", "Failed to create lobby channel in DB: {}", r2.error().message());
            }
        }
    }

    NEVO_LOG_INFO("server", "ChannelManager initialized with {} channels", channels_.size());
    return Ok();
}

// ============================================================
// 频道操作
// ============================================================

Result<ChannelId> ChannelManager::createChannel(ChannelId parent_id,
                                                 const std::string& name,
                                                 UserId created_by) {
    std::lock_guard<std::mutex> lock(mutex_);

    NEVO_LOG_INFO("server", "Creating channel: '{}' under parent_id={}", name, parent_id.value);

    // 查找父频道
    Channel* parent = findParentChannel(parent_id);
    if (!parent) {
        NEVO_LOG_ERROR("server", "Parent channel not found: {}", parent_id.value);
        return Err<ChannelId>(ResultCode::ChannelNotFound, "Parent channel not found");
    }

    // 分配新频道 ID
    ChannelId new_id = next_channel_id_;
    next_channel_id_ = ChannelId(next_channel_id_.value + 1);

    // 创建频道对象并添加到树
    auto channel = std::make_unique<Channel>(new_id, name, parent);
    Channel* channel_ptr = channel.get();
    parent->addChild(channel_ptr);
    channels_[new_id] = std::move(channel);

    // 持久化到数据库
    if (db_) {
        auto db_result = db_->createChannel(name, parent->id(), created_by);
        if (!db_result) {
            // 回滚内存操作
            parent->removeChild(channel_ptr);
            channels_.erase(new_id);
            next_channel_id_ = ChannelId(next_channel_id_.value - 1);
            NEVO_LOG_ERROR("server", "Failed to persist channel to DB: {}", db_result.error().message());
            return Err<ChannelId>(db_result.error().code(), db_result.error().message());
        }
    }

    NEVO_LOG_INFO("server", "Channel created: '{}' (id={})", name, new_id.value);
    return Ok(new_id);
}

Result<void> ChannelManager::deleteChannel(ChannelId id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 不允许删除根频道和默认频道
    if (id == root_channel_->id()) {
        return Err<void>(ResultCode::PermissionDenied, "Cannot delete root channel");
    }
    if (default_channel_ && id == default_channel_->id()) {
        return Err<void>(ResultCode::PermissionDenied, "Cannot delete default channel (Lobby)");
    }

    // 查找频道
    auto it = channels_.find(id);
    if (it == channels_.end()) {
        return Err<void>(ResultCode::ChannelNotFound, "Channel not found");
    }

    Channel* channel = it->second.get();
    NEVO_LOG_INFO("server", "Deleting channel: '{}' (id={})", channel->name(), id.value);

    // 递归删除（将用户移至默认频道，删除所有子频道）
    deleteChannelRecursive(channel);

    NEVO_LOG_INFO("server", "Channel deleted: id={}", id.value);
    return Ok();
}

Result<void> ChannelManager::renameChannel(ChannelId id, const std::string& new_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 查找频道
    auto it = channels_.find(id);
    if (it == channels_.end()) {
        return Err<void>(ResultCode::ChannelNotFound, "Channel not found");
    }

    Channel* channel = it->second.get();
    NEVO_LOG_INFO("server", "Renaming channel: '{}' -> '{}' (id={})", channel->name(), new_name, id.value);

    // 持久化到数据库
    if (db_) {
        auto db_result = db_->renameChannel(id, new_name);
        if (!db_result) {
            NEVO_LOG_ERROR("server", "Failed to rename channel in DB: {}", db_result.error().message());
            return db_result;
        }
    }

    // 更新内存中的名称
    channel->setName(new_name);

    NEVO_LOG_INFO("server", "Channel renamed: id={} to '{}'", id.value, new_name);
    return Ok();
}

Result<void> ChannelManager::moveUserToChannel(UserId user_id, ChannelId channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 查找目标频道
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) {
        return Err<void>(ResultCode::ChannelNotFound, "Target channel not found");
    }

    Channel* target_channel = it->second.get();

    // 从旧频道移除
    auto old_it = user_channel_map_.find(user_id);
    if (old_it != user_channel_map_.end()) {
        auto old_channel_it = channels_.find(old_it->second);
        if (old_channel_it != channels_.end()) {
            old_channel_it->second->removeUser(user_id);
        }
    }

    // 添加到新频道
    target_channel->addUser(user_id);
    user_channel_map_[user_id] = channel_id;

    NEVO_LOG_DEBUG("server", "User {} moved to channel '{}' ({})",
                   user_id.value, target_channel->name(), channel_id.value);
    return Ok();
}

void ChannelManager::removeUserFromChannel(UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = user_channel_map_.find(user_id);
    if (it != user_channel_map_.end()) {
        auto channel_it = channels_.find(it->second);
        if (channel_it != channels_.end()) {
            channel_it->second->removeUser(user_id);
        }
        user_channel_map_.erase(it);
        NEVO_LOG_DEBUG("server", "User {} removed from channel", user_id.value);
    }
}

// ============================================================
// 查询
// ============================================================

Channel* ChannelManager::getChannel(ChannelId id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = channels_.find(id);
    return it != channels_.end() ? it->second.get() : nullptr;
}

Channel* ChannelManager::getRootChannel() {
    std::lock_guard<std::mutex> lock(mutex_);
    return root_channel_;
}

Channel* ChannelManager::getDefaultChannel() {
    std::lock_guard<std::mutex> lock(mutex_);
    return default_channel_;
}

Channel* ChannelManager::getUserChannel(UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = user_channel_map_.find(user_id);
    if (it == user_channel_map_.end()) {
        return nullptr;
    }

    auto channel_it = channels_.find(it->second);
    return channel_it != channels_.end() ? channel_it->second.get() : nullptr;
}

std::vector<ChannelWithUsers> ChannelManager::getChannelsWithUsers() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ChannelWithUsers> result;
    result.reserve(channels_.size());

    for (const auto& [id, channel] : channels_) {
        ChannelWithUsers cwu;
        cwu.channel_id = channel->id();
        cwu.channel_name = channel->name();
        cwu.parent_id = channel->parent() ? channel->parent()->id() : ChannelId(0);
        cwu.user_ids = channel->users();
        result.push_back(std::move(cwu));
    }

    return result;
}

std::vector<Channel*> ChannelManager::getAllChannels() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Channel*> result;
    result.reserve(channels_.size());

    for (const auto& [id, channel] : channels_) {
        result.push_back(channel.get());
    }

    return result;
}

// ============================================================
// 内部方法
// ============================================================

void ChannelManager::deleteChannelRecursive(Channel* channel) {
    if (!channel) return;

    // 先递归删除所有子频道（拷贝 children 列表，因为删除会修改列表）
    auto children = channel->children();
    for (Channel* child : children) {
        deleteChannelRecursive(child);
    }

    // 将此频道中的所有用户移至默认频道
    auto users = channel->users();
    for (UserId uid : users) {
        if (default_channel_) {
            default_channel_->addUser(uid);
            user_channel_map_[uid] = default_channel_->id();
        } else {
            user_channel_map_.erase(uid);
        }
    }

    // 从父频道的子列表中移除
    if (channel->parent()) {
        channel->parent()->removeChild(channel);
    }

    // 从数据库删除
    if (db_) {
        auto result = db_->deleteChannel(channel->id());
        if (!result) {
            // DB deletion failed but in-memory state is already partially modified
            // (users moved, parent-child link severed). Continuing with in-memory
            // deletion means the channel will reappear on server restart — a
            // self-healing tradeoff that is preferable to leaving the tree
            // in an inconsistent half-deleted state.
            NEVO_LOG_ERROR("server", "Failed to delete channel from DB: id={} — "
                           "channel will reappear on restart", channel->id().value);
        }
    }

    // 从内存映射中移除
    channels_.erase(channel->id());
}

Result<void> ChannelManager::loadChannelsFromDb() {
    if (!db_) {
        NEVO_LOG_WARN("server", "No database available, skipping channel load");
        return Ok();
    }

    // 加载根频道（parent_id = 0）
    auto root_records = db_->getChannelsByParent(ChannelId(0));
    if (root_records.empty()) {
        // 数据库中没有频道，将由 initialize() 创建默认结构
        return Ok();
    }

    // 取第一个根频道作为服务器根频道
    // 通常只有一个根频道
    ChannelId max_id(0);

    // 递归构建频道树
    // 首先创建所有频道对象（不带 parent 指针）
    std::unordered_map<ChannelId, ChannelRecord> all_records;

    // 使用 BFS 遍历所有频道
    std::vector<ChannelId> to_process;
    for (const auto& rec : root_records) {
        to_process.push_back(rec.id);
    }

    while (!to_process.empty()) {
        ChannelId current_id = to_process.back();
        to_process.pop_back();

        auto record = db_->getChannel(current_id);
        if (!record) continue;

        all_records[current_id] = *record;

        // 添加子频道到待处理列表
        auto children = db_->getChannelsByParent(current_id);
        for (const auto& child : children) {
            to_process.push_back(child.id);
        }
    }

    // 构建频道对象
    for (auto& [id, record] : all_records) {
        Channel* parent_ptr = nullptr;
        if (record.parent_id.value != 0) {
            auto parent_it = channels_.find(record.parent_id);
            if (parent_it != channels_.end()) {
                parent_ptr = parent_it->second.get();
            }
        }

        auto channel = std::make_unique<Channel>(record.id, record.name, parent_ptr);
        Channel* raw_ptr = channel.get();

        if (parent_ptr) {
            parent_ptr->addChild(raw_ptr);
        }

        // 识别根频道和默认频道
        if (record.parent_id.value == 0 && !root_channel_) {
            root_channel_ = raw_ptr;
        }
        if (record.name == "Lobby" && !default_channel_) {
            default_channel_ = raw_ptr;
        }

        channels_[record.id] = std::move(channel);

        // 更新下一个可用 ID
        if (record.id.value >= max_id.value) {
            max_id = ChannelId(record.id.value + 1);
        }
    }

    next_channel_id_ = max_id.value > 0 ? max_id : ChannelId(1);

    NEVO_LOG_INFO("server", "Loaded {} channels from database", channels_.size());
    return Ok();
}

Channel* ChannelManager::findParentChannel(ChannelId parent_id) {
    if (!parent_id || parent_id.value == 0) {
        return root_channel_;
    }

    auto it = channels_.find(parent_id);
    return it != channels_.end() ? it->second.get() : root_channel_;
}

} // namespace nevo
