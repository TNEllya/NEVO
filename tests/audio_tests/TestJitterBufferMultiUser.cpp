/**
 * @file TestJitterBufferMultiUser.cpp
 * @brief JitterBuffer 多用户功能测试
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "nevo/core/audio/JitterBuffer.h"

namespace nevo {

class JitterBufferMultiUserTest : public ::testing::Test {
protected:
    void SetUp() override {
        jb = std::make_unique<JitterBuffer>();
    }

    std::unique_ptr<JitterBuffer> jb;
};

TEST_F(JitterBufferMultiUserTest, PushStoresSingleUserPacket) {
    uint8_t data[] = {1, 2, 3, 4, 5};

    jb->push(UserId(1), data, sizeof(data), 1000);

    uint8_t* out_data = nullptr;
    uint32_t out_size = 0;
    EXPECT_TRUE(jb->pop(UserId(1), out_data, out_size));
    EXPECT_EQ(out_size, sizeof(data));
}

TEST_F(JitterBufferMultiUserTest, PopReturnsDataInFIFOOrder) {
    uint8_t data1[] = {1};
    uint8_t data2[] = {2};
    uint8_t data3[] = {3};

    jb->push(UserId(1), data1, 1, 1000);
    jb->push(UserId(1), data2, 1, 2000);
    jb->push(UserId(1), data3, 1, 3000);

    uint8_t* out_data = nullptr;
    uint32_t out_size = 0;

    ASSERT_TRUE(jb->pop(UserId(1), out_data, out_size));
    EXPECT_EQ(out_data[0], 1);

    ASSERT_TRUE(jb->pop(UserId(1), out_data, out_size));
    EXPECT_EQ(out_data[0], 2);

    ASSERT_TRUE(jb->pop(UserId(1), out_data, out_size));
    EXPECT_EQ(out_data[0], 3);
}

TEST_F(JitterBufferMultiUserTest, MultipleUsersHaveIsolatedQueues) {
    uint8_t data1[] = {10, 20};
    uint8_t data2[] = {30, 40};

    jb->push(UserId(1), data1, sizeof(data1), 1000);
    jb->push(UserId(2), data2, sizeof(data2), 2000);

    uint8_t* out_data = nullptr;
    uint32_t out_size = 0;

    ASSERT_TRUE(jb->pop(UserId(1), out_data, out_size));
    EXPECT_EQ(out_size, 2u);
    EXPECT_EQ(out_data[0], 10);
    EXPECT_EQ(out_data[1], 20);

    ASSERT_TRUE(jb->pop(UserId(2), out_data, out_size));
    EXPECT_EQ(out_size, 2u);
    EXPECT_EQ(out_data[0], 30);
    EXPECT_EQ(out_data[1], 40);
}

TEST_F(JitterBufferMultiUserTest, RemoveUserClearsAllData) {
    uint8_t data[] = {1, 2, 3};

    jb->push(UserId(1), data, sizeof(data), 1000);
    jb->push(UserId(2), data, sizeof(data), 2000);

    jb->removeUser(UserId(1));

    uint8_t* out_data = nullptr;
    uint32_t out_size = 0;
    EXPECT_FALSE(jb->pop(UserId(1), out_data, out_size));

    ASSERT_TRUE(jb->pop(UserId(2), out_data, out_size));
    EXPECT_EQ(out_size, 3u);
}

TEST_F(JitterBufferMultiUserTest, RemoveNonexistentUserIsSafe) {
    jb->removeUser(UserId(999));
    EXPECT_FALSE(jb->pop(UserId(999), nullptr, nullptr));
}

TEST_F(JitterBufferMultiUserTest, PopFromEmptyUserReturnsFalse) {
    uint8_t* out_data = nullptr;
    uint32_t out_size = 0;
    EXPECT_FALSE(jb->pop(UserId(1), out_data, out_size));
}

TEST_F(JitterBufferMultiUserTest, LargePacketHandling) {
    std::vector<uint8_t> large_data(1400, 0xFF);

    jb->push(UserId(1), large_data.data(), large_data.size(), 1000);

    uint8_t* out_data = nullptr;
    uint32_t out_size = 0;
    ASSERT_TRUE(jb->pop(UserId(1), out_data, out_size));
    EXPECT_EQ(out_size, 1400u);
}

TEST_F(JitterBufferMultiUserTest, EmptyPacketHandling) {
    uint8_t empty_data[] = {};

    jb->push(UserId(1), empty_data, 0, 1000);

    uint8_t* out_data = nullptr;
    uint32_t out_size = 0;
    ASSERT_TRUE(jb->pop(UserId(1), out_data, out_size));
    EXPECT_EQ(out_size, 0u);
}

TEST_F(JitterBufferMultiUserTest, FiftyConcurrentUsers) {
    const uint32_t num_users = 50;
    uint8_t data[] = {42};

    for (uint32_t i = 0; i < num_users; ++i) {
        jb->push(UserId(i), data, 1, i * 1000);
    }

    for (uint32_t i = 0; i < num_users; ++i) {
        uint8_t* out_data = nullptr;
        uint32_t out_size = 0;
        ASSERT_TRUE(jb->pop(UserId(i), out_data, out_size));
        EXPECT_EQ(out_size, 1u);
        EXPECT_EQ(out_data[0], 42);
    }
}

TEST_F(JitterBufferMultiUserTest, HundredPacketsPerUser) {
    const uint32_t num_packets = 100;
    UserId uid(1);

    for (uint32_t i = 0; i < num_packets; ++i) {
        uint8_t data[] = {static_cast<uint8_t>(i)};
        jb->push(uid, data, 1, i);
    }

    for (uint32_t i = 0; i < num_packets; ++i) {
        uint8_t* out_data = nullptr;
        uint32_t out_size = 0;
        ASSERT_TRUE(jb->pop(uid, out_data, out_size));
        EXPECT_EQ(out_data[0], static_cast<uint8_t>(i));
    }

    uint8_t* out_data = nullptr;
    uint32_t out_size = 0;
    EXPECT_FALSE(jb->pop(uid, out_data, out_size));
}

TEST_F(JitterBufferMultiUserTest, ResetClearsAllUsers) {
    uint8_t data[] = {1, 2, 3};

    jb->push(UserId(1), data, sizeof(data), 1000);
    jb->push(UserId(2), data, sizeof(data), 2000);

    jb->reset();

    uint8_t* out_data = nullptr;
    uint32_t out_size = 0;
    EXPECT_FALSE(jb->pop(UserId(1), out_data, out_size));
    EXPECT_FALSE(jb->pop(UserId(2), out_data, out_size));
}

TEST_F(JitterBufferMultiUserTest, UserDataPersistenceAfterPop) {
    uint8_t data[] = {0xAB, 0xCD};

    jb->push(UserId(1), data, sizeof(data), 1000);

    uint8_t* out_data1 = nullptr;
    uint32_t out_size1 = 0;
    ASSERT_TRUE(jb->pop(UserId(1), out_data1, out_size1));

    uint8_t* out_data2 = nullptr;
    uint32_t out_size2 = 0;
    ASSERT_TRUE(jb->pop(UserId(1), out_data2, out_size2));

    EXPECT_EQ(out_size1, out_size2);
    EXPECT_EQ(out_data1[0], out_data2[0]);
}

} // namespace nevo
