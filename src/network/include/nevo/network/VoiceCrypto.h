#pragma once
/**
 * @file VoiceCrypto.h
 * @brief 语音数据加密/解密模块
 *
 * 使用 libsodium 的 XChaCha20-Poly1305 AEAD 算法对 UDP 语音载荷进行加密。
 * XChaCha20-Poly1305 在所有 CPU 平台上均有良好性能，不依赖 AES-NI 指令集。
 *
 * 密钥管理：
 *   - setSessionKey(): 设置当前会话密钥（32 字节）
 *   - rotateKey(): 密钥轮换，旧密钥保留一段重叠期以处理过渡期数据包
 *
 * Nonce 方案：
 *   - 使用原子递增计数器生成 12 字节 nonce
 *   - 发送端单调递增，接收端不验证顺序（允许丢包/乱序）
 *
 * 帧格式：
 *   [12-byte nonce][ciphertext][16-byte auth tag]
 *   - nonce: 明文传输，每包不同
 *   - ciphertext: ChaCha20 加密的语音数据
 *   - auth tag: Poly1305 认证标签，同时覆盖 AAD（包头）
 *
 * AAD（Additional Authenticated Data）：
 *   - 通常包含 UDP 包头（不含载荷），用于绑定加密数据与包元数据
 *   - 防止包头被篡改（如序列号、时间戳等）
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

#include "nevo/core/common/Result.h"
#include "nevo/core/protocol/PacketTypes.h"

namespace nevo {

// ============================================================
// 加密常量
// ============================================================

/// XChaCha20-Poly1305 密钥长度（32 字节）
inline constexpr size_t CRYPTO_KEY_SIZE = 32;

/// XChaCha20-Poly1305 Nonce 长度（24 字节）
/// 注意：虽然用户需求说 AES_GCM_NONCE_SIZE=12，但 XChaCha20-Poly1305 使用 24 字节 nonce
/// 我们仍保留 PacketTypes.h 中定义的 AES_GCM_NONCE_SIZE=12 常量以兼容协议定义
inline constexpr size_t XCHACHA_NONCE_SIZE = 24;

/// Poly1305 认证标签长度（16 字节）
inline constexpr size_t POLY1305_TAG_SIZE = 16;

/// 旧密钥保留窗口（秒）——密钥轮换后旧密钥保持可用的时长
inline constexpr uint32_t CRYPTO_KEY_OVERLAP_SECONDS = KEY_OVERLAP_WINDOW_SEC;

// ============================================================
// VoiceCrypto 类
// ============================================================

/**
 * @class VoiceCrypto
 * @brief UDP 语音载荷加密/解密
 *
 * 使用 libsodium 的 crypto_aead_xchacha20poly1305_ietf 进行 AEAD 加密。
 * 支持密钥轮换：新密钥生效后，旧密钥保留一段重叠期，
 * 以确保在密钥切换过渡期间到达的数据包仍能正确解密。
 *
 * 线程安全说明：
 *   - encrypt() 线程安全（原子 nonce 计数器）
 *   - decrypt() 线程安全（只读密钥）
 *   - setSessionKey() / rotateKey() 非线程安全，需在外部同步
 *
 * 典型用法：
 * @code
 *   VoiceCrypto crypto;
 *   crypto.setSessionKey(key);
 *
 *   // 发送端
 *   auto encrypted = crypto.encrypt(plaintext, pt_len, header_aad, aad_len);
 *   send(encrypted.data(), encrypted.size());
 *
 *   // 接收端
 *   auto decrypted = crypto.decrypt(ct, ct_len, nonce, nonce_len, header_aad, aad_len);
 *   if (decrypted) { use(*decrypted); }
 *   else { // 认证失败，丢弃包 }
 * @endcode
 */
class VoiceCrypto {
public:
    /// 构造函数
    VoiceCrypto();

    /// 析构函数
    ~VoiceCrypto();

    // ----- 禁止拷贝，允许移动 -----
    VoiceCrypto(const VoiceCrypto&) = delete;
    VoiceCrypto& operator=(const VoiceCrypto&) = delete;
    VoiceCrypto(VoiceCrypto&&) noexcept;
    VoiceCrypto& operator=(VoiceCrypto&&) noexcept;

    // ============================================================
    // 密钥管理
    // ============================================================

    /**
     * @brief 设置当前会话密钥
     *
     * @param key 32 字节密钥（XChaCha20-Poly1305 使用 256 位密钥）
     *
     * 注意：此操作会清空旧密钥缓冲。如果是密钥轮换，
     * 应使用 rotateKey() 而非直接调用 setSessionKey()。
     */
    void setSessionKey(const uint8_t key[CRYPTO_KEY_SIZE]);

    /**
     * @brief 密钥轮换
     *
     * 将当前密钥移入旧密钥槽位，设置新密钥为当前密钥。
     * 旧密钥保留 CRYPTO_KEY_OVERLAP_SECONDS 秒，
     * 在此期间 decrypt() 会同时尝试用新密钥和旧密钥解密。
     *
     * @param new_key 32 字节新密钥
     */
    void rotateKey(const uint8_t new_key[CRYPTO_KEY_SIZE]);

