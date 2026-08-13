package com.nevo.voip.feature.connection.data

import android.util.Log
import com.nevo.voip.core.crypto.CryptoManager
import com.nevo.voip.core.model.ChannelListUpdate
import com.nevo.voip.core.model.ChatBroadcast
import com.nevo.voip.core.model.LoginResponse
import com.nevo.voip.core.model.ServerMessage
import com.nevo.voip.core.model.UserJoinedChannel
import com.nevo.voip.core.model.UserLeftChannel
import com.nevo.voip.core.model.UserSpeaking
import com.nevo.voip.core.network.ConnectionState
import com.nevo.voip.core.network.NetworkMonitor
import com.nevo.voip.core.network.TcpConnectionManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ConnectionRepository @Inject constructor(
    private val tcpConnectionManager: TcpConnectionManager,
    private val cryptoManager: CryptoManager,
    private val authenticationManager: AuthenticationManager,
    private val messageDispatcher: MessageDispatcher,
    private val connectionStateManager: ConnectionStateManager,
    private val connectionHistoryManager: ConnectionHistoryManager,
    private val reconnectStrategy: ReconnectStrategy,
    private val networkMonitor: NetworkMonitor,
    private val _channelListUpdates: MutableSharedFlow<ChannelListUpdate>,
    private val _userJoinedChannel: MutableSharedFlow<UserJoinedChannel>,
    private val _userLeftChannel: MutableSharedFlow<UserLeftChannel>,
    private val _userSpeaking: MutableSharedFlow<UserSpeaking>,
    private val _chatMessages: MutableSharedFlow<ChatBroadcast>,
    private val _serverMessages: MutableSharedFlow<ServerMessage>
) {
    companion object {
        private const val TAG = "ConnRepo"
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    val channelListUpdates: SharedFlow<ChannelListUpdate> = _channelListUpdates.asSharedFlow()
    val userJoinedChannel: SharedFlow<UserJoinedChannel> = _userJoinedChannel.asSharedFlow()
    val userLeftChannel: SharedFlow<UserLeftChannel> = _userLeftChannel.asSharedFlow()
    val userSpeaking: SharedFlow<UserSpeaking> = _userSpeaking.asSharedFlow()
    val chatMessages: SharedFlow<ChatBroadcast> = _chatMessages.asSharedFlow()
    val serverMessages: SharedFlow<ServerMessage> = _serverMessages.asSharedFlow()

    val connectionState: StateFlow<ConnectionState> = connectionStateManager.connectionState

    private var receiveJob: Job? = null
    private var reconnectJob: Job? = null

    @Volatile
    private var currentPrivateKey: ByteArray = ByteArray(0)

    @Volatile
    private var currentSessionKey: ByteArray = ByteArray(0)

    @Volatile
    private var currentServerUdpPort: Int = 0

    @Volatile
    private var lastPassword: String = ""

    suspend fun connect(
        host: String,
        port: Int,
        username: String,
        password: String
    ): Result<LoginResponse> {
        connectionStateManager.updateState(ConnectionState.Connecting, "Starting connection")

        // TCP connection
        val tcpResult = tcpConnectionManager.connect(host, port)
        if (tcpResult.isFailure) {
            val error = tcpResult.exceptionOrNull()?.message ?: "TCP connection failed"
            connectionStateManager.updateState(ConnectionState.Error(error), "TCP connection failed")
            return Result.failure(tcpResult.exceptionOrNull() ?: Exception(error))
        }

        // Authentication
        val authResult = authenticationManager.authenticate(username, password, host, port, tcpConnectionManager)
        if (authResult.isFailure) {
            val error = authResult.exceptionOrNull()?.message ?: "Authentication failed"
            connectionStateManager.updateState(ConnectionState.Error(error), "Authentication failed")
            disconnect()
            return Result.failure(authResult.exceptionOrNull() ?: Exception(error))
        }

        val authData = authResult.getOrThrow()

        // Save session data
        currentPrivateKey = authData.privateKey
        currentSessionKey = authData.sessionKey
        currentServerUdpPort = authData.serverUdpPort
        lastPassword = password

        // Save connection history
        connectionHistoryManager.saveConnectionHistory(host, port, username, authData.serverName)

        // Update state
        connectionStateManager.updateState(
            ConnectionState.Connected(
                serverName = authData.serverName,
                userId = authData.userId,
                sessionId = 0L
            ),
            "Connection established"
        )

        // Start receive loop and network monitor
        startReceiveLoop()
        startNetworkMonitor()

        return Result.success(authData.loginResponse)
    }

    fun getCurrentSessionKey(): ByteArray = currentSessionKey.copyOf()

    fun getCurrentServerUdpPort(): Int = currentServerUdpPort

    suspend fun disconnect() {
        reconnectJob?.cancel()
        reconnectJob = null
        receiveJob?.cancel()
        receiveJob = null
        tcpConnectionManager.disconnect()
        connectionStateManager.updateState(ConnectionState.Disconnected, "Disconnected by user")
    }

    private fun startReceiveLoop() {
        receiveJob?.cancel()
        Log.d(TAG, "Starting receive loop")
        receiveJob = scope.launch {
            while (isActive && tcpConnectionManager.isConnected) {
                Log.d(TAG, "Receive loop: waiting for message...")
                val result = tcpConnectionManager.readMessage()
                if (result.isFailure) {
                    Log.e(TAG, "Receive loop: read failed: ${result.exceptionOrNull()?.message}")
                    if (isActive) {
                        connectionStateManager.updateState(
                            ConnectionState.Error(result.exceptionOrNull()?.message ?: "Connection lost"),
                            "Connection lost"
                        )
                        attemptReconnect()
                    }
                    return@launch
                }

                val (messageType, payload) = result.getOrThrow()
                Log.d(TAG, "Receive loop: got msgType=$messageType, payload=${payload.size}B")

                val context = MessageContext(
                    tcpConnectionManager = tcpConnectionManager,
                    cryptoManager = cryptoManager,
                    currentPrivateKey = currentPrivateKey,
                    scope = scope,
                    onKeysRotated = { newPrivateKey, newSessionKey ->
                        // 密钥轮换回写：替换本地私钥与会话密钥
                        currentPrivateKey = newPrivateKey
                        currentSessionKey = newSessionKey
                        Log.d(TAG, "Key rotation applied: privateKey=${newPrivateKey.size}B, sessionKey=${newSessionKey.size}B")
                    }
                )
                messageDispatcher.dispatch(messageType, payload, context)
            }
            Log.d(TAG, "Receive loop exited: isActive=$isActive, isConnected=${tcpConnectionManager.isConnected}")
        }
    }

    private fun startNetworkMonitor() {
        scope.launch {
            networkMonitor.isNetworkAvailable.collect { available ->
                if (!available && connectionStateManager.isConnected()) {
                    connectionStateManager.updateState(
                        ConnectionState.Error("Network unavailable"),
                        "Network unavailable"
                    )
                } else if (available && connectionStateManager.isError()) {
                    attemptReconnect()
                }
            }
        }
    }

    private fun attemptReconnect() {
        if (reconnectJob?.isActive == true) return
        reconnectJob = scope.launch {
            var attempt = 0

            while (isActive && reconnectStrategy.shouldReconnect(attempt)) {
                attempt++
                delay(reconnectStrategy.getDelay(attempt))

                val credentials = connectionHistoryManager.getLastConnection()
                if (credentials == null || lastPassword.isBlank()) {
                    connectionStateManager.updateState(
                        ConnectionState.Disconnected,
                        "No valid credentials for reconnect"
                    )
                    break
                }

                val result = connect(
                    credentials.host,
                    credentials.port,
                    credentials.username,
                    lastPassword
                )
                if (result.isSuccess) {
                    break
                }
            }

            if (attempt >= reconnectStrategy.getMaxAttempts()) {
                connectionStateManager.updateState(
                    ConnectionState.Disconnected,
                    "Max reconnect attempts reached"
                )
            }
        }
    }
}
