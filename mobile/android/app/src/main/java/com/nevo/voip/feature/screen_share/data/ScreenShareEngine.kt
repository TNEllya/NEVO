package com.nevo.voip.feature.screen_share.data

import android.content.Context
import android.content.Intent
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaCodecList
import android.media.projection.MediaProjection
import android.os.Handler
import android.os.HandlerThread
import android.view.Surface
import com.nevo.voip.core.crypto.CryptoManager
import com.nevo.voip.core.network.UdpSocketManager
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import java.net.InetSocketAddress
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.inject.Inject
import javax.inject.Singleton

sealed class ScreenShareState {
    object Idle : ScreenShareState()
    object Preparing : ScreenShareState()
    data class Sharing(val viewerCount: Int) : ScreenShareState()
    data class Viewing(val userId: Long, val userName: String) : ScreenShareState()
    data class Error(val message: String) : ScreenShareState()
}

class NalFragment(val index: Int, val total: Int, val data: ByteArray)

@Singleton
class ScreenShareEngine @Inject constructor(
    @ApplicationContext private val context: Context,
    private val cryptoManager: CryptoManager,
    private val udpSocketManager: UdpSocketManager,
) {
    private val _state = MutableStateFlow<ScreenShareState>(ScreenShareState.Idle)
    val state: StateFlow<ScreenShareState> = _state

    private var mediaProjection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var encoder: MediaCodec? = null
    private var encoderThread: HandlerThread? = null
    private var encoderHandler: Handler? = null
    private var serverVideoAddress: InetSocketAddress? = null
    private var fragmentSequence: Int = 0
    private var currentVoiceKey: ByteArray = ByteArray(32)

    private val nalReassembler = mutableMapOf<Int, MutableMap<Int, ByteArray>>()
    private val nalReassemblerTimestamps = mutableMapOf<Int, Long>()

    private val sendScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    companion object {
        const val MAX_NAL_SIZE = 1200
        const val FRAGMENT_TTL_MS = 5000L
        const val WIDTH = 1280
        const val HEIGHT = 720
        const val BITRATE = 2_000_000
        const val FRAME_RATE = 30
    }

    fun startSharing(
        projection: MediaProjection,
        serverHost: String,
        serverVideoUdpPort: Int
    ) {
        _state.value = ScreenShareState.Preparing
        mediaProjection = projection

        encoderThread = HandlerThread("ScreenEncoder").apply { start() }
        encoderHandler = Handler(encoderThread!!.looper)

        serverVideoAddress = InetSocketAddress(serverHost, serverVideoUdpPort)

        try {
            val format = MediaFormat.createVideoFormat(
                "video/avc", WIDTH, HEIGHT
            ).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_BIT_RATE, BITRATE)
                setInteger(MediaFormat.KEY_FRAME_RATE, FRAME_RATE)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 2)
            }

            val codecName = MediaCodecList(MediaCodecList.REGULAR_CODECS)
                .findEncoderForFormat(format)
            encoder = MediaCodec.createByCodecName(codecName)
            encoder!!.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)

            val inputSurface = encoder!!.createInputSurface()
            encoder!!.start()

            virtualDisplay = projection.createVirtualDisplay(
                "NevoScreenShare",
                WIDTH, HEIGHT, context.resources.displayMetrics.densityDpi,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                inputSurface, null, encoderHandler
            )

            encoderHandler!!.post {
                drainEncoder(serverHost, serverVideoUdpPort)
            }

            _state.value = ScreenShareState.Sharing(viewerCount = 0)
        } catch (e: Exception) {
            _state.value = ScreenShareState.Error("Failed to start screen share: ${e.message}")
            stopSharing()
        }
    }

    private fun drainEncoder(serverHost: String, serverPort: Int) {
        val bufferInfo = MediaCodec.BufferInfo()
        val codec = encoder ?: return

        while (true) {
            val outputIndex = codec.dequeueOutputBuffer(bufferInfo, 10000)
            if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) continue
            if (outputIndex == MediaCodec.INFO_TRY_AGAIN_LATER) continue
            if (outputIndex < 0) break

            val outputBuffer = codec.getOutputBuffer(outputIndex)
            if (outputBuffer != null && bufferInfo.size > 0) {
                val nalData = ByteArray(bufferInfo.size)
                outputBuffer.position(bufferInfo.offset)
                outputBuffer.get(nalData, 0, bufferInfo.size)

                sendNalFragmented(nalData, serverHost, serverPort)
            }
            codec.releaseOutputBuffer(outputIndex, false)
        }
    }

    private fun sendNalFragmented(nalData: ByteArray, serverHost: String, serverPort: Int) {
        val totalFragments = (nalData.size + MAX_NAL_SIZE - 1) / MAX_NAL_SIZE
        val sequence = fragmentSequence++

        if (totalFragments == 1) {
            val encrypted = cryptoManager.voiceEncrypt(
                currentVoiceKey,
                ByteArray(24),
                nalData,
                null
            )
            if (encrypted.isEmpty()) return
            sendScope.launch {
                try {
                    udpSocketManager.sendTo(InetSocketAddress(serverHost, serverPort), encrypted)
                } catch (_: Exception) {}
            }
            return
        }

        for (i in 0 until totalFragments) {
            val start = i * MAX_NAL_SIZE
            val end = minOf(start + MAX_NAL_SIZE, nalData.size)
            val fragment = nalData.copyOfRange(start, end)

            val header = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
            header.putInt(sequence)
            header.putShort(i.toShort())
            header.putShort(totalFragments.toShort())

            val combined = header.array() + fragment
            val encrypted = cryptoManager.voiceEncrypt(
                currentVoiceKey, ByteArray(24), combined, null
            )
            if (encrypted.isEmpty()) continue

            sendScope.launch {
                try {
                    udpSocketManager.sendTo(InetSocketAddress(serverHost, serverPort), encrypted)
                } catch (_: Exception) {}
            }
        }
    }

    fun receiveFragment(encryptedData: ByteArray): NalFragment? {
        val decrypted = cryptoManager.voiceDecrypt(
            currentVoiceKey, ByteArray(24), encryptedData, null
        ) ?: return null

        val header = ByteBuffer.wrap(decrypted, 0, 8).order(ByteOrder.LITTLE_ENDIAN)
        val sequence = header.int
        val index = header.short.toInt()
        val total = header.short.toInt()

        val data = decrypted.copyOfRange(8, decrypted.size)

        val fragments = nalReassembler.getOrPut(sequence) { mutableMapOf() }
        fragments[index] = data
        nalReassemblerTimestamps[sequence] = System.currentTimeMillis()

        if (fragments.size == total) {
            val ordered = (0 until total).mapNotNull { fragments[it] }
            val complete = ByteArray(ordered.sumOf { it.size })
            var offset = 0
            for (part in ordered) {
                System.arraycopy(part, 0, complete, offset, part.size)
                offset += part.size
            }
            nalReassembler.remove(sequence)
            nalReassemblerTimestamps.remove(sequence)
            return NalFragment(0, total, complete)
        }
        return null
    }

    fun purgeExpiredFragments() {
        val now = System.currentTimeMillis()
        val expired = nalReassemblerTimestamps.filter {
            now - it.value > FRAGMENT_TTL_MS
        }.keys
        expired.forEach { sequence ->
            nalReassembler.remove(sequence)
            nalReassemblerTimestamps.remove(sequence)
        }
    }

    fun setServerVideoAddress(host: String, port: Int) {
        serverVideoAddress = InetSocketAddress(host, port)
    }

    fun setSessionKey(key: ByteArray) {
        currentVoiceKey = key
    }

    fun stopSharing() {
        encoder?.stop()
        encoder?.release()
        encoder = null

        virtualDisplay?.release()
        virtualDisplay = null

        mediaProjection?.stop()
        mediaProjection = null

        encoderHandler?.looper?.quitSafely()
        encoderThread = null
        encoderHandler = null

        serverVideoAddress = null
        sendScope.cancel()
        _state.value = ScreenShareState.Idle
    }

    fun cleanup() {
        stopSharing()
        nalReassembler.clear()
        nalReassemblerTimestamps.clear()
    }
}