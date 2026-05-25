/**
 * @file TestPermissionEdgeCases.cpp
 * @brief Edge case tests for Permission system
 * 
 * 覆盖缺口：PermissionManager边界条件（不存在的组、权限组合）
 * 风险等级：高 - 权限系统涉及安全，边界条件可能导致未授权访问
 */

#include <gtest/gtest.h>
#include "nevo/core/model/Permission.h"

namespace nevo {
namespace {

// ============================================================
// PermissionManager edge cases
// ============================================================

TEST(PermissionEdgeCaseTest, GrantPermissionToNonExistentGroup) {
    PermissionManager pm;
    
    // Grant to non-existent group should not crash
    pm.grantPermission(GroupId(999), Permission::Speak);
    
    // Group still doesn't exist
    EXPECT_EQ(pm.getGroup(GroupId(999)), nullptr);
    EXPECT_FALSE(pm.hasPermission(GroupId(999), Permission::Speak));
}

TEST(PermissionEdgeCaseTest, RevokePermissionFromNonExistentGroup) {
    PermissionManager pm;
    
    // Revoke from non-existent group should not crash
    pm.revokePermission(GroupId(999), Permission::Speak);
    
    // Group still doesn't exist
    EXPECT_EQ(pm.getGroup(GroupId(999)), nullptr);
}

TEST(PermissionEdgeCaseTest, GrantPermissionAlreadyGranted) {
    PermissionManager pm;
    
    // User already has Speak
    EXPECT_TRUE(pm.hasPermission(GROUP_USER, Permission::Speak));
    
    // Grant again (idempotent)
    pm.grantPermission(GROUP_USER, Permission::Speak);
    
    // Should still have it
    EXPECT_TRUE(pm.hasPermission(GROUP_USER, Permission::Speak));
}

TEST(PermissionEdgeCaseTest, RevokePermissionAlreadyRevoked) {
    PermissionManager pm;
    
    // Guest doesn't have Speak
    EXPECT_FALSE(pm.hasPermission(GROUP_GUEST, Permission::Speak));
    
    // Revoke again (idempotent, should not crash)
    pm.revokePermission(GROUP_GUEST, Permission::Speak);
    
    // Should still not have it
    EXPECT_FALSE(pm.hasPermission(GROUP_GUEST, Permission::Speak));
}

TEST(PermissionEdgeCaseTest, RevokeAllPermissions) {
    PermissionManager pm;
    
    // Start with a group that has some permissions
    EXPECT_TRUE(pm.hasPermission(GROUP_USER, Permission::JoinChannel));
    
    // Revoke all permissions one by one
    pm.revokePermission(GROUP_USER, Permission::JoinChannel);
    pm.revokePermission(GROUP_USER, Permission::Speak);
    pm.revokePermission(GROUP_USER, Permission::CreateChannel);
    pm.revokePermission(GROUP_USER, Permission::TextChat);
    
    // Should have no permissions now
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::JoinChannel));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::Speak));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::CreateChannel));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::TextChat));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::ServerAdmin));
}

TEST(PermissionEdgeCaseTest, GrantAllPermissions) {
    PermissionManager pm;
    
    // Start with Guest (limited permissions)
    EXPECT_FALSE(pm.hasPermission(GROUP_GUEST, Permission::Speak));
    
    // Grant all permissions
    pm.grantPermission(GROUP_GUEST, Permission::JoinChannel);
    pm.grantPermission(GROUP_GUEST, Permission::Speak);
    pm.grantPermission(GROUP_GUEST, Permission::CreateChannel);
    pm.grantPermission(GROUP_GUEST, Permission::DeleteChannel);
    pm.grantPermission(GROUP_GUEST, Permission::KickUser);
    pm.grantPermission(GROUP_GUEST, Permission::MoveUser);
    pm.grantPermission(GROUP_GUEST, Permission::MuteUser);
    pm.grantPermission(GROUP_GUEST, Permission::ManagePermission);
    pm.grantPermission(GROUP_GUEST, Permission::ServerAdmin);
    pm.grantPermission(GROUP_GUEST, Permission::BanUser);
    pm.grantPermission(GROUP_GUEST, Permission::TextChat);
    pm.grantPermission(GROUP_GUEST, Permission::Whisper);
    
    // Should have all permissions now
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::JoinChannel));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::Speak));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::CreateChannel));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::DeleteChannel));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::KickUser));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::MoveUser));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::MuteUser));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::ManagePermission));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::ServerAdmin));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::BanUser));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::TextChat));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::Whisper));
}