    /**
     * @brief 检查是否有旧密钥（仍在重叠期内）
     * @return true 如果旧密钥存在且未过期
     */
    bool hasOldKey() const;

    /**
     * @brief 清除过期的旧密钥
     *
     * 应定期调用（如每秒一次），清理超过重叠窗口的旧密钥。
     */
    void purgeExpiredOldKey();

    // ============================================================
    // 加密 / 解密
    // ============================================================

    /**
     * @brief 加密语音数据
     *
     * 使用 XChaCha20-Poly1305 AEAD 加密，输出格式：
     *   [nonce (24 bytes)][ciphertext][auth tag (16 bytes)]
     *
     * nonce 由内部原子计数器递增生成，保证每包不同。
     * 认证标签同时覆盖 ciphertext 和 AAD。
     *
     * @param plaintext  明文数据指针（语音编码后的载荷）
     * @param pt_len     明文长度
     * @param header_aad 附加认证数据（通常为 UDP 包头）
     * @param aad_len    AAD 长度
     * @return std::vector<uint8_t> 加密后的完整帧（nonce + ciphertext + tag）
     */
    std::vector<uint8_t> encrypt(
        const uint8_t* plaintext,
        size_t pt_len,
        const uint8_t* header_aad,
        size_t aad_len);

    /**
     * @brief 解密语音数据
     *
     * 首先尝试用当前密钥解密，如果认证失败且有旧密钥，
     * 则尝试用旧密钥解密（处理密钥轮换过渡期的包）。
     *
     * @param ciphertext  密文指针（不含 nonce，仅 ciphertext + tag）
     * @param ct_len      密文长度（含 tag）
     * @param nonce       nonce 指针
     * @param nonce_len   nonce 长度（应为 24 字节）
     * @param header_aad  附加认证数据
     * @param aad_len     AAD 长度
     * @return std::optional<std::vector<uint8_t>> 解密成功返回明文，失败返回 nullopt
     */
    std::optional<std::vector<uint8_t>> decrypt(
        const uint8_t* ciphertext,
        size_t ct_len,
        const uint8_t* nonce,
        size_t nonce_len,
        const uint8_t* header_aad,
        size_t aad_len);

    // ============================================================
    // 工具方法
    // ============================================================

    /**
     * @brief 计算加密后帧的总长度
     * @param plaintext_len 明文长度
     * @return 加密帧总长度 = nonce(24) + pt_len + tag(16)
     */
    static size_t encryptedSize(size_t plaintext_len) {
        return XCHACHA_NONCE_SIZE + plaintext_len + POLY1305_TAG_SIZE;
    }

    /**
     * @brief 计算密文中明文的长度
     * @param ct_len 密文长度（含 tag，不含 nonce）
     * @return 明文长度 = ct_len - tag(16)；如果 ct_len < tag 则返回 0
     */
    static size_t plaintextSize(size_t ct_len) {
        return ct_len >= POLY1305_TAG_SIZE ? ct_len - POLY1305_TAG_SIZE : 0;
    }

    /**
     * @brief 使用指定密钥解密（公开静态方法，供服务端 AudioRelay 使用）
     *
     * @param key        密钥指针
     * @param ciphertext 密文
     * @param ct_len     密文长度
     * @param nonce      nonce
     * @param nonce_len  nonce 长度
     * @param header_aad AAD
     * @param aad_len    AAD 长度
     * @return 解密成功返回明文，失败返回 nullopt
     */
    static std::optional<std::vector<uint8_t>> decryptWithKey(
        const uint8_t* key,
        const uint8_t* ciphertext,
        size_t ct_len,
        const uint8_t* nonce,
        size_t nonce_len,
        const uint8_t* header_aad,
        size_t aad_len);

private:
    // ---- 密钥同步 ----
    /// 保护密钥读写操作的互斥锁
    /// encrypt/decrypt 从音频线程读取密钥，setSessionKey/rotateKey 从 IO 线程写入密钥
    mutable std::mutex key_mutex_;

    // ---- 密钥存储 ----
    /// 当前会话密钥
    std::array<uint8_t, CRYPTO_KEY_SIZE> current_key_{};

    /// 旧密钥（密钥轮换时保留）
    std::array<uint8_t, CRYPTO_KEY_SIZE> old_key_{};

    /// 是否存在有效的旧密钥
    bool has_old_key_ = false;

    /// 旧密钥的过期时间戳（自 epoch 以来的秒数）
    int64_t old_key_expiry_time_ = 0;

    // ---- Nonce 生成 ----
    /// 原子 nonce 计数器——每次加密递增，生成唯一 nonce
    std::atomic<uint64_t> nonce_counter_{0};

    // ---- 内部方法 ----

    /**
     * @brief 从计数器生成 24 字节 nonce
     * @param counter 计数器值
     * @return 24 字节 nonce
     */
    static std::array<uint8_t, XCHACHA_NONCE_SIZE> generateNonce(uint64_t counter);

    /**
     * @brief 获取当前时间戳（秒，自 epoch）
     * @return 当前时间戳
     */
    static int64_t currentTimestampSeconds();
};

} // namespace nevo
