package com.nevo.voip.core.crypto

import javax.crypto.Cipher
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class CryptoManager @Inject constructor() {

    companion object {
        private const val TAG = "CryptoManager"

        init {
            try {
                System.loadLibrary("nevo_jni")
            } catch (e: UnsatisfiedLinkError) {
                android.util.Log.w(TAG, "Native library nevo_jni not loaded: ${e.message}")
            }
        }
    }

    private var nativeAvailable = false

    fun init(): Boolean {
        nativeAvailable = try {
            nativeInit()
        } catch (e: Throwable) {
            android.util.Log.e(TAG, "nativeInit failed: ${e.message}")
            false
        }
        android.util.Log.d(TAG, "CryptoManager initialized, native=$nativeAvailable")
        return nativeAvailable
    }

    fun generateKeyPair(): Pair<ByteArray, ByteArray> {
        // 安全铁律：失败即断开。原生加密库不可用时不做任何不安全的"降级"密钥对
        // （此前的 fallback 只是两个互不相关的随机数，并非合法的 X25519 密钥对）。
        if (!nativeAvailable) {
            throw IllegalStateException("原生加密库 nevo_jni 未加载，无法建立安全连接")
        }
        try {
            return nativeGenerateKeyPair()
        } catch (e: Exception) {
            android.util.Log.e(TAG, "nativeGenerateKeyPair failed: ${e.message}")
            throw IllegalStateException("原生加密库 nevo_jni 密钥生成失败，无法建立安全连接", e)
        }
    }

    fun encryptSealed(message: ByteArray, recipientPublicKey: ByteArray): ByteArray {
        // 安全铁律：失败即断开。此前 fallback 将随机 AES 会话密钥明文拼进输出，
        // 形同明文传输，已删除；加密不可用即抛错，由调用方中止连接。
        if (!nativeAvailable) {
            throw IllegalStateException("原生加密库 nevo_jni 未加载，无法建立安全连接")
        }
        try {
            return nativeEncryptSealed(message, recipientPublicKey)
        } catch (e: Exception) {
            android.util.Log.e(TAG, "nativeEncryptSealed failed: ${e.message}")
            throw IllegalStateException("原生加密库 nevo_jni 加密失败，无法建立安全连接", e)
        }
    }

    fun decryptSealed(ciphertext: ByteArray, privateKey: ByteArray): ByteArray? {
        if (!nativeAvailable) {
            throw IllegalStateException("原生加密库 nevo_jni 未加载，无法解密会话密钥")
        }
        try {
            return nativeDecryptSealed(ciphertext, privateKey)
        } catch (e: Throwable) {
            // 解密失败即失败：返回 null 由调用方断开连接，不做任何降级
            android.util.Log.e(TAG, "nativeDecryptSealed failed: ${e.message}")
            return null
        }
    }

    fun voiceEncrypt(
        key: ByteArray,
        nonce: ByteArray,
        plaintext: ByteArray,
        aad: ByteArray? = null
    ): ByteArray {
        if (nativeAvailable) {
            try {
                return nativeVoiceEncrypt(key, nonce, plaintext, aad)
            } catch (e: Exception) {
                android.util.Log.w(TAG, "nativeVoiceEncrypt failed, fallback: ${e.message}")
            }
        }
        return fallbackVoiceEncrypt(key, nonce, plaintext, aad)
    }

    fun voiceDecrypt(
        key: ByteArray,
        nonce: ByteArray,
        ciphertext: ByteArray,
        aad: ByteArray? = null
    ): ByteArray? {
        if (nativeAvailable) {
            try {
                return nativeVoiceDecrypt(key, nonce, ciphertext, aad)
            } catch (e: Exception) {
                android.util.Log.w(TAG, "nativeVoiceDecrypt failed, fallback: ${e.message}")
            }
        }
        return fallbackVoiceDecrypt(key, nonce, ciphertext, aad)
    }

    private external fun nativeInit(): Boolean

    @JvmSuppressWildcards
    private external fun nativeGenerateKeyPair(): Pair<ByteArray, ByteArray>

    private external fun nativeEncryptSealed(
        message: ByteArray,
        recipientPublicKey: ByteArray
    ): ByteArray

    private external fun nativeDecryptSealed(
        ciphertext: ByteArray,
        privateKey: ByteArray
    ): ByteArray

    private external fun nativeVoiceEncrypt(
        key: ByteArray,
        nonce: ByteArray,
        plaintext: ByteArray,
        aad: ByteArray?
    ): ByteArray

    private external fun nativeVoiceDecrypt(
        key: ByteArray,
        nonce: ByteArray,
        ciphertext: ByteArray,
        aad: ByteArray?
    ): ByteArray

    // ================================================================
    // Pure-Kotlin fallback implementations using javax.crypto
    // 仅语音通道（AES/GCM，密钥来自协商好的会话密钥）保留纯 Kotlin 实现；
    // 密钥对生成 / crypto_box_seal 的 fallback 已删除：失败即断开，不做不安全降级。
    // ================================================================

    private val gcmTagLength = 128
    private val gcmIvLength = 12

    private fun fallbackVoiceEncrypt(
        key: ByteArray,
        nonce: ByteArray,
        plaintext: ByteArray,
        aad: ByteArray?
    ): ByteArray {
        return try {
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            val keySpec = SecretKeySpec(key.copyOf(32), "AES")
            val iv = nonce.copyOf(gcmIvLength)
            val gcmSpec = GCMParameterSpec(gcmTagLength, iv)
            cipher.init(Cipher.ENCRYPT_MODE, keySpec, gcmSpec)
            if (aad != null && aad.isNotEmpty()) {
                cipher.updateAAD(aad)
            }
            cipher.doFinal(plaintext)
        } catch (e: Exception) {
            android.util.Log.e(TAG, "fallbackVoiceEncrypt failed: ${e.message}")
            ByteArray(0)
        }
    }

    private fun fallbackVoiceDecrypt(
        key: ByteArray,
        nonce: ByteArray,
        ciphertext: ByteArray,
        aad: ByteArray?
    ): ByteArray? {
        return try {
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            val keySpec = SecretKeySpec(key.copyOf(32), "AES")
            val iv = nonce.copyOf(gcmIvLength)
            val gcmSpec = GCMParameterSpec(gcmTagLength, iv)
            cipher.init(Cipher.DECRYPT_MODE, keySpec, gcmSpec)
            if (aad != null && aad.isNotEmpty()) {
                cipher.updateAAD(aad)
            }
            cipher.doFinal(ciphertext)
        } catch (e: Exception) {
            android.util.Log.e(TAG, "fallbackVoiceDecrypt failed: ${e.message}")
            null
        }
    }
}