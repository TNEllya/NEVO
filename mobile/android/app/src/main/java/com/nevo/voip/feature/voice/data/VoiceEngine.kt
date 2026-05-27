package com.nevo.voip.feature.voice.data

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.os.Process
import androidx.core.content.ContextCompat
import com.nevo.voip.core.audio.NativeAudioEngine
import com.nevo.voip.core.crypto.CryptoManager
import com.nevo.voip.core.crypto.VoiceCryptoState
import com.nevo.voip.core.network.UdpSocketManager
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.InetSocketAddress
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class VoiceEngine @Inject constructor(
    @ApplicationContext private val applicationContext: Context,
    private val cryptoManager: CryptoManager,
    private val udpSocketManager: UdpSocketManager
) {
    companion object {
        private const val TAG = "VoiceEngine"
        private const val SAMPLE_RATE = 48000
        private const val CHANNEL_COUNT = 1
        private const val FRAME_SAMPLES = 1920
        private const val BYTES_PER_SAMPLE = 2
        private const val FRAME_SIZE_BYTES = FRAME_SAMPLES * BYTES_PER_SAMPLE
    }

    private val nativeAudioEngine = NativeAudioEngine()
    private val voiceCryptoState = VoiceCryptoState()
    private val isMuted = AtomicBoolean(false)
    private val isDeafened = AtomicBoolean(false)
    private val isRunning = AtomicBoolean(false)

    private var audioRecord: AudioRecord? = null
    private var audioTrack: AudioTrack? = null
    private var micJob: Job? = null
    private var speakerJob: Job? = null
    private var serverAddress: InetSocketAddress? = null

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private var nativeInitialized = false
    private var udpSocketCreated = false

    fun init() {
        if (!nativeInitialized) {
            nativeAudioEngine.initAudio(SAMPLE_RATE, CHANNEL_COUNT)
            nativeInitialized = true
        }
    }

    suspend fun startMicrophone(): Result<Unit> = withContext(Dispatchers.IO) {
        init()

        if (ContextCompat.checkSelfPermission(
                applicationContext,
                Manifest.permission.RECORD_AUDIO
            ) != PackageManager.PERMISSION_GRANTED
        ) {
            return@withContext Result.failure(
                SecurityException("RECORD_AUDIO permission not granted")
            )
        }

        ensureUdpSocket()

        val minBufferSize = AudioRecord.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT
        )
        val bufferSize = maxOf(minBufferSize, FRAME_SIZE_BYTES * 2)

        if (audioRecord == null || audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
            audioRecord?.release()
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.VOICE_COMMUNICATION,
                SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                bufferSize
            )
        }

        if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
            audioRecord?.release()
            audioRecord = null
            return@withContext Result.failure(IllegalStateException("AudioRecord initialization failed"))
        }

        isRunning.set(true)
        audioRecord?.startRecording()

        micJob?.cancel()
        micJob = scope.launch {
            val pcmBuffer = ShortArray(FRAME_SAMPLES)
            val floatBuffer = FloatArray(FRAME_SAMPLES)
            android.os.Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)

            while (isActive && isRunning.get()) {
                val bytesRead = audioRecord?.read(pcmBuffer, 0, FRAME_SAMPLES) ?: -1
                if (bytesRead <= 0) continue

                if (isMuted.get()) continue

                val opusData = nativeAudioEngine.encodeOpus(pcmBuffer.copyOf(bytesRead))
                if (opusData.isEmpty()) continue

                val key = try {
                    voiceCryptoState.currentKey
                } catch (e: Exception) {
                    continue
                }
                val nonce = voiceCryptoState.nextNonce
                val encrypted = cryptoManager.voiceEncrypt(key, nonce, opusData)
                if (encrypted.isEmpty()) continue

                val addr = serverAddress ?: continue
                udpSocketManager.sendTo(addr, encrypted)
            }
        }

        Result.success(Unit)
    }

    suspend fun startSpeaker(serverUdpPort: Int): Result<Unit> = withContext(Dispatchers.IO) {
        init()
        ensureUdpSocket()

        val minBufferSize = AudioTrack.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT
        )
        val bufferSize = maxOf(minBufferSize, FRAME_SIZE_BYTES * 4)

        if (audioTrack == null || audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
            audioTrack?.release()
            val attributes = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                .build()
            val format = AudioFormat.Builder()
                .setSampleRate(SAMPLE_RATE)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                .build()
            audioTrack = AudioTrack.Builder()
                .setAudioAttributes(attributes)
                .setAudioFormat(format)
                .setBufferSizeInBytes(bufferSize)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
        }

        if (audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
            audioTrack?.release()
            audioTrack = null
            return@withContext Result.failure(IllegalStateException("AudioTrack initialization failed"))
        }

        audioTrack?.play()

        speakerJob?.cancel()
        speakerJob = scope.launch {
            android.os.Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)

            while (isActive && isRunning.get()) {
                val result = udpSocketManager.receiveFrom()
                if (result.isFailure) {
                    if (isRunning.get()) continue else break
                }
                val packet = result.getOrThrow()
                val data = packet.data.copyOf(packet.length)

                if (isDeafened.get()) continue

                var decrypted: ByteArray? = null
                for (key in voiceCryptoState.getKeysToTry()) {
                    val candidate = cryptoManager.voiceDecrypt(key, ByteArray(24), data)
                    if (candidate != null && candidate.isNotEmpty()) {
                        decrypted = candidate
                        break
                    }
                }
                if (decrypted == null) continue

                val timestamp = (System.nanoTime() / 1000).toInt()
                nativeAudioEngine.jitterBufferPush(decrypted, timestamp)

                val popped = nativeAudioEngine.jitterBufferPop()
                if (popped != null) {
                    val (opusPacket, _) = popped
                    val pcm = nativeAudioEngine.decodeOpus(opusPacket)
                    if (audioTrack?.playState == AudioTrack.PLAYSTATE_PLAYING) {
                        val byteBuffer = ShortArrayToByteBuffer(pcm)
                        audioTrack?.write(byteBuffer, 0, byteBuffer.size)
                    }
                }
            }
        }

        Result.success(Unit)
    }

    fun stopAll() {
        isRunning.set(false)
        micJob?.cancel()
        speakerJob?.cancel()
        micJob = null
        speakerJob = null

        try {
            audioRecord?.stop()
            audioRecord?.release()
        } catch (_: Exception) {}
        audioRecord = null

        try {
            audioTrack?.stop()
            audioTrack?.release()
        } catch (_: Exception) {}
        audioTrack = null

        serverAddress = null
        udpSocketCreated = false
        voiceCryptoState.reset()
    }

    fun setMuted(muted: Boolean) {
        isMuted.set(muted)
    }

    fun setDeafened(deafened: Boolean) {
        isDeafened.set(deafened)
    }

    fun setSessionKey(key: ByteArray) {
        if (key.size == VoiceCryptoState.KEY_SIZE) {
            voiceCryptoState.setSessionKey(key)
        }
    }

    fun setServerInfo(host: String, port: Int) {
        serverAddress = InetSocketAddress(host, port)
    }

    private suspend fun ensureUdpSocket() {
        if (!udpSocketCreated) {
            val result = udpSocketManager.createSocket()
            if (result.isSuccess) {
                udpSocketCreated = true
            }
        }
    }

    private fun ShortArrayToByteBuffer(shorts: ShortArray): ByteArray {
        val bytes = ByteArray(shorts.size * 2)
        val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        for (s in shorts) {
            buf.putShort(s)
        }
        return bytes
    }
}