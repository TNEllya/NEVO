/**
 * @file TestUser.cpp
 * @brief Unit tests for User model
 * 
 * 覆盖缺口：User模型完全缺少测试
 * 风险等级：高 - User是核心领域模型，涉及用户状态管理
 */

#include <gtest/gtest.h>
#include "nevo/core/model/User.h"

namespace nevo {
namespace {

// ============================================================
// Construction and default state
// ============================================================

TEST(UserTest, DefaultConstruction) {
    User user;
    EXPECT_EQ(user.id(), UserId(0));
    EXPECT_EQ(user.username(), "");
    EXPECT_EQ(user.status(), UserStatus::Offline);
    EXPECT_FALSE(user.isMuted());
    EXPECT_FALSE(user.isDeafened());
    EXPECT_FALSE(user.isSpeaking());
    EXPECT_EQ(user.groupId(), GroupId(3)); // Default User group
    EXPECT_EQ(user.currentChannel(), ChannelId(0));
}

TEST(UserTest, ParameterizedConstruction) {
    User user(UserId(42), "testuser", GroupId(2));
    
    EXPECT_EQ(user.id(), UserId(42));
    EXPECT_EQ(user.username(), "testuser");
    EXPECT_EQ(user.status(), UserStatus::Online); // Constructor sets Online
    EXPECT_EQ(user.groupId(), GroupId(2));
    EXPECT_FALSE(user.isMuted());
    EXPECT_FALSE(user.isDeafened());
}

TEST(UserTest, ConstructionWithDefaultGroup) {
    User user(UserId(1), "defaultuser");
    
    EXPECT_EQ(user.id(), UserId(1));
    EXPECT_EQ(user.username(), "defaultuser");
    EXPECT_EQ(user.groupId(), GroupId(3)); // Default User group
    EXPECT_EQ(user.status(), UserStatus::Online);
}

// ============================================================
// Status management
// ============================================================

TEST(UserTest, SetStatus) {
    User user(UserId(1), "test");
    
    user.setStatus(UserStatus::Away);
    EXPECT_EQ(user.status(), UserStatus::Away);
    
    user.setStatus(UserStatus::Muted);
    EXPECT_EQ(user.status(), UserStatus::Muted);
    
    user.setStatus(UserStatus::Deafened);
    EXPECT_EQ(user.status(), UserStatus::Deafened);
    
    user.setStatus(UserStatus::Offline);
    EXPECT_EQ(user.status(), UserStatus::Offline);
    
    user.setStatus(UserStatus::Online);
    EXPECT_EQ(user.status(), UserStatus::Online);
}

// ============================================================
// Mute/Deafen state management
// ============================================================

TEST(UserTest, SetMuted) {
    User user(UserId(1), "test");
    
    EXPECT_FALSE(user.isMuted());
    
    user.setMuted(true);
    EXPECT_TRUE(user.isMuted());
    
    user.setMuted(false);
    EXPECT_FALSE(user.isMuted());
}

TEST(UserTest, SetDeafened) {
    User user(UserId(1), "test");
    
    EXPECT_FALSE(user.isDeafened());
    
    user.setDeafened(true);
    EXPECT_TRUE(user.isDeafened());
    
    user.setDeafened(false);
    EXPECT_FALSE(user.isDeafened());
}

TEST(UserTest, MuteAndDeafenAreIndependent) {
    User user(UserId(1), "test");
    
    user.setMuted(true);
    user.setDeafened(false);
    EXPECT_TRUE(user.isMuted());
    EXPECT_FALSE(user.isDeafened());
    
    user.setMuted(false);
    user.setDeafened(true);
    EXPECT_FALSE(user.isMuted());
    EXPECT_TRUE(user.isDeafened());
    
    user.setMuted(true);
    user.setDeafened(true);
    EXPECT_TRUE(user.isMuted());
    EXPECT_TRUE(user.isDeafened());
}

// ============================================================
// Speaking state
// ============================================================

TEST(UserTest, SetSpeaking) {
    User user(UserId(1), "test");
    
    EXPECT_FALSE(user.isSpeaking());
    
    user.setSpeaking(true);
    EXPECT_TRUE(user.isSpeaking());
    
    user.setSpeaking(false);
    EXPECT_FALSE(user.isSpeaking());
}

// ============================================================
// Channel management
// ============================================================

TEST(UserTest, SetCurrentChannel) {
    User user(UserId(1), "test");
    
    EXPECT_EQ(user.currentChannel(), ChannelId(0));
    
    user.setCurrentChannel(ChannelId(5));
    EXPECT_EQ(user.currentChannel(), ChannelId(5));
    
    user.setCurrentChannel(ChannelId(100));
    EXPECT_EQ(user.currentChannel(), ChannelId(100));
}

TEST(UserTest, ChannelSwitching) {
    User user(UserId(1), "test");
    
    // Simulate user joining channels
    user.setCurrentChannel(ChannelId(1));
    EXPECT_EQ(user.currentChannel(), ChannelId(1));
    
    user.setCurrentChannel(ChannelId(2));
    EXPECT_EQ(user.currentChannel(), ChannelId(2));
    
    // Leave channel
    user.setCurrentChannel(ChannelId(0));
    EXPECT_EQ(user.currentChannel(), ChannelId(0));
}

// ============================================================
// Group management
// ============================================================

TEST(UserTest, SetGroupId) {
    User user(UserId(1), "test");
    
    EXPECT_EQ(user.groupId(), GroupId(3)); // Default
    
    user.setGroupId(GroupId(1)); // Admin
    EXPECT_EQ(user.groupId(), GroupId(1));
    
    user.setGroupId(GroupId(4)); // Guest
    EXPECT_EQ(user.groupId(), GroupId(4));
}

// ============================================================
// Complex state scenarios
// ============================================================

TEST(UserTest, FullUserSessionLifecycle) {
    User user(UserId(100), "sessionuser");
    
    // User joins server
    EXPECT_EQ(user.status(), UserStatus::Online);
    EXPECT_FALSE(user.isMuted());
    EXPECT_FALSE(user.isDeafened());
    
    // User joins a channel
    user.setCurrentChannel(ChannelId(10));
    EXPECT_EQ(user.currentChannel(), ChannelId(10));
    
    // User starts speaking
    user.setSpeaking(true);
    EXPECT_TRUE(user.isSpeaking());
    
    // User mutes themselves
    user.setMuted(true);
    EXPECT_TRUE(user.isMuted());
    
    // User stops speaking
    user.setSpeaking(false);
    EXPECT_FALSE(user.isSpeaking());
    
    // User deafens (can't hear others)
    user.setDeafened(true);
    EXPECT_TRUE(user.isDeafened());
    
    // User switches channel
    user.setCurrentChannel(ChannelId(20));
    EXPECT_EQ(user.currentChannel(), ChannelId(20));
    
    // User goes away
    user.setStatus(UserStatus::Away);
    EXPECT_EQ(user.status(), UserStatus::Away);
    
    // User comes back
    user.setStatus(UserStatus::Online);
    EXPECT_EQ(user.status(), UserStatus::Online);
    
    // User unmutes and undeafens
    user.setMuted(false);
    user.setDeafened(false);
    EXPECT_FALSE(user.isMuted());
    EXPECT_FALSE(user.isDeafened());
    
    // User leaves
    user.setStatus(UserStatus::Offline);
    user.setCurrentChannel(ChannelId(0));
    EXPECT_EQ(user.status(), UserStatus::Offline);
    EXPECT_EQ(user.currentChannel(), ChannelId(0));
}

TEST(UserTest, EdgeCaseUserIds) {
    // Test with minimum and maximum valid IDs
    User user1(UserId(1), "min");
    User user2(UserId(0xFFFFFFFF), "max");
    
    EXPECT_EQ(user1.id(), UserId(1));
    EXPECT_EQ(user2.id(), UserId(0xFFFFFFFF));
}

TEST(UserTest, EmptyUsername) {
    User user(UserId(1), "");
    EXPECT_EQ(user.username(), "");
}

TEST(UserTest, LongUsername) {
    std::string longName(100, 'a');
    User user(UserId(1), longName);
    EXPECT_EQ(user.username(), longName);
}

} // namespace
} // namespace nevo
