package com.nevo.voip.core.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException
import java.net.NetworkInterface
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class TcpConnectionManager @Inject constructor() {

    companion object {
        const val HEADER_SIZE = 12
        const val MAX_PAYLOAD_SIZE = 1024 * 1024
        private const val CONNECT_TIMEOUT_MS = 10000
    }

    @Volatile
    private var socket: Socket? = null

    private var inputStream: DataInputStream? = null
    private var outputStream: DataOutputStream? = null

    private val stateLock = Any()
    private val writeLock = Any()

    val isConnected: Boolean
        get() = synchronized(stateLock) {
            socket?.isConnected == true && socket?.isClosed == false
        }

    suspend fun connect(host: String, port: Int): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val addresses = InetAddress.getAllByName(host)
            var lastErr: Exception? = null
            var sock: Socket? = null
            for (addr in addresses) {
                try {
                    val s = Socket()
                    s.soTimeout = 0
                    s.tcpNoDelay = true
                    s.connect(InetSocketAddress(addr, port), CONNECT_TIMEOUT_MS)
                    sock = s
                    break
                } catch (e: Exception) {
                    lastErr = e
                }
            }
            val s = sock ?: throw (lastErr ?: java.net.ConnectException("Failed to connect to $host:$port"))
            synchronized(stateLock) {
                disconnectInternal()
                socket = s
                inputStream = DataInputStream(s.getInputStream())
                outputStream = DataOutputStream(s.getOutputStream())
            }
            Result.success(Unit)
        } catch (e: Exception) {
            synchronized(stateLock) {
                disconnectInternal()
            }
            Result.failure(buildConnectionException(host, port, e))
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
            val out = synchronized(stateLock) {
                outputStream ?: return@withContext Result.failure(IllegalStateException("Not connected"))
            }
            synchronized(writeLock) {
                out.write(header)
                out.write(payload)
                out.flush()
            }
            Result.success(Unit)
        } catch (e: Exception) {
            synchronized(stateLock) {
                disconnectInternal()
            }
            Result.failure(e)
        }
    }

    suspend fun readMessage(): Result<Pair<Int, ByteArray>> = withContext(Dispatchers.IO) {
        try {
            val input = synchronized(stateLock) {
                inputStream ?: return@withContext Result.failure(IllegalStateException("Not connected"))
            }
            val headerBytes = ByteArray(HEADER_SIZE)
            input.readFully(headerBytes)
            val header = ByteBuffer.wrap(headerBytes).order(ByteOrder.BIG_ENDIAN)
            val payloadLength = header.getInt(0)
            val messageType = header.getInt(4)
            val requestId = header.getInt(8)

            if (payloadLength < 0 || payloadLength > MAX_PAYLOAD_SIZE) {
                return@withContext Result.failure(IllegalArgumentException("Invalid payload length: $payloadLength"))
            }

            val payload = ByteArray(payloadLength)
            if (payloadLength > 0) {
                input.readFully(payload)
            }

            Result.success(Pair(messageType, payload))
        } catch (e: Exception) {
            synchronized(stateLock) {
                disconnectInternal()
            }
            Result.failure(e)
        }
    }

    fun disconnect() {
        synchronized(stateLock) {
            disconnectInternal()
        }
    }

    private fun buildConnectionException(host: String, port: Int, cause: Exception): Exception {
        val target = runCatching { InetAddress.getByName(host) }.getOrNull()
        val localIpv4 = getLocalIpv4Addresses()
        val sameSubnet = target is Inet4Address && localIpv4.any { isSameClassCSubnet(it, target) }
        val message = if (cause is SocketTimeoutException || cause.message?.contains("after ${CONNECT_TIMEOUT_MS}ms") == true) {
            buildString {
                append("无法连接到 $host:$port，连接超时 ${CONNECT_TIMEOUT_MS / 1000}s。")
                if (target is Inet4Address && localIpv4.isNotEmpty() && !sameSubnet) {
                    append(" 当前设备IP=${localIpv4.joinToString { it.hostAddress ?: it.toString() }}，目标IP=${target.hostAddress}，不在同一网段；请确认手机和服务器在同一局域网/VPN，或检查路由、防火墙、服务端监听地址。")
                } else {
                    append(" 请确认服务端已启动、端口开放、防火墙放行，并监听 0.0.0.0 或局域网地址。")
                }
            }
        } else {
            cause.message ?: cause.javaClass.simpleName
        }
        return Exception(message, cause)
    }

    private fun getLocalIpv4Addresses(): List<Inet4Address> {
        return runCatching {
            NetworkInterface.getNetworkInterfaces().toList()
                .filter { it.isUp && !it.isLoopback }
                .flatMap { it.inetAddresses.toList() }
                .filterIsInstance<Inet4Address>()
                .filterNot { it.isLoopbackAddress }
        }.getOrDefault(emptyList())
    }

    private fun isSameClassCSubnet(a: Inet4Address, b: Inet4Address): Boolean {
        val aa = a.address
        val bb = b.address
        return aa[0] == bb[0] && aa[1] == bb[1] && aa[2] == bb[2]
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