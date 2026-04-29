#pragma once
/**
 * @file User.h
 * @brief 用户模型
 */

#include "nevo/core/common/Types.h"
#include <string>
#include <cstdint>

namespace nevo {

/// 用户在线状态
enum class UserStatus : uint8_t {
    Offline  = 0,
    Online   = 1,
    Away     = 2,
    Muted    = 3,   // 麦克风静音
    Deafened = 4,   // 耳机静音
};

/// 用户模型
class User {
public:
    User() = default;
    User(UserId id, const std::string& username, GroupId group_id = GroupId(3));

    // --- 访问器 ---
    UserId id() const { return id_; }
    const std::string& username() const { return username_; }
    UserStatus status() const { return status_; }
    bool isMuted() const { return muted_; }
    bool isDeafened() const { return deafened_; }
    GroupId groupId() const { return group_id_; }
    ChannelId currentChannel() const { return current_channel_; }
    bool isSpeaking() const { return speaking_; }

    // --- 状态修改 ---
    void setStatus(UserStatus status) { status_ = status; }
    void setMuted(bool muted) { muted_ = muted; }
    void setDeafened(bool deafened) { deafened_ = deafened; }
    void setCurrentChannel(ChannelId cid) { current_channel_ = cid; }
    void setSpeaking(bool speaking) { speaking_ = speaking; }
    void setGroupId(GroupId gid) { group_id_ = gid; }

private:
    UserId id_;
    std::string username_;
    UserStatus status_ = UserStatus::Offline;
    bool muted_ = false;
    bool deafened_ = false;
    bool speaking_ = false;
    GroupId group_id_{3};              // 默认 User 组
    ChannelId current_channel_;        // 当前所在频道
};

} // namespace nevo
