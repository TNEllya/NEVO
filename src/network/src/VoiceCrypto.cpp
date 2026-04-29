/**
 * @file VoiceCrypto.cpp
 * @brief 语音数据加密/解密模块实现
 *
 * 使用 libsodium 的 crypto_aead_xchacha20poly1305_ietf 算法。
 * XChaCha20-Poly1305 是一种 AEAD（Authenticated Encryption with Associated Data）
 * 算法，提供机密性、完整性和真实性保证。
 *
 * 当 libsodium 不可用时（NEVO_HAS_SODIUM 未定义），提供 stub 实现，
 * 编译可通过但运行时返回错误。
 */

#include "nevo/network/VoiceCrypto.h"

#include <chrono>
#include <cstring>

#ifdef NEVO_HAS_SODIUM
#include <sodium.h>
#endif

#include "nevo/core/common/Logger.h"

namespace nevo {

#ifdef NEVO_HAS_SODIUM

// ============================================================
// 构造 / 析构 / 移动
// ============================================================

VoiceCrypto::VoiceCrypto() {
    // 确保 libsodium 已初始化
    if (sodium_init() < 0) {
        NEVO_LOG_CRITICAL("network", "VoiceCrypto: libsodium initialization failed!");
        // 注意：sodium_init() 可被多次调用，但只有第一次会真正初始化
        // 如果初始化失败，后续所有加密操作都将不安全
    } else {
        NEVO_LOG_DEBUG("network", "VoiceCrypto: initialized (libsodium)");
    }

    // 初始化密钥为零（不安全状态，必须调用 setSessionKey 后才能加密）
    current_key_.fill(0);
    old_key_.fill(0);
}

VoiceCrypto::~VoiceCrypto() {
    // 安全擦除密钥内存
    sodium_memzero(current_key_.data(), current_key_.size());
    sodium_memzero(old_key_.data(), old_key_.size());
    NEVO_LOG_DEBUG("network", "VoiceCrypto: destroyed, keys wiped");
}

VoiceCrypto::VoiceCrypto(VoiceCrypto&& other) noexcept
    : current_key_(other.current_key_)
    , old_key_(other.old_key_)
    , has_old_key_(other.has_old_key_)
    , old_key_expiry_time_(other.old_key_expiry_time_)
    , nonce_counter_(other.nonce_counter_.load())
{
    // 安全擦除源对象密钥
    sodium_memzero(other.current_key_.data(), other.current_key_.size());
    sodium_memzero(other.old_key_.data(), other.old_key_.size());
    other.has_old_key_ = false;
}

VoiceCrypto& VoiceCrypto::operator=(VoiceCrypto&& other) noexcept {
    if (this != &other) {
        // 擦除当前密钥
        sodium_memzero(current_key_.data(), current_key_.size());
        sodium_memzero(old_key_.data(), old_key_.size());

        current_key_ = other.current_key_;
        old_key_ = other.old_key_;
        has_old_key_ = other.has_old_key_;
        old_key_expiry_time_ = other.old_key_expiry_time_;
        nonce_counter_.store(other.nonce_counter_.load());

        // 擦除源对象密钥
        sodium_memzero(other.current_key_.data(), other.current_key_.size());
        sodium_memzero(other.old_key_.data(), other.old_key_.size());
        other.has_old_key_ = false;
    }
    return *this;
}

// ============================================================
// 密钥管理
// ============================================================

void VoiceCrypto::setSessionKey(const uint8_t key[CRYPTO_KEY_SIZE]) {
    std::lock_guard<std::mutex> lock(key_mutex_);

    // 安全拷贝新密钥
    std::memcpy(current_key_.data(), key, CRYPTO_KEY_SIZE);

    // 清除旧密钥（首次设置密钥时无旧密钥）
    sodium_memzero(old_key_.data(), old_key_.size());
    has_old_key_ = false;
    old_key_expiry_time_ = 0;

    // 重置 nonce 计数器
    nonce_counter_.store(0, std::memory_order_release);

    NEVO_LOG_INFO("network", "VoiceCrypto: session key set, nonce counter reset");
}

void VoiceCrypto::rotateKey(const uint8_t new_key[CRYPTO_KEY_SIZE]) {
    std::lock_guard<std::mutex> lock(key_mutex_);

    // 将当前密钥移入旧密钥槽位
    std::memcpy(old_key_.data(), current_key_.data(), CRYPTO_KEY_SIZE);
    has_old_key_ = true;
    old_key_expiry_time_ = currentTimestampSeconds() + CRYPTO_KEY_OVERLAP_SECONDS;

    // 设置新密钥
    std::memcpy(current_key_.data(), new_key, CRYPTO_KEY_SIZE);

    // 注意：密钥轮换时不重置 nonce 计数器！
    // nonce 空间为 64 位（实际使用 24 字节 = 192 位），计数器从当前值继续递增。
    // 即使计数器回绕（极端情况），nonce 空间也足够大不会碰撞。

    NEVO_LOG_INFO("network",
                  "VoiceCrypto: key rotated, old key expires in {}s",
                  CRYPTO_KEY_OVERLAP_SECONDS);
}

bool VoiceCrypto::hasOldKey() const {
    if (!has_old_key_) {
        return false;
    }
    // 检查旧密钥是否已过期
    return currentTimestampSeconds() < old_key_expiry_time_;
}

void VoiceCrypto::purgeExpiredOldKey() {
    if (has_old_key_ && currentTimestampSeconds() >= old_key_expiry_time_) {
        sodium_memzero(old_key_.data(), old_key_.size());
        has_old_key_ = false;
        NEVO_LOG_INFO("network", "VoiceCrypto: old key purged (expired)");
    }
}

// ============================================================
// 加密
// ============================================================

std::vector<uint8_t> VoiceCrypto::encrypt(
    const uint8_t* plaintext,
    size_t pt_len,
    const uint8_t* header_aad,
    size_t aad_len)
{
    // 计算输出帧大小：nonce(24) + ciphertext(pt_len) + tag(16)
    size_t encrypted_len = encryptedSize(pt_len);
    std::vector<uint8_t> output(encrypted_len, 0);

    // 生成 nonce：递增计数器 → 24 字节
    uint64_t counter = nonce_counter_.fetch_add(1, std::memory_order_relaxed);
    auto nonce = generateNonce(counter);

    // 写入 nonce 到输出帧头部
    std::memcpy(output.data(), nonce.data(), XCHACHA_NONCE_SIZE);

    // 拷贝当前密钥（在锁保护下），避免加密期间密钥被并发修改
    std::array<uint8_t, CRYPTO_KEY_SIZE> key_copy;
    {
        std::lock_guard<std::mutex> lock(key_mutex_);
        key_copy = current_key_;
    }

    // 调用 libsodium AEAD 加密
    unsigned long long clen = 0;
    int result = crypto_aead_xchacha20poly1305_ietf_encrypt(
        output.data() + XCHACHA_NONCE_SIZE,  // 输出：密文 + tag
        &clen,                                 // 输出密文长度（含 tag）
        plaintext,                             // 明文
        pt_len,                                // 明文长度
        header_aad,                            // 附加认证数据
        aad_len,                               // AAD 长度
        nullptr,                               // nsec（保留，传 NULL）
        nonce.data(),                          // nonce
        key_copy.data()                        // 密钥副本
    );

    if (result != 0) {
        NEVO_LOG_ERROR("network", "VoiceCrypto::encrypt: libsodium encryption failed!");
        // 返回空帧表示失败
        output.clear();
        return output;
    }

    NEVO_LOG_TRACE("network",
                    "VoiceCrypto::encrypt: pt_len={}, ct_len={}, nonce_counter={}",
                    pt_len, clen, counter);

    return output;
}

// ============================================================
// 解密
// ============================================================

std::optional<std::vector<uint8_t>> VoiceCrypto::decrypt(
    const uint8_t* ciphertext,
    size_t ct_len,
    const uint8_t* nonce,
    size_t nonce_len,
    const uint8_t* header_aad,
    size_t aad_len)
{
    // 验证 nonce 长度
    if (nonce_len != XCHACHA_NONCE_SIZE) {
        NEVO_LOG_WARN("network",
                      "VoiceCrypto::decrypt: invalid nonce length {} (expected {})",
                      nonce_len, XCHACHA_NONCE_SIZE);
        return std::nullopt;
    }

    // 验证密文最小长度（至少包含 tag）
    if (ct_len < POLY1305_TAG_SIZE) {
        NEVO_LOG_WARN("network",
                      "VoiceCrypto::decrypt: ciphertext too short ({})",
                      ct_len);
        return std::nullopt;
    }

    // 拷贝密钥（在锁保护下），避免解密期间密钥被并发修改
    std::array<uint8_t, CRYPTO_KEY_SIZE> key_copy;
    std::array<uint8_t, CRYPTO_KEY_SIZE> old_key_copy;
    bool has_old;
    {
        std::lock_guard<std::mutex> lock(key_mutex_);
        key_copy = current_key_;
        old_key_copy = old_key_;
        has_old = has_old_key_ && (currentTimestampSeconds() < old_key_expiry_time_);
    }

    // 首先尝试用当前密钥解密
    auto result = decryptWithKey(
        key_copy.data(), ciphertext, ct_len,
        nonce, nonce_len, header_aad, aad_len);

    if (result.has_value()) {
        NEVO_LOG_TRACE("network", "VoiceCrypto::decrypt: success with current key");
        return result;
    }

    // 当前密钥解密失败，尝试旧密钥（如果存在且未过期）
    if (has_old) {
        NEVO_LOG_DEBUG("network", "VoiceCrypto::decrypt: trying old key for overlapping packet");
        result = decryptWithKey(
            old_key_copy.data(), ciphertext, ct_len,
            nonce, nonce_len, header_aad, aad_len);

        if (result.has_value()) {
            NEVO_LOG_DEBUG("network", "VoiceCrypto::decrypt: success with old key");
            return result;
        }
    }

    NEVO_LOG_WARN("network", "VoiceCrypto::decrypt: authentication failed (both keys)");
    return std::nullopt;
}

// ============================================================
// 内部辅助方法
// ============================================================

std::array<uint8_t, XCHACHA_NONCE_SIZE> VoiceCrypto::generateNonce(uint64_t counter) {
    std::array<uint8_t, XCHACHA_NONCE_SIZE> nonce{};

    // 将 64 位计数器放入 nonce 的低 8 字节（小端序），
    // 高 16 字节保持为零。这在实际使用中是安全的，因为：
    //   1. 64 位计数器空间极大（2^64 = 1.8 * 10^19 次加密）
    //   2. 即使每秒 1000 次加密，也需要约 5.8 亿年才会回绕
    //   3. 每次密钥设置时计数器重置，进一步保证安全
    //
    // 替代方案：使用随机 nonce（crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
    // 为 24 字节，随机碰撞概率极低），但递增计数器更简单且可排序。
    uint64_t counter_be = counter; // 直接使用，nonce 内部格式不要求端序
    std::memcpy(nonce.data(), &counter_be, sizeof(counter_be));
    // 高 16 字节已初始化为 0

    return nonce;
}

std::optional<std::vector<uint8_t>> VoiceCrypto::decryptWithKey(
    const uint8_t* key,
    const uint8_t* ciphertext,
    size_t ct_len,
    const uint8_t* nonce,
    size_t nonce_len,
    const uint8_t* header_aad,
    size_t aad_len)
{
    // 明文长度 = 密文长度 - tag 长度
    size_t pt_len = ct_len - POLY1305_TAG_SIZE;
    std::vector<uint8_t> plaintext(pt_len, 0);

    unsigned long long mlen = 0;
    int result = crypto_aead_xchacha20poly1305_ietf_decrypt(
        plaintext.data(),       // 输出：明文
        &mlen,                  // 输出明文长度
        nullptr,                // nsec（保留，传 NULL）
        ciphertext,             // 密文（含 tag）
        ct_len,                 // 密文长度
        header_aad,             // 附加认证数据
        aad_len,                // AAD 长度
        nonce,                  // nonce
        key                     // 密钥
    );

    if (result != 0) {
        // 认证失败——密钥不匹配或数据被篡改
        return std::nullopt;
    }

    // 调整明文大小（理论上 mlen == pt_len，但以防万一）
    plaintext.resize(mlen);

    return plaintext;
}

#else // !NEVO_HAS_SODIUM

// ============================================================
// Stub implementation when libsodium is not available
// ============================================================

VoiceCrypto::VoiceCrypto() {
    current_key_.fill(0);
    old_key_.fill(0);
    NEVO_LOG_WARN("network", "VoiceCrypto: initialized as stub (libsodium not available)");
}

VoiceCrypto::~VoiceCrypto() {
    // No sodium_memzero available — use volatile pointer to prevent
    // the compiler from optimizing away the zeroing (dead store elimination).
    volatile uint8_t* p = current_key_.data();
    for (size_t i = 0; i < current_key_.size(); ++i) { p[i] = 0; }
    p = old_key_.data();
    for (size_t i = 0; i < old_key_.size(); ++i) { p[i] = 0; }
}

VoiceCrypto::VoiceCrypto(VoiceCrypto&& other) noexcept
    : current_key_(other.current_key_)
    , old_key_(other.old_key_)
    , has_old_key_(other.has_old_key_)
    , old_key_expiry_time_(other.old_key_expiry_time_)
    , nonce_counter_(other.nonce_counter_.load())
{
    // Use volatile pointer to prevent dead store elimination
    volatile uint8_t* p = other.current_key_.data();
    for (size_t i = 0; i < other.current_key_.size(); ++i) { p[i] = 0; }
    p = other.old_key_.data();
    for (size_t i = 0; i < other.old_key_.size(); ++i) { p[i] = 0; }
    other.has_old_key_ = false;
}

VoiceCrypto& VoiceCrypto::operator=(VoiceCrypto&& other) noexcept {
    if (this != &other) {
        // Wipe current keys with volatile to prevent dead store elimination
        volatile uint8_t* p = current_key_.data();
        for (size_t i = 0; i < current_key_.size(); ++i) { p[i] = 0; }
        p = old_key_.data();
        for (size_t i = 0; i < old_key_.size(); ++i) { p[i] = 0; }

        current_key_ = other.current_key_;
        old_key_ = other.old_key_;
        has_old_key_ = other.has_old_key_;
        old_key_expiry_time_ = other.old_key_expiry_time_;
        nonce_counter_.store(other.nonce_counter_.load());

        // Wipe source keys with volatile
        p = other.current_key_.data();
        for (size_t i = 0; i < other.current_key_.size(); ++i) { p[i] = 0; }
        p = other.old_key_.data();
        for (size_t i = 0; i < other.old_key_.size(); ++i) { p[i] = 0; }
        other.has_old_key_ = false;
    }
    return *this;
}

void VoiceCrypto::setSessionKey(const uint8_t key[CRYPTO_KEY_SIZE]) {
    std::memcpy(current_key_.data(), key, CRYPTO_KEY_SIZE);
    old_key_.fill(0);
    has_old_key_ = false;
    old_key_expiry_time_ = 0;
    nonce_counter_.store(0, std::memory_order_release);
    NEVO_LOG_WARN("network", "VoiceCrypto::setSessionKey: stub (libsodium not available)");
}

void VoiceCrypto::rotateKey(const uint8_t new_key[CRYPTO_KEY_SIZE]) {
    old_key_ = current_key_;
    has_old_key_ = true;
    old_key_expiry_time_ = currentTimestampSeconds() + CRYPTO_KEY_OVERLAP_SECONDS;
    std::memcpy(current_key_.data(), new_key, CRYPTO_KEY_SIZE);
    NEVO_LOG_WARN("network", "VoiceCrypto::rotateKey: stub (libsodium not available)");
}

bool VoiceCrypto::hasOldKey() const {
    if (!has_old_key_) return false;
    return currentTimestampSeconds() < old_key_expiry_time_;
}

void VoiceCrypto::purgeExpiredOldKey() {
    if (has_old_key_ && currentTimestampSeconds() >= old_key_expiry_time_) {
        old_key_.fill(0);
        has_old_key_ = false;
    }
}

std::vector<uint8_t> VoiceCrypto::encrypt(
    const uint8_t* /*plaintext*/,
    size_t pt_len,
    const uint8_t* /*header_aad*/,
    size_t /*aad_len*/)
{
    NEVO_LOG_ERROR("network", "VoiceCrypto::encrypt: not available (built without libsodium)");
    return {};
}

std::optional<std::vector<uint8_t>> VoiceCrypto::decrypt(
    const uint8_t* /*ciphertext*/,
    size_t /*ct_len*/,
    const uint8_t* /*nonce*/,
    size_t /*nonce_len*/,
    const uint8_t* /*header_aad*/,
    size_t /*aad_len*/)
{
    NEVO_LOG_ERROR("network", "VoiceCrypto::decrypt: not available (built without libsodium)");
    return std::nullopt;
}

std::array<uint8_t, XCHACHA_NONCE_SIZE> VoiceCrypto::generateNonce(uint64_t counter) {
    std::array<uint8_t, XCHACHA_NONCE_SIZE> nonce{};
    std::memcpy(nonce.data(), &counter, sizeof(counter));
    return nonce;
}

#endif // NEVO_HAS_SODIUM

int64_t VoiceCrypto::currentTimestampSeconds() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}

} // namespace nevo
