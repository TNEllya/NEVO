package com.nevo.voip.core.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class TcpConnectionManager @Inject constructor() {

    companion object {
        const val HEADER_SIZE = 12
        const val MAX_PAYLOAD_SIZE = 1024 * 1024
    }

    @Volatile
    private var socket: Socket? = null

    private var inputStream: DataInputStream? = null
    private var outputStream: DataOutputStream? = null

    private val lock = Any()

    val isConnected: Boolean
        get() = synchronized(lock) {
            socket?.isConnected == true && socket?.isClosed == false
        }

    suspend fun connect(host: String, port: Int): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            synchronized(lock) {
                disconnectInternal()
                val addresses = InetAddress.getAllByName(host)
                var lastErr: Exception? = null
                var sock: Socket? = null
                for (addr in addresses) {
                    try {
                        val s = Socket()
                        s.soTimeout = 0
                        s.tcpNoDelay = true
                        s.connect(InetSocketAddress(addr, port), 5000)
                        sock = s
                        break
                    } catch (e: Exception) {
                        lastErr = e
                    }
                }
                val s = sock ?: throw (lastErr ?: java.net.ConnectException("Failed to connect to $host:$port"))
                socket = s
                inputStream = DataInputStream(s.getInputStream())
                outputStream = DataOutputStream(s.getOutputStream())
            }
            Result.success(Unit)
        } catch (e: Exception) {
            synchronized(lock) {
                disconnectInternal()
            }
            Result.failure(e)
        }
    }

    suspend fun sendMessage(messageType: Int, payload: ByteArray): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            if (payload.size > MAX_PAYLOAD_SIZE) {
                return@withContext Result.failure(IllegalArgumentException("Payload too large: ${payload.size}"))
            }
            val header = ByteBuffer.allocate(HEADER_SIZE)
                .order(ByteOrder.BIG_ENDIAN)
                .putInt(payload.size)
                .putInt(messageType)
                .putInt(0)
                .array()
            synchronized(lock) {
                val out = outputStream ?: return@withContext Result.failure(IllegalStateException("Not connected"))
                out.write(header)
                out.write(payload)
                out.flush()
            }
            Result.success(Unit)
        } catch (e: Exception) {
            synchronized(lock) {
                disconnectInternal()
            }
            Result.failure(e)
        }
    }

    suspend fun readMessage(): Result<Pair<Int, ByteArray>> = withContext(Dispatchers.IO) {
        try {
            val headerBytes = ByteArray(HEADER_SIZE)
            synchronized(lock) {
                val input = inputStream ?: return@withContext Result.failure(IllegalStateException("Not connected"))
                input.readFully(headerBytes)
            }
            val header = ByteBuffer.wrap(headerBytes).order(ByteOrder.BIG_ENDIAN)
            val payloadLength = header.getInt(0)
            val messageType = header.getInt(4)
            val requestId = header.getInt(8)

            if (payloadLength < 0 || payloadLength > MAX_PAYLOAD_SIZE) {
                return@withContext Result.failure(IllegalArgumentException("Invalid payload length: $payloadLength"))
            }

            val payload = ByteArray(payloadLength)
            if (payloadLength > 0) {
                synchronized(lock) {
                    val input = inputStream ?: return@withContext Result.failure(IllegalStateException("Not connected"))
                    input.readFully(payload)
                }
            }

            Result.success(Pair(messageType, payload))
        } catch (e: Exception) {
            synchronized(lock) {
                disconnectInternal()
            }
            Result.failure(e)
        }
    }

    fun disconnect() {
        synchronized(lock) {
            disconnectInternal()
        }
    }

    private fun disconnectInternal() {
        try {
            inputStream?.close()
        } catch (_: Exception) {
        }
        try {
            outputStream?.close()
        } catch (_: Exception) {
        }
        try {
            socket?.close()
        } catch (_: Exception) {
        }
        inputStream = null
        outputStream = null
        socket = null
    }
}