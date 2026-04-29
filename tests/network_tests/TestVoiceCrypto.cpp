/**
 * @file TestVoiceCrypto.cpp
 * @brief Unit tests for voice data encryption/decryption module
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>

#include "nevo/network/VoiceCrypto.h"

namespace nevo {
namespace {

// Helper: generate a random key
static std::array<uint8_t, CRYPTO_KEY_SIZE> makeKey(uint8_t seed) {
    std::array<uint8_t, CRYPTO_KEY_SIZE> key;
    for (size_t i = 0; i < CRYPTO_KEY_SIZE; ++i) {
        key[i] = static_cast<uint8_t>(seed ^ i);
    }
    return key;
}

// ============================================================
// Encrypt-then-decrypt roundtrip
// ============================================================

TEST(VoiceCryptoTest, EncryptDecryptRoundtrip) {
    VoiceCrypto crypto;
    auto key = makeKey(0xAB);
    crypto.setSessionKey(key.data());

    // Plaintext: simulated Opus frame
    std::vector<uint8_t> plaintext(80, 0);
    for (size_t i = 0; i < plaintext.size(); ++i) {
        plaintext[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // Encrypt
    uint8_t aad[] = {0x01, 0x02, 0x03, 0x04};
    auto encrypted = crypto.encrypt(plaintext.data(), plaintext.size(), aad, sizeof(aad));

    // Verify encrypted frame format: [nonce(24)][ciphertext][tag(16)]
    EXPECT_EQ(encrypted.size(), VoiceCrypto::encryptedSize(plaintext.size()));
    EXPECT_GE(encrypted.size(), XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE);

    // Extract nonce and ciphertext
    const uint8_t* nonce_ptr = encrypted.data();
    const uint8_t* ct_ptr = encrypted.data() + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted.size() - XCHACHA_NONCE_SIZE;

    // Decrypt
    auto decrypted = crypto.decrypt(ct_ptr, ct_len, nonce_ptr, XCHACHA_NONCE_SIZE,
                                     aad, sizeof(aad));

    ASSERT_TRUE(decrypted.has_value()) << "Decryption failed";
    EXPECT_EQ(decrypted->size(), plaintext.size());

    // Verify plaintext matches
    for (size_t i = 0; i < plaintext.size(); ++i) {
        EXPECT_EQ((*decrypted)[i], plaintext[i]) << "Mismatch at byte " << i;
    }
}

TEST(VoiceCryptoTest, RoundtripWithEmptyPlaintext) {
    VoiceCrypto crypto;
    auto key = makeKey(0x42);
    crypto.setSessionKey(key.data());

    std::vector<uint8_t> plaintext;

    auto encrypted = crypto.encrypt(plaintext.data(), 0, nullptr, 0);
    EXPECT_EQ(encrypted.size(), VoiceCrypto::encryptedSize(0));

    const uint8_t* nonce_ptr = encrypted.data();
    const uint8_t* ct_ptr = encrypted.data() + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted.size() - XCHACHA_NONCE_SIZE;

    auto decrypted = crypto.decrypt(ct_ptr, ct_len, nonce_ptr, XCHACHA_NONCE_SIZE,
                                     nullptr, 0);

    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(decrypted->size(), 0u);
}

TEST(VoiceCryptoTest, RoundtripWithLargePayload) {
    VoiceCrypto crypto;
    auto key = makeKey(0x77);
    crypto.setSessionKey(key.data());

    // 1400-byte payload (typical UDP voice packet)
    std::vector<uint8_t> plaintext(1400);
    for (size_t i = 0; i < plaintext.size(); ++i) {
        plaintext[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
    }

    auto encrypted = crypto.encrypt(plaintext.data(), plaintext.size(), nullptr, 0);

    const uint8_t* nonce_ptr = encrypted.data();
    const uint8_t* ct_ptr = encrypted.data() + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted.size() - XCHACHA_NONCE_SIZE;

    auto decrypted = crypto.decrypt(ct_ptr, ct_len, nonce_ptr, XCHACHA_NONCE_SIZE,
                                     nullptr, 0);

    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(decrypted->size(), plaintext.size());
    EXPECT_EQ(std::memcmp(decrypted->data(), plaintext.data(), plaintext.size()), 0);
}

// ============================================================
// Authentication failure on tampered data
// ============================================================

TEST(VoiceCryptoTest, TamperedCiphertextFailsAuthentication) {
    VoiceCrypto crypto;
    auto key = makeKey(0x11);
    crypto.setSessionKey(key.data());

    std::vector<uint8_t> plaintext(64, 0xAA);
    auto encrypted = crypto.encrypt(plaintext.data(), plaintext.size(), nullptr, 0);

    // Tamper with a ciphertext byte
    encrypted[XCHACHA_NONCE_SIZE + 5] ^= 0xFF;

    const uint8_t* nonce_ptr = encrypted.data();
    const uint8_t* ct_ptr = encrypted.data() + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted.size() - XCHACHA_NONCE_SIZE;

    auto decrypted = crypto.decrypt(ct_ptr, ct_len, nonce_ptr, XCHACHA_NONCE_SIZE,
                                     nullptr, 0);
    EXPECT_FALSE(decrypted.has_value()) << "Tampered data should fail authentication";
}

TEST(VoiceCryptoTest, TamperedNonceFailsAuthentication) {
    VoiceCrypto crypto;
    auto key = makeKey(0x22);
    crypto.setSessionKey(key.data());

    std::vector<uint8_t> plaintext(64, 0xBB);
    auto encrypted = crypto.encrypt(plaintext.data(), plaintext.size(), nullptr, 0);

    // Tamper with nonce byte
    encrypted[10] ^= 0x01;

    const uint8_t* nonce_ptr = encrypted.data();
    const uint8_t* ct_ptr = encrypted.data() + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted.size() - XCHACHA_NONCE_SIZE;

    auto decrypted = crypto.decrypt(ct_ptr, ct_len, nonce_ptr, XCHACHA_NONCE_SIZE,
                                     nullptr, 0);
    EXPECT_FALSE(decrypted.has_value()) << "Tampered nonce should fail authentication";
}

TEST(VoiceCryptoTest, WrongKeyFailsAuthentication) {
    VoiceCrypto encryptor;
    VoiceCrypto decryptor;

    auto key1 = makeKey(0x01);
    auto key2 = makeKey(0x02);

    encryptor.setSessionKey(key1.data());
    decryptor.setSessionKey(key2.data());

    std::vector<uint8_t> plaintext(64, 0xCC);
    auto encrypted = encryptor.encrypt(plaintext.data(), plaintext.size(), nullptr, 0);

    const uint8_t* nonce_ptr = encrypted.data();
    const uint8_t* ct_ptr = encrypted.data() + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted.size() - XCHACHA_NONCE_SIZE;

    auto decrypted = decryptor.decrypt(ct_ptr, ct_len, nonce_ptr, XCHACHA_NONCE_SIZE,
                                        nullptr, 0);
    EXPECT_FALSE(decrypted.has_value()) << "Wrong key should fail authentication";
}

TEST(VoiceCryptoTest, TamperedAADFailsAuthentication) {
    VoiceCrypto crypto;
    auto key = makeKey(0x33);
    crypto.setSessionKey(key.data());

    uint8_t aad[] = {0xAA, 0xBB, 0xCC, 0xDD};
    std::vector<uint8_t> plaintext(64, 0xDD);

    auto encrypted = crypto.encrypt(plaintext.data(), plaintext.size(), aad, sizeof(aad));

    const uint8_t* nonce_ptr = encrypted.data();
    const uint8_t* ct_ptr = encrypted.data() + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted.size() - XCHACHA_NONCE_SIZE;

    // Decrypt with modified AAD
    uint8_t wrong_aad[] = {0xAA, 0xBB, 0xCC, 0xEE}; // Last byte changed
    auto decrypted = crypto.decrypt(ct_ptr, ct_len, nonce_ptr, XCHACHA_NONCE_SIZE,
                                     wrong_aad, sizeof(wrong_aad));
    EXPECT_FALSE(decrypted.has_value()) << "Wrong AAD should fail authentication";
}

// ============================================================
// Key rotation: old key still works during overlap
// ============================================================

TEST(VoiceCryptoTest, KeyRotationOldKeyStillWorks) {
    VoiceCrypto encryptor;
    VoiceCrypto decryptor;

    auto key1 = makeKey(0x10);
    auto key2 = makeKey(0x20);

    encryptor.setSessionKey(key1.data());
    decryptor.setSessionKey(key1.data());

    // Encrypt with old key
    std::vector<uint8_t> plaintext1(64, 0x11);
    auto encrypted1 = encryptor.encrypt(plaintext1.data(), plaintext1.size(), nullptr, 0);

    // Rotate keys on both sides
    encryptor.rotateKey(key2.data());
    decryptor.rotateKey(key2.data());

    EXPECT_TRUE(decryptor.hasOldKey());

    // Decrypt the old-key encrypted data with new-key decryptor
    // (should fall back to old key)
    const uint8_t* nonce1 = encrypted1.data();
    const uint8_t* ct1 = encrypted1.data() + XCHACHA_NONCE_SIZE;
    size_t ct1_len = encrypted1.size() - XCHACHA_NONCE_SIZE;

    auto decrypted1 = decryptor.decrypt(ct1, ct1_len, nonce1, XCHACHA_NONCE_SIZE,
                                         nullptr, 0);
    ASSERT_TRUE(decrypted1.has_value()) << "Old key should still decrypt during overlap";
    EXPECT_EQ(decrypted1->size(), plaintext1.size());
    EXPECT_EQ(std::memcmp(decrypted1->data(), plaintext1.data(), plaintext1.size()), 0);

    // Encrypt with new key and verify it works too
    std::vector<uint8_t> plaintext2(64, 0x22);
    auto encrypted2 = encryptor.encrypt(plaintext2.data(), plaintext2.size(), nullptr, 0);

    const uint8_t* nonce2 = encrypted2.data();
    const uint8_t* ct2 = encrypted2.data() + XCHACHA_NONCE_SIZE;
    size_t ct2_len = encrypted2.size() - XCHACHA_NONCE_SIZE;

    auto decrypted2 = decryptor.decrypt(ct2, ct2_len, nonce2, XCHACHA_NONCE_SIZE,
                                         nullptr, 0);
    ASSERT_TRUE(decrypted2.has_value()) << "New key should work";
    EXPECT_EQ(std::memcmp(decrypted2->data(), plaintext2.data(), plaintext2.size()), 0);
}

TEST(VoiceCryptoTest, HasOldKeyAfterRotation) {
    VoiceCrypto crypto;
    auto key1 = makeKey(0x10);
    auto key2 = makeKey(0x20);

    crypto.setSessionKey(key1.data());
    EXPECT_FALSE(crypto.hasOldKey());

    crypto.rotateKey(key2.data());
    EXPECT_TRUE(crypto.hasOldKey());
}

TEST(VoiceCryptoTest, SetSessionKeyClearsOldKey) {
    VoiceCrypto crypto;
    auto key1 = makeKey(0x10);
    auto key2 = makeKey(0x20);

    crypto.setSessionKey(key1.data());
    crypto.rotateKey(key2.data());
    EXPECT_TRUE(crypto.hasOldKey());

    // setSessionKey clears old key
    auto key3 = makeKey(0x30);
    crypto.setSessionKey(key3.data());
    EXPECT_FALSE(crypto.hasOldKey());
}

// ============================================================
// Nonce uniqueness
// ============================================================

TEST(VoiceCryptoTest, NonceUniquenessAcrossEncryptions) {
    VoiceCrypto crypto;
    auto key = makeKey(0x55);
    crypto.setSessionKey(key.data());

    std::vector<uint8_t> plaintext(32, 0xFF);
    const int num_encryptions = 100;

    std::vector<std::array<uint8_t, XCHACHA_NONCE_SIZE>> nonces;
    nonces.reserve(num_encryptions);

    for (int i = 0; i < num_encryptions; ++i) {
        auto encrypted = crypto.encrypt(plaintext.data(), plaintext.size(), nullptr, 0);
        ASSERT_EQ(encrypted.size(), VoiceCrypto::encryptedSize(plaintext.size()));

        std::array<uint8_t, XCHACHA_NONCE_SIZE> nonce;
        std::memcpy(nonce.data(), encrypted.data(), XCHACHA_NONCE_SIZE);
        nonces.push_back(nonce);
    }

    // Verify all nonces are unique
    for (size_t i = 0; i < nonces.size(); ++i) {
        for (size_t j = i + 1; j < nonces.size(); ++j) {
            EXPECT_NE(nonces[i], nonces[j])
                << "Duplicate nonce at indices " << i << " and " << j;
        }
    }
}

TEST(VoiceCryptoTest, EncryptedSizeCalculation) {
    EXPECT_EQ(VoiceCrypto::encryptedSize(0), XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE);
    EXPECT_EQ(VoiceCrypto::encryptedSize(100), XCHACHA_NONCE_SIZE + 100 + POLY1305_TAG_SIZE);
}

TEST(VoiceCryptoTest, PlaintextSizeCalculation) {
    EXPECT_EQ(VoiceCrypto::plaintextSize(POLY1305_TAG_SIZE), 0u);
    EXPECT_EQ(VoiceCrypto::plaintextSize(POLY1305_TAG_SIZE + 100), 100u);
    EXPECT_EQ(VoiceCrypto::plaintextSize(10), 0u); // Less than tag size
}

} // namespace
} // namespace nevo
