package com.nevo.voip.core.crypto

import java.util.concurrent.atomic.AtomicLong

class VoiceCryptoState {

    companion object {
        const val KEY_SIZE = 32
        const val NONCE_SIZE = 24
        const val TAG_SIZE = 16
        const val KEY_OVERLAP_SECONDS = 20L
    }

    @Volatile
    var currentKey: ByteArray = ByteArray(KEY_SIZE)
        private set

    @Volatile
    private var oldKeyBytes: ByteArray? = null

    @Volatile
    private var oldKeyExpiryTimestamp: Long = 0

    private val nonceCounter = AtomicLong(0)

    val hasOldKey: Boolean
        get() {
            val expiry = oldKeyExpiryTimestamp
            return oldKeyBytes != null && System.currentTimeMillis() / 1000 < expiry
        }

    val nextNonce: ByteArray
        get() {
            val counter = nonceCounter.getAndIncrement()
            val nonce = ByteArray(NONCE_SIZE)
            nonce[0] = (counter ushr 0).toByte()
            nonce[1] = (counter ushr 8).toByte()
            nonce[2] = (counter ushr 16).toByte()
            nonce[3] = (counter ushr 24).toByte()
            nonce[4] = (counter ushr 32).toByte()
            nonce[5] = (counter ushr 40).toByte()
            nonce[6] = (counter ushr 48).toByte()
            nonce[7] = (counter ushr 56).toByte()
            return nonce
        }

    fun setSessionKey(key: ByteArray) {
        require(key.size == KEY_SIZE) {
            "Key must be $KEY_SIZE bytes, got ${key.size}"
        }
        synchronized(this) {
            currentKey = key.copyOf()
            oldKeyBytes = null
            oldKeyExpiryTimestamp = 0
            nonceCounter.set(0)
        }
    }

    fun rotateKey(newKey: ByteArray) {
        require(newKey.size == KEY_SIZE) {
            "New key must be $KEY_SIZE bytes, got ${newKey.size}"
        }
        synchronized(this) {
            oldKeyBytes = currentKey.copyOf()
            oldKeyExpiryTimestamp = System.currentTimeMillis() / 1000 + KEY_OVERLAP_SECONDS
            currentKey = newKey.copyOf()
        }
    }

    fun purgeExpiredOldKey() {
        synchronized(this) {
            if (oldKeyBytes != null &&
                System.currentTimeMillis() / 1000 >= oldKeyExpiryTimestamp
            ) {
                oldKeyBytes?.fill(0)
                oldKeyBytes = null
                oldKeyExpiryTimestamp = 0
            }
        }
    }

    fun getKeysToTry(): List<ByteArray> {
        val keys = mutableListOf(currentKey)
        synchronized(this) {
            val oldKey = oldKeyBytes
            if (oldKey != null && System.currentTimeMillis() / 1000 < oldKeyExpiryTimestamp) {
                keys.add(oldKey)
            }
        }
        return keys
    }

    fun reset() {
        synchronized(this) {
            currentKey.fill(0)
            currentKey = ByteArray(KEY_SIZE)
            oldKeyBytes?.fill(0)
            oldKeyBytes = null
            oldKeyExpiryTimestamp = 0
            nonceCounter.set(0)
        }
    }
}