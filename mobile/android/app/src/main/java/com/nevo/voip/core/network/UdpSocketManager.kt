package com.nevo.voip.core.network

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class UdpSocketManager @Inject constructor() {

    companion object {
        private const val DEFAULT_BUFFER_SIZE = 1500
        private const val RECEIVE_BUFFER_SIZE = 65536
    }

    @Volatile
    private var socket: DatagramSocket? = null

    private val lock = Any()

    val localPort: Int
        get() = synchronized(lock) {
            socket?.localPort ?: 0
        }

    suspend fun createSocket(): Result<Int> = withContext(Dispatchers.IO) {
        try {
            synchronized(lock) {
                closeInternal()
                val s = DatagramSocket(null)
                s.reuseAddress = true
                s.sendBufferSize = DEFAULT_BUFFER_SIZE
                s.receiveBufferSize = RECEIVE_BUFFER_SIZE
                try {
                    s.bind(InetSocketAddress("::", 0))
                } catch (e: Exception) {
                    s.bind(InetSocketAddress("0.0.0.0", 0))
                }
                socket = s
            }
            Result.success(socket?.localPort ?: 0)
        } catch (e: Exception) {
            synchronized(lock) {
                closeInternal()
            }
            Result.failure(e)
        }
    }

    suspend fun sendTo(address: InetSocketAddress, data: ByteArray): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val packet = DatagramPacket(data, data.size, address)
            synchronized(lock) {
                socket?.send(packet) ?: throw IllegalStateException("Socket not created")
            }
            Result.success(Unit)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun receiveFrom(bufferSize: Int = DEFAULT_BUFFER_SIZE): Result<DatagramPacket> = withContext(Dispatchers.IO) {
        try {
            val buffer = ByteArray(bufferSize)
            val packet = DatagramPacket(buffer, buffer.size)
            synchronized(lock) {
                socket?.receive(packet) ?: throw IllegalStateException("Socket not created")
            }
            Result.success(packet)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun startReceiveLoop(
        address: InetSocketAddress,
        onPacket: (ByteArray) -> Unit
    ): Job = withContext(Dispatchers.IO) {
        CoroutineScope(Dispatchers.IO).launch {
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            while (true) {
                try {
                    val packet = DatagramPacket(buffer, buffer.size)
                    synchronized(lock) {
                        val s = socket
                        if (s == null || s.isClosed) return@launch
                        s.receive(packet)
                    }
                    if (packet.address == address.address && packet.port == address.port) {
                        val data = packet.data.copyOf(packet.length)
                        onPacket(data)
                    }
                } catch (e: Exception) {
                    if (socket?.isClosed == true) return@launch
                }
            }
        }
    }

    fun close() {
        synchronized(lock) {
            closeInternal()
        }
    }

    private fun closeInternal() {
        try {
            socket?.close()
        } catch (_: Exception) {
        }
        socket = null
    }
}