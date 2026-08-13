package com.nevo.voip.core.audio

import javax.inject.Singleton

@Singleton
class NativeAudioEngine {

    companion object {
        private const val TAG = "NativeAudioEngine"

        init {
            try {
                System.loadLibrary("nevo_jni")
            } catch (e: UnsatisfiedLinkError) {
                android.util.Log.w(TAG, "Native library nevo_jni not loaded: ${e.message}")
            }
        }
    }

    private var nativeAvailable = false
    private var initialized = false

    fun initAudio(sampleRate: Int, channels: Int): Boolean {
        android.util.Log.d(TAG, "Initializing audio: $sampleRate Hz, $channels channels")
        _sampleRate = sampleRate
        _channels = channels
        nativeAvailable = try {
            nativeInitAudio(sampleRate, channels)
        } catch (e: Throwable) {
            android.util.Log.e(TAG, "nativeInitAudio failed: ${e.message}")
            false
        }
        initialized = nativeAvailable
        return nativeAvailable
    }

    fun encodeOpus(pcm: ShortArray): ByteArray {
        if (!initialized) return ByteArray(0)
        return try {
            nativeEncodeOpus(pcm, pcm.size / channels)
        } catch (e: Throwable) {
            android.util.Log.e(TAG, "nativeEncodeOpus failed: ${e.message}")
            ByteArray(0)
        }
    }

    fun encodeOpus(pcm: ShortArray, frameSize: Int): ByteArray {
        if (!initialized) return ByteArray(0)
        return try {
            nativeEncodeOpus(pcm, frameSize)
        } catch (e: Throwable) {
            android.util.Log.e(TAG, "nativeEncodeOpus failed: ${e.message}")
            ByteArray(0)
        }
    }

    fun decodeOpus(opus: ByteArray): ShortArray {
        val frameSize = sampleRate / 50
        if (!initialized) return ShortArray(frameSize * channels)
        return try {
            nativeDecodeOpus(opus, frameSize)
                ?: ShortArray(frameSize * channels)
        } catch (e: Throwable) {
            android.util.Log.e(TAG, "nativeDecodeOpus failed: ${e.message}")
            ShortArray(sampleRate / 50 * channels)
        }
    }

    fun decodeOpus(opus: ByteArray, frameSize: Int): ShortArray {
        if (!initialized) return ShortArray(frameSize * channels)
        return try {
            nativeDecodeOpus(opus, frameSize)
                ?: ShortArray(frameSize * channels)
        } catch (e: Throwable) {
            android.util.Log.e(TAG, "nativeDecodeOpus failed: ${e.message}")
            ShortArray(frameSize * channels)
        }
    }

    fun mixAudio(audioFrames: Array<ShortArray>, volumes: FloatArray): ByteArray {
        if (!initialized) return ByteArray(0)
        return try {
            nativeMixAudio(audioFrames, volumes)
                ?: ByteArray(0)
        } catch (e: Throwable) {
            android.util.Log.e(TAG, "nativeMixAudio failed: ${e.message}")
            ByteArray(0)
        }
    }

    fun jitterBufferPush(packet: ByteArray, timestamp: Int) {
        if (!initialized) return
        try {
            nativeJitterBufferPush(packet, timestamp)
        } catch (e: Throwable) {
            android.util.Log.e(TAG, "nativeJitterBufferPush failed: ${e.message}")
        }
    }

    fun jitterBufferPop(): Pair<ByteArray, Int>? {
        if (!initialized) return null
        return try {
            nativeJitterBufferPop()
        } catch (e: Throwable) {
            android.util.Log.e(TAG, "nativeJitterBufferPop failed: ${e.message}")
            null
        }
    }

    val sampleRate: Int
        get() = _sampleRate

    val channels: Int
        get() = _channels

    private var _sampleRate = 48000
    private var _channels = 1

    private external fun nativeInitAudio(sampleRate: Int, channels: Int): Boolean
    private external fun nativeEncodeOpus(pcm: ShortArray, frameSize: Int): ByteArray
    private external fun nativeDecodeOpus(opus: ByteArray, frameSize: Int): ShortArray?
    private external fun nativeMixAudio(audioFrames: Array<ShortArray>, volumes: FloatArray): ByteArray?
    private external fun nativeJitterBufferPush(packet: ByteArray, timestamp: Int)
    private external fun nativeJitterBufferPop(): Pair<ByteArray, Int>?
}