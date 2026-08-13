/**
 * @file TestPermission.cpp
 * @brief Unit tests for the group-based permission system
 */

#include <gtest/gtest.h>
#include "nevo/core/model/Permission.h"

namespace nevo {
namespace {

// ============================================================
// PermissionGroup: hasPermission / grant / revoke
// ============================================================

TEST(PermissionGroupTest, HasPermissionReturnsFalseByDefault) {
    PermissionGroup g;
    g.id = GroupId(10);
    g.name = "TestGroup";
    g.permissions = 0;

    EXPECT_FALSE(g.hasPermission(Permission::JoinChannel));
    EXPECT_FALSE(g.hasPermission(Permission::Speak));
}

TEST(PermissionGroupTest, GrantAndHasPermission) {
    PermissionGroup g;
    g.id = GroupId(10);
    g.name = "TestGroup";
    g.permissions = 0;

    g.grant(Permission::JoinChannel);
    EXPECT_TRUE(g.hasPermission(Permission::JoinChannel));
    EXPECT_FALSE(g.hasPermission(Permission::Speak));

    g.grant(Permission::Speak);
    EXPECT_TRUE(g.hasPermission(Permission::Speak));
}

TEST(PermissionGroupTest, RevokeRemovesPermission) {
    PermissionGroup g;
    g.id = GroupId(10);
    g.name = "TestGroup";
    g.permissions = 0;

    g.grant(Permission::JoinChannel);
    g.grant(Permission::Speak);
    EXPECT_TRUE(g.hasPermission(Permission::Speak));

    g.revoke(Permission::Speak);
    EXPECT_FALSE(g.hasPermission(Permission::Speak));
    EXPECT_TRUE(g.hasPermission(Permission::JoinChannel));
}

TEST(PermissionGroupTest, GrantIdempotent) {
    PermissionGroup g;
    g.id = GroupId(10);
    g.name = "TestGroup";
    g.permissions = 0;

    g.grant(Permission::JoinChannel);
    g.grant(Permission::JoinChannel);
    EXPECT_TRUE(g.hasPermission(Permission::JoinChannel));

    g.revoke(Permission::JoinChannel);
    EXPECT_FALSE(g.hasPermission(Permission::JoinChannel));
}

TEST(PermissionGroupTest, RevokeOnNoPermissionIsNoop) {
    PermissionGroup g;
    g.id = GroupId(10);
    g.name = "TestGroup";
    g.permissions = 0;

    g.revoke(Permission::JoinChannel);
    EXPECT_FALSE(g.hasPermission(Permission::JoinChannel));
}

// ============================================================
// PermissionManager: default groups
// ============================================================

TEST(PermissionManagerTest, DefaultGroupsExist) {
    PermissionManager pm;

    EXPECT_NE(pm.getGroup(GROUP_ADMIN), nullptr);
    EXPECT_NE(pm.getGroup(GROUP_CHANNEL_ADMIN), nullptr);
    EXPECT_NE(pm.getGroup(GROUP_USER), nullptr);
    EXPECT_NE(pm.getGroup(GROUP_GUEST), nullptr);
}

TEST(PermissionManagerTest, AdminHasAllPermissions) {
    PermissionManager pm;
    const auto* admin = pm.getGroup(GROUP_ADMIN);
    ASSERT_NE(admin, nullptr);

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

TEST(PermissionManagerTest, GuestHasNoSpeak) {
    PermissionManager pm;
    const auto* guest = pm.getGroup(GROUP_GUEST);
    ASSERT_NE(guest, nullptr);

    EXPECT_TRUE(guest->hasPermission(Permission::JoinChannel));
    EXPECT_TRUE(guest->hasPermission(Permission::TextChat));
    EXPECT_FALSE(guest->hasPermission(Permission::Speak));
}

// ============================================================
// hasPermission for each group
// ============================================================

TEST(PermissionManagerTest, HasPermissionForAdmin) {
    PermissionManager pm;
    EXPECT_TRUE(pm.hasPermission(GROUP_ADMIN, Permission::ServerAdmin));
    EXPECT_TRUE(pm.hasPermission(GROUP_ADMIN, Permission::BanUser));
    EXPECT_TRUE(pm.hasPermission(GROUP_ADMIN, Permission::Speak));
}

TEST(PermissionManagerTest, HasPermissionForChannelAdmin) {
    PermissionManager pm;
    EXPECT_TRUE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::JoinChannel));
    EXPECT_TRUE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::Speak));
    EXPECT_TRUE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::CreateChannel));
    EXPECT_TRUE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::DeleteChannel));
    EXPECT_TRUE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::KickUser));
    EXPECT_TRUE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::MuteUser));
    EXPECT_TRUE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::TextChat));

    // Channel Admin should NOT have these
    EXPECT_FALSE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::MoveUser));
    EXPECT_FALSE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::ManagePermission));
    EXPECT_FALSE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::ServerAdmin));
    EXPECT_FALSE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::BanUser));
    EXPECT_FALSE(pm.hasPermission(GROUP_CHANNEL_ADMIN, Permission::Whisper));
}

