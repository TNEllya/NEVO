/**
 * @file TestChannel.cpp
 * @brief Unit tests for Channel tree model
 */

#include <gtest/gtest.h>
#include "nevo/core/model/Channel.h"

namespace nevo {
namespace {

// ============================================================
// Channel construction
// ============================================================

TEST(ChannelTest, ConstructionSetsIdAndName) {
    Channel ch(ChannelId(1), "General");
    EXPECT_EQ(ch.id(), ChannelId(1));
    EXPECT_EQ(ch.name(), "General");
    EXPECT_EQ(ch.parent(), nullptr);
    EXPECT_TRUE(ch.children().empty());
    EXPECT_TRUE(ch.users().empty());
    EXPECT_TRUE(ch.isPermanent());
}

TEST(ChannelTest, ConstructionWithParent) {
    Channel root(ChannelId(1), "Root");
    Channel child(ChannelId(2), "Child", &root);
    EXPECT_EQ(child.parent(), &root);
}

// ============================================================
// addChild / removeChild
// ============================================================

TEST(ChannelTest, AddChildSetsParentAndAddsToChildren) {
    Channel root(ChannelId(1), "Root");
    Channel child(ChannelId(2), "Child");

    root.addChild(&child);

    ASSERT_EQ(root.children().size(), 1u);
    EXPECT_EQ(root.children()[0], &child);
    EXPECT_EQ(child.parent(), &root);
}

TEST(ChannelTest, AddChildRejectsSelf) {
    Channel ch(ChannelId(1), "Self");
    ch.addChild(&ch);
    EXPECT_TRUE(ch.children().empty());
}

TEST(ChannelTest, AddChildRejectsNull) {
    Channel ch(ChannelId(1), "Root");
    ch.addChild(nullptr);
    EXPECT_TRUE(ch.children().empty());
}

TEST(ChannelTest, RemoveChildClearsParent) {
    Channel root(ChannelId(1), "Root");
    Channel child(ChannelId(2), "Child");

    root.addChild(&child);
    ASSERT_EQ(child.parent(), &root);

    root.removeChild(&child);

    EXPECT_TRUE(root.children().empty());
    EXPECT_EQ(child.parent(), nullptr);
}

TEST(ChannelTest, RemoveNonexistentChildIsNoop) {
    Channel root(ChannelId(1), "Root");
    Channel orphan(ChannelId(2), "Orphan");

    root.removeChild(&orphan);
    EXPECT_TRUE(root.children().empty());
}

// ============================================================
// addUser / removeUser / hasUser
// ============================================================

TEST(ChannelTest, AddUserAndHasUser) {
    Channel ch(ChannelId(1), "Room");
    UserId uid(42);

    EXPECT_FALSE(ch.hasUser(uid));

    ch.addUser(uid);
    EXPECT_TRUE(ch.hasUser(uid));
    ASSERT_EQ(ch.users().size(), 1u);
    EXPECT_EQ(ch.users()[0], uid);
}

TEST(ChannelTest, AddUserPreventsDuplicate) {
    Channel ch(ChannelId(1), "Room");
    UserId uid(42);

    ch.addUser(uid);
    ch.addUser(uid);
    EXPECT_EQ(ch.users().size(), 1u);
}

TEST(ChannelTest, RemoveUser) {
    Channel ch(ChannelId(1), "Room");
    UserId uid1(10);
    UserId uid2(20);

    ch.addUser(uid1);
    ch.addUser(uid2);
    EXPECT_EQ(ch.users().size(), 2u);

    ch.removeUser(uid1);
    EXPECT_FALSE(ch.hasUser(uid1));
    EXPECT_TRUE(ch.hasUser(uid2));
    EXPECT_EQ(ch.users().size(), 1u);
}

TEST(ChannelTest, RemoveNonexistentUserIsNoop) {
    Channel ch(ChannelId(1), "Room");
    UserId uid(99);
    ch.removeUser(uid);
    EXPECT_TRUE(ch.users().empty());
}

// ============================================================
// findChild recursive search
// ============================================================

TEST(ChannelTest, FindChildDirect) {
    Channel root(ChannelId(1), "Root");
    Channel child(ChannelId(2), "Child");

    root.addChild(&child);

    EXPECT_EQ(root.findChild(ChannelId(2)), &child);
    EXPECT_EQ(root.findChild(ChannelId(99)), nullptr);
}

TEST(ChannelTest, FindChildReturnsNullForNonexistent) {
    Channel root(ChannelId(1), "Root");
    EXPECT_EQ(root.findChild(ChannelId(5)), nullptr);
}

// ============================================================
// Tree structure with 3 levels
// ============================================================

TEST(ChannelTest, ThreeLevelTreeStructure) {
    // Level 0: Root
    Channel root(ChannelId(1), "Root");
    // Level 1: TeamA, TeamB
    Channel team_a(ChannelId(2), "TeamA");
    Channel team_b(ChannelId(3), "TeamB");
    // Level 2: TeamA-General, TeamA-Voice, TeamB-General
    Channel team_a_general(ChannelId(4), "TeamA-General");
    Channel team_a_voice(ChannelId(5), "TeamA-Voice");
    Channel team_b_general(ChannelId(6), "TeamB-General");

    // Build tree
    root.addChild(&team_a);
    root.addChild(&team_b);
    team_a.addChild(&team_a_general);
    team_a.addChild(&team_a_voice);
    team_b.addChild(&team_b_general);

    // Verify level 0
    ASSERT_EQ(root.children().size(), 2u);
    EXPECT_EQ(root.children()[0], &team_a);
    EXPECT_EQ(root.children()[1], &team_b);

    // Verify level 1
    EXPECT_EQ(team_a.parent(), &root);
    EXPECT_EQ(team_b.parent(), &root);
    ASSERT_EQ(team_a.children().size(), 2u);
    ASSERT_EQ(team_b.children().size(), 1u);

    // Verify level 2
    EXPECT_EQ(team_a_general.parent(), &team_a);
    EXPECT_EQ(team_a_voice.parent(), &team_a);
    EXPECT_EQ(team_b_general.parent(), &team_b);

    // findChild on root can find deep children
    EXPECT_EQ(root.findChild(ChannelId(5)), &team_a_voice);
    EXPECT_EQ(root.findChild(ChannelId(6)), &team_b_general);
    EXPECT_EQ(root.findChild(ChannelId(1)), nullptr); // Root cannot find itself

    // findChild on intermediate node
    EXPECT_EQ(team_a.findChild(ChannelId(4)), &team_a_general);
    EXPECT_EQ(team_a.findChild(ChannelId(6)), nullptr); // Not in team_a subtree

    // Users in tree
    team_a_general.addUser(UserId(100));
    team_a_voice.addUser(UserId(200));
    team_b_general.addUser(UserId(300));

    EXPECT_TRUE(team_a_general.hasUser(UserId(100)));
    EXPECT_FALSE(team_a_general.hasUser(UserId(200)));
    EXPECT_TRUE(team_a_voice.hasUser(UserId(200)));
}

// ============================================================
// Property setters
// ============================================================

TEST(ChannelTest, SetName) {
    Channel ch(ChannelId(1), "Old");
    ch.setName("New");
    EXPECT_EQ(ch.name(), "New");
}

TEST(ChannelTest, SetPermanent) {
    Channel ch(ChannelId(1), "Temp");
    ch.setPermanent(false);
    EXPECT_FALSE(ch.isPermanent());
    ch.setPermanent(true);
    EXPECT_TRUE(ch.isPermanent());
}

} // namespace
} // namespace nevo
