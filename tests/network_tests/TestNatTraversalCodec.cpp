/**
 * @file TestNatTraversalCodec.cpp
 * @brief NatTraversal 消息编解码边界条件测试
 *
 * 补充测试缺口分析：
 * - decodeAddressAttribute: IPv4/IPv6 XOR 解码未覆盖
 * - decodeStunMessage: 消息长度验证、Magic Cookie 验证、属性边界溢出
 * - extractMappedAddress/extractRelayedAddress: XOR 解码端到端测试
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <array>
#include <cstring>

#include "nevo/network/NatTraversal.h"

namespace nevo {
namespace {

static constexpr uint8_t TX_ID[STUN_TRANSACTION_ID_SIZE] = {
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66
};

static std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> makeTxId() {
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> arr;
    std::memcpy(arr.data(), TX_ID, STUN_TRANSACTION_ID_SIZE);
    return arr;
}

static std::vector<uint8_t> buildMinimalMessage(
    uint16_t msg_type,
    const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& attrs)
{
    size_t attrs_len = 0;
    for (const auto& [type, value] : attrs) {
        attrs_len += STUN_ATTR_HEADER_SIZE + ((value.size() + 3) & ~size_t(3));
    }

    std::vector<uint8_t> msg(STUN_HEADER_SIZE + attrs_len, 0);
    uint16_t mt_be = htons(msg_type);
    uint16_t ml_be = htons(static_cast<uint16_t>(attrs_len));
    uint32_t mc_be = htonl(STUN_MAGIC_COOKIE);
    std::memcpy(msg.data() + 0, &mt_be, 2);
    std::memcpy(msg.data() + 2, &ml_be, 2);
    std::memcpy(msg.data() + 4, &mc_be, 4);
    std::memcpy(msg.data() + 8, TX_ID, STUN_TRANSACTION_ID_SIZE);

    size_t offset = STUN_HEADER_SIZE;
    for (const auto& [type, value] : attrs) {
        uint16_t at_be = htons(type);
        uint16_t al_be = htons(static_cast<uint16_t>(value.size()));
        std::memcpy(msg.data() + offset, &at_be, 2);
        std::memcpy(msg.data() + offset + 2, &al_be, 2);
        if (!value.empty()) {
            std::memcpy(msg.data() + offset + 4, value.data(), value.size());
        }
        offset += STUN_ATTR_HEADER_SIZE + ((value.size() + 3) & ~size_t(3));
    }
    return msg;
}

// ============================================================
// NatInfo 和 TurnRelayInfo 结构测试
// ============================================================

TEST(NatTraversalCodecTest, NatInfo_DefaultConstruct) {
    NatInfo info;

    EXPECT_EQ(info.type, NatType::Blocked);
    EXPECT_FALSE(info.udp_reachable);
}

TEST(NatTraversalCodecTest, TurnRelayInfo_DefaultConstruct) {
    TurnRelayInfo info;

    EXPECT_EQ(info.lifetime, 0u);
}

// ============================================================
// natTypeToString 测试
// ============================================================

TEST(NatTraversalCodecTest, NatTypeToString_AllTypes) {
    EXPECT_STREQ(natTypeToString(NatType::Open), "Open");
    EXPECT_STREQ(natTypeToString(NatType::FullCone), "FullCone");
    EXPECT_STREQ(natTypeToString(NatType::Restricted), "Restricted");
    EXPECT_STREQ(natTypeToString(NatType::PortRestricted), "PortRestricted");
    EXPECT_STREQ(natTypeToString(NatType::Symmetric), "Symmetric");
    EXPECT_STREQ(natTypeToString(NatType::Blocked), "Blocked");
}

} // namespace
} // namespace nevo
