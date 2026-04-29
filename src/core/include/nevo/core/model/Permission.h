#pragma once
/**
 * @file Permission.h
 * @brief 基于组的权限系统
 *
 * 使用位掩码实现细粒度权限控制。
 * 每个用户属于一个权限组，权限组定义了允许的操作集合。
 */

#include "nevo/core/common/Types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace nevo {

// ============================================================
// 权限位掩码枚举
// ============================================================
enum class Permission : uint64_t {
    JoinChannel      = 1ULL << 0,   // 加入频道
    Speak            = 1ULL << 1,   // 说话
    CreateChannel    = 1ULL << 2,   // 创建频道
    DeleteChannel    = 1ULL << 3,   // 删除频道
    KickUser         = 1ULL << 4,   // 踢出用户
    MoveUser         = 1ULL << 5,   // 移动用户到其他频道
    MuteUser         = 1ULL << 6,   // 静音用户
    ManagePermission = 1ULL << 7,   // 管理权限
    ServerAdmin      = 1ULL << 8,   // 服务器管理
    BanUser          = 1ULL << 9,   // 封禁用户
    TextChat         = 1ULL << 10,  // 文字聊天
    Whisper          = 1ULL << 11,  // 私语（跨频道说话）
};

// ============================================================
// 权限组定义
// ============================================================

/// 预定义权限组 ID
inline constexpr GroupId GROUP_ADMIN{1};
inline constexpr GroupId GROUP_CHANNEL_ADMIN{2};
inline constexpr GroupId GROUP_USER{3};
inline constexpr GroupId GROUP_GUEST{4};

/// 权限组结构
struct PermissionGroup {
    GroupId id;
    std::string name;
    uint64_t permissions;   // 位掩码：Permission 值的按位或

    /// 检查是否拥有指定权限
    bool hasPermission(Permission perm) const {
        return (permissions & static_cast<uint64_t>(perm)) != 0;
    }

    /// 添加权限
    void grant(Permission perm) {
        permissions |= static_cast<uint64_t>(perm);
    }

    /// 移除权限
    void revoke(Permission perm) {
        permissions &= ~static_cast<uint64_t>(perm);
    }
};

// ============================================================
// 权限管理器
// ============================================================
class PermissionManager {
public:
    PermissionManager();

    /// 获取权限组
    const PermissionGroup* getGroup(GroupId gid) const;

    /// 检查用户是否拥有指定权限
    bool hasPermission(GroupId gid, Permission perm) const;

    /// 添加自定义权限组
    void addGroup(const PermissionGroup& group);

    /// 修改权限组的权限
    void grantPermission(GroupId gid, Permission perm);
    void revokePermission(GroupId gid, Permission perm);

private:
    /// 初始化默认权限组
    void initDefaultGroups();

    std::vector<PermissionGroup> groups_;
};

} // namespace nevo