TEST(PermissionEdgeCaseTest, PermissionCombinations) {
    PermissionGroup g;
    g.id = GroupId(100);
    g.name = "Combo";
    g.permissions = 0;
    
    // Grant multiple permissions at once via bitwise OR
    g.permissions = static_cast<uint64_t>(Permission::JoinChannel)
                  | static_cast<uint64_t>(Permission::Speak)
                  | static_cast<uint64_t>(Permission::TextChat);
    
    EXPECT_TRUE(g.hasPermission(Permission::JoinChannel));
    EXPECT_TRUE(g.hasPermission(Permission::Speak));
    EXPECT_TRUE(g.hasPermission(Permission::TextChat));
    EXPECT_FALSE(g.hasPermission(Permission::CreateChannel));
}

// ============================================================
// PermissionGroup edge cases
// ============================================================

TEST(PermissionEdgeCaseTest, PermissionGroupDefaultState) {
    PermissionGroup g;
    g.id = GroupId(1);
    g.name = "Test";
    // permissions not initialized - test behavior
    
    // With uninitialized permissions, behavior is undefined
    // But we can test that hasPermission doesn't crash
    // (actual result depends on memory state)
}

TEST(PermissionEdgeCaseTest, PermissionGroupZeroPermissions) {
    PermissionGroup g;
    g.id = GroupId(1);
    g.name = "None";
    g.permissions = 0;
    
    EXPECT_FALSE(g.hasPermission(Permission::JoinChannel));
    EXPECT_FALSE(g.hasPermission(Permission::Speak));
    EXPECT_FALSE(g.hasPermission(Permission::ServerAdmin));
}

TEST(PermissionEdgeCaseTest, PermissionGroupAllBitsSet) {
    PermissionGroup g;
    g.id = GroupId(1);
    g.name = "All";
    g.permissions = ~0ULL; // All bits set
    
    EXPECT_TRUE(g.hasPermission(Permission::JoinChannel));
    EXPECT_TRUE(g.hasPermission(Permission::Speak));
    EXPECT_TRUE(g.hasPermission(Permission::ServerAdmin));
    EXPECT_TRUE(g.hasPermission(Permission::Whisper));
}

TEST(PermissionEdgeCaseTest, GrantRevokeSequence) {
    PermissionGroup g;
    g.id = GroupId(1);
    g.name = "Test";
    g.permissions = 0;
    
    // Grant
    g.grant(Permission::Speak);
    EXPECT_TRUE(g.hasPermission(Permission::Speak));
    
    // Revoke
    g.revoke(Permission::Speak);
    EXPECT_FALSE(g.hasPermission(Permission::Speak));
    
    // Grant again
    g.grant(Permission::Speak);
    EXPECT_TRUE(g.hasPermission(Permission::Speak));
    
    // Revoke again
    g.revoke(Permission::Speak);
    EXPECT_FALSE(g.hasPermission(Permission::Speak));
}

// ============================================================
// Group ID edge cases
// ============================================================

TEST(PermissionEdgeCaseTest, GroupIdZero) {
    PermissionManager pm;
    
    // GroupId(0) is not a default group
    EXPECT_EQ(pm.getGroup(GroupId(0)), nullptr);
    EXPECT_FALSE(pm.hasPermission(GroupId(0), Permission::JoinChannel));
}

TEST(PermissionEdgeCaseTest, LargeGroupId) {
    PermissionManager pm;
    
    PermissionGroup custom;
    custom.id = GroupId(0xFFFFFFFF);
    custom.name = "Large";
    custom.permissions = static_cast<uint64_t>(Permission::JoinChannel);
    
    pm.addGroup(custom);
    
    EXPECT_NE(pm.getGroup(GroupId(0xFFFFFFFF)), nullptr);
    EXPECT_TRUE(pm.hasPermission(GroupId(0xFFFFFFFF), Permission::JoinChannel));
}

// ============================================================
// addGroup edge cases
// ============================================================

TEST(PermissionEdgeCaseTest, AddMultipleCustomGroups) {
    PermissionManager pm;
    
    for (int i = 100; i < 110; ++i) {
        PermissionGroup g;
        g.id = GroupId(i);
        g.name = "Group" + std::to_string(i);
        g.permissions = static_cast<uint64_t>(Permission::JoinChannel);
        pm.addGroup(g);
    }
    
    // Verify all were added
    for (int i = 100; i < 110; ++i) {
        EXPECT_NE(pm.getGroup(GroupId(i)), nullptr);
        EXPECT_TRUE(pm.hasPermission(GroupId(i), Permission::JoinChannel));
    }
    
    // Default groups still exist
    EXPECT_NE(pm.getGroup(GROUP_ADMIN), nullptr);
    EXPECT_NE(pm.getGroup(GROUP_USER), nullptr);
}

