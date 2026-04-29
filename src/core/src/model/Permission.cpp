/**
 * @file Permission.cpp
 * @brief 权限系统实现
 */

#include "nevo/core/model/Permission.h"
#include "nevo/core/common/Logger.h"
#include <algorithm>

namespace nevo {

PermissionManager::PermissionManager() {
    initDefaultGroups();
}

void PermissionManager::initDefaultGroups() {
    // Admin: 所有权限
    PermissionGroup admin;
    admin.id = GROUP_ADMIN;
    admin.name = "Admin";
    admin.permissions = static_cast<uint64_t>(Permission::JoinChannel)
                      | static_cast<uint64_t>(Permission::Speak)
                      | static_cast<uint64_t>(Permission::CreateChannel)
                      | static_cast<uint64_t>(Permission::DeleteChannel)
                      | static_cast<uint64_t>(Permission::KickUser)
                      | static_cast<uint64_t>(Permission::MoveUser)
                      | static_cast<uint64_t>(Permission::MuteUser)
                      | static_cast<uint64_t>(Permission::ManagePermission)
                      | static_cast<uint64_t>(Permission::ServerAdmin)
                      | static_cast<uint64_t>(Permission::BanUser)
                      | static_cast<uint64_t>(Permission::TextChat)
                      | static_cast<uint64_t>(Permission::Whisper);
    groups_.push_back(admin);

    // Channel Admin: 管理自己创建的频道
    PermissionGroup channel_admin;
    channel_admin.id = GROUP_CHANNEL_ADMIN;
    channel_admin.name = "Channel Admin";
    channel_admin.permissions = static_cast<uint64_t>(Permission::JoinChannel)
                              | static_cast<uint64_t>(Permission::Speak)
                              | static_cast<uint64_t>(Permission::CreateChannel)
                              | static_cast<uint64_t>(Permission::DeleteChannel)
                              | static_cast<uint64_t>(Permission::KickUser)
                              | static_cast<uint64_t>(Permission::MuteUser)
                              | static_cast<uint64_t>(Permission::TextChat);
    groups_.push_back(channel_admin);

    // User: 基本权限
    PermissionGroup user;
    user.id = GROUP_USER;
    user.name = "User";
    user.permissions = static_cast<uint64_t>(Permission::JoinChannel)
                     | static_cast<uint64_t>(Permission::Speak)
                     | static_cast<uint64_t>(Permission::CreateChannel)
                     | static_cast<uint64_t>(Permission::TextChat);
    groups_.push_back(user);

    // Guest: 只能听
    PermissionGroup guest;
    guest.id = GROUP_GUEST;
    guest.name = "Guest";
    guest.permissions = static_cast<uint64_t>(Permission::JoinChannel)
                      | static_cast<uint64_t>(Permission::TextChat);
    // 注意：没有 Speak 权限
    groups_.push_back(guest);

    NEVO_LOG_INFO("permission", "Initialized {} default permission groups", groups_.size());
}

const PermissionGroup* PermissionManager::getGroup(GroupId gid) const {
    for (const auto& g : groups_) {
        if (g.id == gid) {
            return &g;
        }
    }
    return nullptr;
}

bool PermissionManager::hasPermission(GroupId gid, Permission perm) const {
    auto* group = getGroup(gid);
    if (!group) {
        NEVO_LOG_WARN("permission", "Unknown group id: {}", gid.value);
        return false;
    }
    return group->hasPermission(perm);
}

void PermissionManager::addGroup(const PermissionGroup& group) {
    // 检查是否已存在
    for (auto& g : groups_) {
        if (g.id == group.id) {
            NEVO_LOG_WARN("permission", "Group {} already exists, updating", group.name);
            g = group;
            return;
        }
    }
    groups_.push_back(group);
    NEVO_LOG_INFO("permission", "Added permission group: {}", group.name);
}

void PermissionManager::grantPermission(GroupId gid, Permission perm) {
    for (auto& g : groups_) {
        if (g.id == gid) {
            g.grant(perm);
            return;
        }
    }
    NEVO_LOG_WARN("permission", "Cannot grant permission: group {} not found", gid.value);
}

void PermissionManager::revokePermission(GroupId gid, Permission perm) {
    for (auto& g : groups_) {
        if (g.id == gid) {
            g.revoke(perm);
            return;
        }
    }
    NEVO_LOG_WARN("permission", "Cannot revoke permission: group {} not found", gid.value);
}

} // namespace nevo