TEST(PermissionManagerTest, HasPermissionForUser) {
    PermissionManager pm;
    EXPECT_TRUE(pm.hasPermission(GROUP_USER, Permission::JoinChannel));
    EXPECT_TRUE(pm.hasPermission(GROUP_USER, Permission::Speak));
    EXPECT_TRUE(pm.hasPermission(GROUP_USER, Permission::CreateChannel));
    EXPECT_TRUE(pm.hasPermission(GROUP_USER, Permission::TextChat));

    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::DeleteChannel));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::KickUser));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::MoveUser));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::MuteUser));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::ManagePermission));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::ServerAdmin));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::BanUser));
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::Whisper));
}

TEST(PermissionManagerTest, HasPermissionForGuest) {
    PermissionManager pm;
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::JoinChannel));
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::TextChat));

    EXPECT_FALSE(pm.hasPermission(GROUP_GUEST, Permission::Speak));
    EXPECT_FALSE(pm.hasPermission(GROUP_GUEST, Permission::CreateChannel));
    EXPECT_FALSE(pm.hasPermission(GROUP_GUEST, Permission::DeleteChannel));
}

TEST(PermissionManagerTest, HasPermissionReturnsFalseForUnknownGroup) {
    PermissionManager pm;
    EXPECT_FALSE(pm.hasPermission(GroupId(999), Permission::JoinChannel));
}

// ============================================================
// addGroup and grantPermission
// ============================================================

TEST(PermissionManagerTest, AddCustomGroup) {
    PermissionManager pm;

    PermissionGroup custom;
    custom.id = GroupId(100);
    custom.name = "Custom";
    custom.permissions = 0;
    custom.grant(Permission::JoinChannel);
    custom.grant(Permission::Speak);

    pm.addGroup(custom);

    const auto* g = pm.getGroup(GroupId(100));
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->name, "Custom");
    EXPECT_TRUE(g->hasPermission(Permission::JoinChannel));
    EXPECT_TRUE(g->hasPermission(Permission::Speak));
    EXPECT_FALSE(g->hasPermission(Permission::CreateChannel));
}

TEST(PermissionManagerTest, AddGroupWithExistingIdUpdatesGroup) {
    PermissionManager pm;

    // GROUP_USER initially has CreateChannel
    EXPECT_TRUE(pm.hasPermission(GROUP_USER, Permission::CreateChannel));

    PermissionGroup modified;
    modified.id = GROUP_USER;
    modified.name = "Restricted User";
    modified.permissions = 0;
    modified.grant(Permission::JoinChannel);
    modified.grant(Permission::Speak);
    // No CreateChannel

    pm.addGroup(modified);

    // Should now be updated
    const auto* g = pm.getGroup(GROUP_USER);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->name, "Restricted User");
    EXPECT_FALSE(g->hasPermission(Permission::CreateChannel));
}

TEST(PermissionManagerTest, GrantPermissionToExistingGroup) {
    PermissionManager pm;

    // Guest does not have Speak
    EXPECT_FALSE(pm.hasPermission(GROUP_GUEST, Permission::Speak));

    pm.grantPermission(GROUP_GUEST, Permission::Speak);

    // Now Guest has Speak
    EXPECT_TRUE(pm.hasPermission(GROUP_GUEST, Permission::Speak));
}

TEST(PermissionManagerTest, RevokePermissionFromExistingGroup) {
    PermissionManager pm;

    // User has Speak
    EXPECT_TRUE(pm.hasPermission(GROUP_USER, Permission::Speak));

    pm.revokePermission(GROUP_USER, Permission::Speak);

    // Now User does not have Speak
    EXPECT_FALSE(pm.hasPermission(GROUP_USER, Permission::Speak));
}

} // namespace
} // namespace nevo