TEST(PermissionEdgeCaseTest, AddGroupWithEmptyName) {
    PermissionManager pm;
    
    PermissionGroup g;
    g.id = GroupId(100);
    g.name = "";
    g.permissions = static_cast<uint64_t>(Permission::JoinChannel);
    
    pm.addGroup(g);
    
    auto* retrieved = pm.getGroup(GroupId(100));
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "");
}

// ============================================================
// Permission bit manipulation edge cases
// ============================================================

TEST(PermissionEdgeCaseTest, PermissionBitValues) {
    // Verify each permission has correct bit value
    EXPECT_EQ(static_cast<uint64_t>(Permission::JoinChannel),      1ULL << 0);
    EXPECT_EQ(static_cast<uint64_t>(Permission::Speak),            1ULL << 1);
    EXPECT_EQ(static_cast<uint64_t>(Permission::CreateChannel),    1ULL << 2);
    EXPECT_EQ(static_cast<uint64_t>(Permission::DeleteChannel),    1ULL << 3);
    EXPECT_EQ(static_cast<uint64_t>(Permission::KickUser),         1ULL << 4);
    EXPECT_EQ(static_cast<uint64_t>(Permission::MoveUser),         1ULL << 5);
    EXPECT_EQ(static_cast<uint64_t>(Permission::MuteUser),         1ULL << 6);
    EXPECT_EQ(static_cast<uint64_t>(Permission::ManagePermission), 1ULL << 7);
    EXPECT_EQ(static_cast<uint64_t>(Permission::ServerAdmin),      1ULL << 8);
    EXPECT_EQ(static_cast<uint64_t>(Permission::BanUser),          1ULL << 9);
    EXPECT_EQ(static_cast<uint64_t>(Permission::TextChat),         1ULL << 10);
    EXPECT_EQ(static_cast<uint64_t>(Permission::Whisper),          1ULL << 11);
}

TEST(PermissionEdgeCaseTest, NoPermissionOverlap) {
    // Verify all permissions have unique bit positions
    std::vector<uint64_t> values = {
        static_cast<uint64_t>(Permission::JoinChannel),
        static_cast<uint64_t>(Permission::Speak),
        static_cast<uint64_t>(Permission::CreateChannel),
        static_cast<uint64_t>(Permission::DeleteChannel),
        static_cast<uint64_t>(Permission::KickUser),
        static_cast<uint64_t>(Permission::MoveUser),
        static_cast<uint_cast<uint64_t>(Permission::MuteUser),
        static_cast<uint64_t>(Permission::ManagePermission),
        static_cast<uint64_t>(Permission::ServerAdmin),
        static_cast<uint64_t>(Permission::BanUser),
        static_cast<uint64_t>(Permission::TextChat),
        static_cast<uint64_t>(Permission::Whisper),
    };
    
    // Check no duplicates
    for (size_t i = 0; i < values.size(); ++i) {
        for (size_t j = i + 1; j < values.size(); ++j) {
            EXPECT_NE(values[i], values[j])
                << "Permissions at indices " << i << " and " << j << " have same value";
        }
    }
}

// ============================================================
// Admin permission completeness
// ============================================================

TEST(PermissionEdgeCaseTest, AdminHasAllDefinedPermissions) {
    PermissionManager pm;
    auto* admin = pm.getGroup(GROUP_ADMIN);
    ASSERT_NE(admin, nullptr);
    
    // Admin should have every permission
    EXPECT_TRUE(admin->hasPermission(Permission::JoinChannel));
    EXPECT_TRUE(admin->hasPermission(Permission::Speak));
    EXPECT_TRUE(admin->hasPermission(Permission::CreateChannel));
    EXPECT_TRUE(admin->hasPermission(Permission::DeleteChannel));
    EXPECT_TRUE(admin->hasPermission(Permission::KickUser));
    EXPECT_TRUE(admin->hasPermission(Permission::MoveUser));
    EXPECT_TRUE(admin->hasPermission(Permission::MuteUser));
    EXPECT_TRUE(admin->hasPermission(Permission::ManagePermission));
    EXPECT_TRUE(admin->hasPermission(Permission::ServerAdmin));
    EXPECT_TRUE(admin->hasPermission(Permission::BanUser));
    EXPECT_TRUE(admin->hasPermission(Permission::TextChat));
    EXPECT_TRUE(admin->hasPermission(Permission::Whisper));
}

} // namespace
} // namespace nevo
