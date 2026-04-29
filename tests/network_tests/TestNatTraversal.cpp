/**
 * @file TestNatTraversal.cpp
 * @brief Unit tests for STUN/TURN NAT traversal module
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "nevo/network/NatTraversal.h"

namespace nevo {
namespace {

// ============================================================
// STUN Binding Request encoding
// ============================================================

TEST(NatTraversalTest, EncodeBindingRequestHasCorrectHeader) {
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> tx_id{};
    auto msg = NatTraversal::encodeBindingRequest(tx_id);

    // Minimum size: 20-byte header + SOFTWARE attribute
    EXPECT_GE(msg.size(), STUN_HEADER_SIZE);

    // Parse message type (first 2 bytes, big endian)
    uint16_t msg_type;
    std::memcpy(&msg_type, msg.data(), 2);
    msg_type = ntohs(msg_type);
    EXPECT_EQ(msg_type, STUN_BINDING_REQUEST);

    // Parse Magic Cookie (bytes 4-7)
    uint32_t magic;
    std::memcpy(&magic, msg.data() + 4, 4);
    magic = ntohl(magic);
    EXPECT_EQ(magic, STUN_MAGIC_COOKIE);

    // Parse message length (bytes 2-3)
    uint16_t msg_length;
    std::memcpy(&msg_length, msg.data() + 2, 2);
    msg_length = ntohs(msg_length);
    EXPECT_EQ(msg.size(), STUN_HEADER_SIZE + msg_length);

    // Transaction ID (bytes 8-19) should not be all zeros
    bool all_zero = true;
    for (size_t i = 0; i < STUN_TRANSACTION_ID_SIZE; ++i) {
        if (tx_id[i] != 0) {
            all_zero = false;
            break;
        }
    }
    EXPECT_FALSE(all_zero) << "Transaction ID should be randomly generated";
}

TEST(NatTraversalTest, EncodeBindingRequestContainsSoftwareAttribute) {
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> tx_id{};
    auto msg = NatTraversal::encodeBindingRequest(tx_id);

    // Parse message length
    uint16_t msg_length;
    std::memcpy(&msg_length, msg.data() + 2, 2);
    msg_length = ntohs(msg_length);

    // Should have attributes (at least SOFTWARE)
    EXPECT_GT(msg_length, 0u);

    // Parse first attribute
    size_t offset = STUN_HEADER_SIZE;
    uint16_t attr_type;
    std::memcpy(&attr_type, msg.data() + offset, 2);
    attr_type = ntohs(attr_type);

    uint16_t attr_len;
    std::memcpy(&attr_len, msg.data() + offset + 2, 2);
    attr_len = ntohs(attr_len);

    EXPECT_EQ(attr_type, STUN_ATTR_SOFTWARE);
    EXPECT_GT(attr_len, 0u);

    // Verify SOFTWARE value contains "NEVO"
    std::string software(reinterpret_cast<const char*>(msg.data() + offset + 4), attr_len);
    EXPECT_NE(software.find("NEVO"), std::string::npos);
}

// ============================================================
// STUN message decoding
// ============================================================

TEST(NatTraversalTest, DecodeValidStunMessage) {
    // Manually construct a STUN Binding Response with MAPPED-ADDRESS
    uint16_t msg_type = htons(STUN_BINDING_RESPONSE);
    uint16_t msg_length = htons(12); // MAPPED-ADDRESS: 4-byte header + 8-byte value
    uint32_t magic = htonl(STUN_MAGIC_COOKIE);

    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> tx_id = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C
    };

    std::vector<uint8_t> msg(STUN_HEADER_SIZE + 12);

    // Header
    std::memcpy(msg.data() + 0, &msg_type, 2);
    std::memcpy(msg.data() + 2, &msg_length, 2);
    std::memcpy(msg.data() + 4, &magic, 4);
    std::memcpy(msg.data() + 8, tx_id.data(), STUN_TRANSACTION_ID_SIZE);

    // MAPPED-ADDRESS attribute (IPv4: 192.168.1.1:12345)
    size_t offset = STUN_HEADER_SIZE;
    uint16_t attr_type = htons(STUN_ATTR_MAPPED_ADDRESS);
    uint16_t attr_len = htons(8);
    std::memcpy(msg.data() + offset, &attr_type, 2);
    offset += 2;
    std::memcpy(msg.data() + offset, &attr_len, 2);
    offset += 2;

    // MAPPED-ADDRESS value: reserved(0), family(0x01=IPv4), port, IP
    msg[offset++] = 0x00; // Reserved
    msg[offset++] = 0x01; // IPv4

    uint16_t port = htons(12345);
    std::memcpy(msg.data() + offset, &port, 2);
    offset += 2;

    uint32_t ip = htonl(0xC0A80101); // 192.168.1.1
    std::memcpy(msg.data() + offset, &ip, 4);

    auto decoded = NatTraversal::decodeStunMessage(msg.data(), msg.size());
    ASSERT_TRUE(decoded.has_value());

    EXPECT_EQ(decoded->header.type, STUN_BINDING_RESPONSE);
    EXPECT_EQ(decoded->header.length, 12u);
    EXPECT_EQ(decoded->header.magic_cookie, STUN_MAGIC_COOKIE);
    EXPECT_EQ(decoded->attributes.size(), 1u);
    EXPECT_EQ(decoded->attributes[0].type, STUN_ATTR_MAPPED_ADDRESS);
    EXPECT_EQ(decoded->attributes[0].length, 8u);
}

TEST(NatTraversalTest, DecodeTooShortReturnsNullopt) {
    std::vector<uint8_t> short_msg(10, 0); // Less than 20 bytes
    auto result = NatTraversal::decodeStunMessage(short_msg.data(), short_msg.size());
    EXPECT_FALSE(result.has_value());
}

TEST(NatTraversalTest, DecodeInvalidMagicCookieReturnsNullopt) {
    std::vector<uint8_t> msg(STUN_HEADER_SIZE, 0);

    // Set invalid magic cookie
    uint32_t bad_magic = htonl(0xDEADBEEF);
    std::memcpy(msg.data() + 4, &bad_magic, 4);

    // Set valid type
    uint16_t msg_type = htons(STUN_BINDING_RESPONSE);
    std::memcpy(msg.data(), &msg_type, 2);

    auto result = NatTraversal::decodeStunMessage(msg.data(), msg.size());
    EXPECT_FALSE(result.has_value());
}

// ============================================================
// MAPPED-ADDRESS extraction
// ============================================================

TEST(NatTraversalTest, ExtractMappedAddressIPv4) {
    // Build a STUN message with MAPPED-ADDRESS
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> tx_id = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C
    };

    // Construct attribute value for 192.168.1.100:5000
    StunAttribute attr;
    attr.type = STUN_ATTR_MAPPED_ADDRESS;
    attr.length = 8;
    attr.value.resize(8);
    attr.value[0] = 0x00; // Reserved
    attr.value[1] = 0x01; // IPv4
    uint16_t port = htons(5000);
    std::memcpy(attr.value.data() + 2, &port, 2);
    uint32_t ip = htonl(0xC0A80164); // 192.168.1.100
    std::memcpy(attr.value.data() + 4, &ip, 4);

    StunMessage msg;
    msg.header.type = STUN_BINDING_RESPONSE;
    msg.header.length = 12;
    msg.header.magic_cookie = STUN_MAGIC_COOKIE;
    std::memcpy(msg.header.transaction_id, tx_id.data(), STUN_TRANSACTION_ID_SIZE);
    msg.attributes.push_back(attr);

    auto endpoint = NatTraversal::extractMappedAddress(msg, tx_id);
    ASSERT_TRUE(endpoint.has_value());
    EXPECT_EQ(endpoint->port(), 5000);
    EXPECT_EQ(endpoint->address().to_string(), "192.168.1.100");
}

TEST(NatTraversalTest, ExtractXorMappedAddressIPv4) {
    // Build a STUN message with XOR-MAPPED-ADDRESS
    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> tx_id = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C
    };

    // XOR-MAPPED-ADDRESS for 192.168.1.100:5000
    // Port XOR with high 16 bits of Magic Cookie (0x2112)
    uint16_t xor_port = htons(5000) ^ htons(static_cast<uint16_t>(STUN_MAGIC_COOKIE >> 16));
    // IP XOR with Magic Cookie in network byte order
    uint32_t xor_ip = htonl(0xC0A80164) ^ htonl(STUN_MAGIC_COOKIE);

    StunAttribute attr;
    attr.type = STUN_ATTR_XOR_MAPPED_ADDRESS;
    attr.length = 8;
    attr.value.resize(8);
    attr.value[0] = 0x00; // Reserved
    attr.value[1] = 0x01; // IPv4
    std::memcpy(attr.value.data() + 2, &xor_port, 2);
    std::memcpy(attr.value.data() + 4, &xor_ip, 4);

    StunMessage msg;
    msg.header.type = STUN_BINDING_RESPONSE;
    msg.header.length = 12;
    msg.header.magic_cookie = STUN_MAGIC_COOKIE;
    std::memcpy(msg.header.transaction_id, tx_id.data(), STUN_TRANSACTION_ID_SIZE);
    msg.attributes.push_back(attr);

    auto endpoint = NatTraversal::extractMappedAddress(msg, tx_id);
    ASSERT_TRUE(endpoint.has_value());
    EXPECT_EQ(endpoint->port(), 5000);
    EXPECT_EQ(endpoint->address().to_string(), "192.168.1.100");
}

TEST(NatTraversalTest, ExtractMappedAddressNoAttributeReturnsNullopt) {
    StunMessage msg;
    msg.header.type = STUN_BINDING_RESPONSE;
    msg.header.magic_cookie = STUN_MAGIC_COOKIE;
    // No MAPPED-ADDRESS attribute

    std::array<uint8_t, STUN_TRANSACTION_ID_SIZE> tx_id{};
    auto endpoint = NatTraversal::extractMappedAddress(msg, tx_id);
    EXPECT_FALSE(endpoint.has_value());
}

// ============================================================
// NAT type string representation
// ============================================================

TEST(NatTraversalTest, NatTypeToStringAllTypes) {
    EXPECT_STREQ(natTypeToString(NatType::Open), "Open");
    EXPECT_STREQ(natTypeToString(NatType::FullCone), "FullCone");
    EXPECT_STREQ(natTypeToString(NatType::Restricted), "Restricted");
    EXPECT_STREQ(natTypeToString(NatType::PortRestricted), "PortRestricted");
    EXPECT_STREQ(natTypeToString(NatType::Symmetric), "Symmetric");
    EXPECT_STREQ(natTypeToString(NatType::Blocked), "Blocked");
}

TEST(NatTraversalTest, NatTypeToStringUnknown) {
    // Cast an invalid value to test the default case
    auto unknown = static_cast<NatType>(99);
    EXPECT_STREQ(natTypeToString(unknown), "Unknown");
}

// ============================================================
// Constants validation
// ============================================================

TEST(NatTraversalTest, StunConstantsAreCorrect) {
    EXPECT_EQ(STUN_HEADER_SIZE, 20u);
    EXPECT_EQ(STUN_TRANSACTION_ID_SIZE, 12u);
    EXPECT_EQ(STUN_MAGIC_COOKIE, 0x2112A442u);
    EXPECT_EQ(STUN_BINDING_REQUEST, 0x0001u);
    EXPECT_EQ(STUN_BINDING_RESPONSE, 0x0101u);
    EXPECT_EQ(STUN_ATTR_MAPPED_ADDRESS, 0x0001u);
    EXPECT_EQ(STUN_ATTR_XOR_MAPPED_ADDRESS, 0x0020u);
}

} // namespace
} // namespace nevo
