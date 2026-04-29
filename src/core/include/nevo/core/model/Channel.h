#pragma once
/**
 * @file Channel.h
 * @brief 树状频道模型
 *
 * 支持多层级嵌套频道。Channel 对象由 ChannelManager 拥有，
 * 外部通过原始指针访问（生命周期由 ChannelManager 保证）。
 */

#include "nevo/core/common/Types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace nevo {

/// 频道信息结构体，对应服务器下发的频道数据
struct ChannelInfo {
    ChannelId channel_id;           ///< 频道唯一标识
    std::string name;               ///< 频道名称
    ChannelId parent_id;            ///< 父频道 ID（0 表示根频道）
    bool is_permanent = true;       ///< 是否为永久频道
    std::vector<UserId> user_ids;   ///< 频道中的用户列表

    ChannelInfo() = default;
    ChannelInfo(ChannelId id, std::string n, ChannelId pid = ChannelId(0),
                bool permanent = true)
        : channel_id(id), name(std::move(n)), parent_id(pid),
          is_permanent(permanent) {}
};

/// 频道模型
class Channel {
public:
    Channel(ChannelId id, const std::string& name, Channel* parent = nullptr);

    // --- 只读访问器 ---
    ChannelId id() const { return id_; }
    const std::string& name() const { return name_; }
    Channel* parent() const { return parent_; }
    const std::vector<Channel*>& children() const { return children_; }
    const std::vector<UserId>& users() const { return users_; }
    bool isPermanent() const { return is_permanent_; }

    // --- 子频道管理 ---
    void addChild(Channel* child);
    void removeChild(Channel* child);
    /// 递归查找子频道
    Channel* findChild(ChannelId cid) const;

    // --- 用户管理 ---
    void addUser(UserId uid);
    void removeUser(UserId uid);
    bool hasUser(UserId uid) const;

    // --- 属性设置 ---
    void setName(const std::string& name) { name_ = name; }
    void setPermanent(bool permanent) { is_permanent_ = permanent; }

private:
    ChannelId id_;
    std::string name_;
    Channel* parent_;
    std::vector<Channel*> children_;
    std::vector<UserId> users_;
    bool is_permanent_ = true;
};

} // namespace nevo
