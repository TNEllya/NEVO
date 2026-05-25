#pragma once
/**
 * @file Types.h
 * @brief NEVO 系统基础类型定义
 *
 * 使用强类型别名区分不同 ID，避免混淆。
 * 所有 ID 使用 uint64_t，0 表示无效值。
 */

#include <cstdint>
#include <functional>
#include <string>

namespace nevo {

// ============================================================
// 强类型 ID：避免将 UserId 当作 ChannelId 传递
// ============================================================

/// 用户唯一标识
struct UserId {
    uint64_t value = 0;

    constexpr UserId() = default;
    constexpr explicit UserId(uint64_t v) : value(v) {}

    constexpr bool operator==(const UserId& other) const { return value == other.value; }
    constexpr bool operator!=(const UserId& other) const { return value != other.value; }
    constexpr bool operator<(const UserId& other) const { return value < other.value; }

    constexpr explicit operator bool() const { return value != 0; }
};

/// 频道唯一标识
struct ChannelId {
    uint64_t value = 0;

    constexpr ChannelId() = default;
    constexpr explicit ChannelId(uint64_t v) : value(v) {}

    constexpr bool operator==(const ChannelId& other) const { return value == other.value; }
    constexpr bool operator!=(const ChannelId& other) const { return value != other.value; }
    constexpr bool operator<(const ChannelId& other) const { return value < other.value; }

    constexpr explicit operator bool() const { return value != 0; }
};

/// 会话唯一标识（登录后分配）
struct SessionId {
    uint64_t value = 0;

    constexpr SessionId() = default;
    constexpr explicit SessionId(uint64_t v) : value(v) {}

    constexpr bool operator==(const SessionId& other) const { return value == other.value; }
    constexpr bool operator!=(const SessionId& other) const { return value != other.value; }

    constexpr explicit operator bool() const { return value != 0; }
};

/// 权限组标识
struct GroupId {
    uint32_t value = 0;

    constexpr GroupId() = default;
    constexpr explicit GroupId(uint32_t v) : value(v) {}

    constexpr bool operator==(const GroupId& other) const { return value == other.value; }
    constexpr bool operator!=(const GroupId& other) const { return value != other.value; }
};

// ============================================================
// 无效 ID 常量
// ============================================================
inline constexpr UserId INVALID_USER_ID{};
inline constexpr ChannelId INVALID_CHANNEL_ID{};
inline constexpr SessionId INVALID_SESSION_ID{};
inline constexpr GroupId INVALID_GROUP_ID{};

// ============================================================
// UserId / ChannelId 的哈希支持（用于 unordered_map）
// ============================================================

} // namespace nevo

// std::hash 特化
template<>
struct std::hash<nevo::UserId> {
    size_t operator()(const nevo::UserId& id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};

template<>
struct std::hash<nevo::ChannelId> {
    size_t operator()(const nevo::ChannelId& id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};

template<>
struct std::hash<nevo::SessionId> {
    size_t operator()(const nevo::SessionId& id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};

template <>
struct std::hash<nevo::GroupId> {
    size_t operator()(nevo::GroupId id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};
