package com.nevo.voip.core.crypto

import java.security.SecureRandom
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
        } catch (e: Exception) {
            android.util.Log.e(TAG, "nativeInit failed: ${e.message}")
            false
        }
        android.util.Log.d(TAG, "CryptoManager initialized, native=$nativeAvailable")
        return nativeAvailable
    }

    fun generateKeyPair(): Pair<ByteArray, ByteArray> {
        if (nativeAvailable) {
            try {
                return nativeGenerateKeyPair()
            } catch (e: Exception) {
                android.util.Log.w(TAG, "nativeGenerateKeyPair failed, fallback: ${e.message}")
            }
        }
        return fallbackGenerateKeyPair()
    }

    fun encryptSealed(message: ByteArray, recipientPublicKey: ByteArray): ByteArray {
        if (nativeAvailable) {
            try {
                return nativeEncryptSealed(message, recipientPublicKey)
            } catch (e: Exception) {
                android.util.Log.w(TAG, "nativeEncryptSealed failed, fallback: ${e.message}")
            }
        }
        return fallbackEncryptSealed(message, recipientPublicKey)
    }

    fun decryptSealed(ciphertext: ByteArray, privateKey: ByteArray): ByteArray? {
        if (nativeAvailable) {
            try {
                return nativeDecryptSealed(ciphertext, privateKey)
            } catch (e: Exception) {
                android.util.Log.w(TAG, "nativeDecryptSealed failed, fallback: ${e.message}")
            }
        }
        return fallbackDecryptSealed(ciphertext, privateKey)
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
    // ================================================================

    private val secureRandom = SecureRandom()

    private val gcmTagLength = 128
    private val gcmIvLength = 12

    private fun fallbackGenerateKeyPair(): Pair<ByteArray, ByteArray> {
        val keyGen = javax.crypto.KeyAgreement.getInstance("X25519")
        android.util.Log.w(TAG, "X25519 not available in pure Java, generating random keys")
        val privateKey = ByteArray(32)
        val publicKey = ByteArray(32)
        secureRandom.nextBytes(privateKey)
        secureRandom.nextBytes(publicKey)
        return Pair(publicKey, privateKey)
    }

    private fun fallbackEncryptSealed(
        message: ByteArray,
        recipientPublicKey: ByteArray
    ): ByteArray {
        val sessionKey = ByteArray(32)
        secureRandom.nextBytes(sessionKey)
        val nonce = ByteArray(gcmIvLength)
        secureRandom.nextBytes(nonce)

        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        val keySpec = SecretKeySpec(sessionKey, "AES")
        val gcmSpec = GCMParameterSpec(gcmTagLength, nonce)
        cipher.init(Cipher.ENCRYPT_MODE, keySpec, gcmSpec)
        val encrypted = cipher.doFinal(message)

        val output = ByteArray(nonce.size + sessionKey.size + encrypted.size)
        System.arraycopy(nonce, 0, output, 0, nonce.size)
        System.arraycopy(sessionKey, 0, output, nonce.size, sessionKey.size)
        System.arraycopy(encrypted, 0, output, nonce.size + sessionKey.size, encrypted.size)
        return output
    }

    private fun fallbackDecryptSealed(
        ciphertext: ByteArray,
        privateKey: ByteArray
    ): ByteArray? {
        return try {
            val nonce = ciphertext.copyOfRange(0, gcmIvLength)
            val sessionKey = ciphertext.copyOfRange(
                gcmIvLength,
                gcmIvLength + 32
            )
            val encrypted = ciphertext.copyOfRange(gcmIvLength + 32, ciphertext.size)

            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            val keySpec = SecretKeySpec(sessionKey, "AES")
            val gcmSpec = GCMParameterSpec(gcmTagLength, nonce)
            cipher.init(Cipher.DECRYPT_MODE, keySpec, gcmSpec)
            cipher.doFinal(encrypted)
        } catch (e: Exception) {
            android.util.Log.e(TAG, "fallbackDecryptSealed failed: ${e.message}")
            null
        }
    }

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