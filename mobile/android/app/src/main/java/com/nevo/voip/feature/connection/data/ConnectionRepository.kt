package com.nevo.voip.feature.connection.data

import android.content.Context
import com.nevo.voip.core.crypto.CryptoManager
import com.nevo.voip.core.database.dao.ServerHistoryDao
import com.nevo.voip.core.database.entity.ServerHistoryEntity
import com.nevo.voip.core.datastore.NevoPreferences
import com.nevo.voip.core.model.ChannelListUpdate
import com.nevo.voip.core.model.ChatBroadcast
import com.nevo.voip.core.model.LoginRequest
import com.nevo.voip.core.model.LoginResponse
import com.nevo.voip.core.model.ServerMessage
import com.nevo.voip.core.model.UserJoinedChannel
import com.nevo.voip.core.model.UserLeftChannel
import com.nevo.voip.core.model.UserSpeaking
import com.nevo.voip.core.network.ConnectionState
import com.nevo.voip.core.network.NetworkMonitor
import com.nevo.voip.core.network.TcpConnectionManager
import com.nevo.voip.core.protocol.MessageType
import com.nevo.voip.core.protocol.ProtocolSerializer
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.firstOrNull
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ConnectionRepository @Inject constructor(
    private val tcpConnectionManager: TcpConnectionManager,
    private val cryptoManager: CryptoManager,
    private val serverHistoryDao: ServerHistoryDao,
    private val networkMonitor: NetworkMonitor,
    @ApplicationContext private val context: Context
) {
    private val preferences = NevoPreferences(context)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _channelListUpdates = MutableSharedFlow<ChannelListUpdate>(extraBufferCapacity = 16)
    val channelListUpdates: SharedFlow<ChannelListUpdate> = _channelListUpdates.asSharedFlow()

    private val _userJoinedChannel = MutableSharedFlow<UserJoinedChannel>(extraBufferCapacity = 16)
    val userJoinedChannel: SharedFlow<UserJoinedChannel> = _userJoinedChannel.asSharedFlow()

    private val _userLeftChannel = MutableSharedFlow<UserLeftChannel>(extraBufferCapacity = 16)
    val userLeftChannel: SharedFlow<UserLeftChannel> = _userLeftChannel.asSharedFlow()

    private val _userSpeaking = MutableSharedFlow<UserSpeaking>(extraBufferCapacity = 16)
    val userSpeaking: SharedFlow<UserSpeaking> = _userSpeaking.asSharedFlow()

    private val _chatMessages = MutableSharedFlow<ChatBroadcast>(extraBufferCapacity = 64)
    val chatMessages: SharedFlow<ChatBroadcast> = _chatMessages.asSharedFlow()

    private val _serverMessages = MutableSharedFlow<ServerMessage>(extraBufferCapacity = 16)
    val serverMessages: SharedFlow<ServerMessage> = _serverMessages.asSharedFlow()

    private var receiveJob: Job? = null
    private var reconnectJob: Job? = null

    @Volatile
    private var currentPrivateKey: ByteArray = ByteArray(0)

    @Volatile
    private var sessionToken: String = ""

    @Volatile
    private var currentUserId: Long = 0

    private var lastServerName: String? = null

    suspend fun connect(
        host: String,
        port: Int,
        username: String,
        password: String
    ): Result<LoginResponse> {
        _connectionState.value = ConnectionState.Connecting

        val tcpResult = tcpConnectionManager.connect(host, port)
        if (tcpResult.isFailure) {
            val error = tcpResult.exceptionOrNull()?.message ?: "TCP connection failed"
            _connectionState.value = ConnectionState.Error(error)
            return Result.failure(tcpResult.exceptionOrNull() ?: Exception(error))
        }

        val (publicKey, privateKey) = cryptoManager.generateKeyPair()
        currentPrivateKey = privateKey

        val authCredential = password.toByteArray(Charsets.UTF_8)
        val loginRequest = LoginRequest(
            username = username,
            authCredential = authCredential,
            keyExchangeMethods = listOf("X25519+crypto_box_seal", "X25519"),
            clientPublicKey = publicKey,
            clientUdpPort = 0,
            clientVideoUdpPort = 0
        )

        val serialized = ProtocolSerializer.serializeLoginRequest(loginRequest)
        val sendResult = tcpConnectionManager.sendMessage(
            MessageType.LOGIN_REQUEST.id,
            serialized
        )
        if (sendResult.isFailure) {
            val error = sendResult.exceptionOrNull()?.message ?: "Failed to send login request"
            _connectionState.value = ConnectionState.Error(error)
            disconnect()
            return Result.failure(sendResult.exceptionOrNull() ?: Exception(error))
        }

        val readResult = tcpConnectionManager.readMessage()
        if (readResult.isFailure) {
            val error = readResult.exceptionOrNull()?.message ?: "Failed to read login response"
            _connectionState.value = ConnectionState.Error(error)
            disconnect()
            return Result.failure(readResult.exceptionOrNull() ?: Exception(error))
        }

        val (messageType, payload) = readResult.getOrThrow()
        if (messageType != MessageType.LOGIN_RESPONSE.id) {
            val error = "Unexpected message type: $messageType"
            _connectionState.value = ConnectionState.Error(error)
            disconnect()
            return Result.failure(Exception(error))
        }

        val loginResponse = ProtocolSerializer.deserializeLoginResponse(payload)

        if (loginResponse.result != 0) {
            val error = "Login failed with result code: ${loginResponse.result}"
            _connectionState.value = ConnectionState.Error(error)
            disconnect()
            return Result.failure(Exception(error))
        }

        val sessionKey = decryptSessionKey(loginResponse)

        sessionToken = loginResponse.sessionToken
        currentUserId = loginResponse.userInfo?.id ?: 0

        preferences.setLastConnectedHost(host)
        preferences.setLastConnectedPort(port)
        preferences.setLastUsername(username)

        val serverName = loginResponse.userInfo?.username?.let {
            if (it.isNotBlank()) it else host
        } ?: host
        lastServerName = serverName

        val historyEntity = ServerHistoryEntity(
            host = host,
            port = port,
            username = username,
            lastConnectedAt = System.currentTimeMillis(),
            serverName = serverName
        )
        serverHistoryDao.insertOrUpdate(historyEntity)

        _connectionState.value = ConnectionState.Connected(
            serverName = serverName,
            userId = currentUserId,
            sessionId = 0L
        )

        startReceiveLoop()
        startNetworkMonitor()

        return Result.success(loginResponse)
    }

    private fun decryptSessionKey(loginResponse: LoginResponse): ByteArray {
        val encryptedSessionKey = loginResponse.encryptedSessionKey
        var sessionKey = cryptoManager.decryptSealed(encryptedSessionKey, currentPrivateKey)

        if (sessionKey == null) {
            val fallbackKey = loginResponse.serverPublicKey.takeLast(32).toByteArray()
            if (fallbackKey.size == 32) {
                sessionKey = cryptoManager.decryptSealed(encryptedSessionKey, fallbackKey)
            }
        }

        return sessionKey ?: encryptedSessionKey
    }

    suspend fun disconnect() {
        reconnectJob?.cancel()
        reconnectJob = null
        receiveJob?.cancel()
        receiveJob = null
        tcpConnectionManager.disconnect()
        _connectionState.value = ConnectionState.Disconnected
    }

    private fun startReceiveLoop() {
        receiveJob?.cancel()
        receiveJob = scope.launch {
            while (isActive && tcpConnectionManager.isConnected) {
                val result = tcpConnectionManager.readMessage()
                if (result.isFailure) {
                    if (isActive) {
                        _connectionState.value = ConnectionState.Error(
                            result.exceptionOrNull()?.message ?: "Connection lost"
                        )
                        attemptReconnect()
                    }
                    return@launch
                }

                val (messageType, payload) = result.getOrThrow()
                dispatchMessage(messageType, payload)
            }
        }
    }

    private fun dispatchMessage(messageTypeId: Int, payload: ByteArray) {
        val msgType = MessageType.fromId(messageTypeId)
        when (msgType) {
            MessageType.CHANNEL_LIST_UPDATE -> {
                val update = ProtocolSerializer.deserializeChannelListUpdate(payload)
                handleChannelListUpdate(update)
            }
            MessageType.USER_JOINED_CHANNEL -> {
                val event = ProtocolSerializer.deserializeUserJoinedChannel(payload)
                handleUserJoined(event)
            }
            MessageType.USER_LEFT_CHANNEL -> {
                val event = ProtocolSerializer.deserializeUserLeftChannel(payload)
                handleUserLeft(event)
            }
            MessageType.USER_SPEAKING -> {
                val event = ProtocolSerializer.deserializeUserSpeaking(payload)
                handleUserSpeaking(event)
            }
            MessageType.CHAT_BROADCAST -> {
                val event = ProtocolSerializer.deserializeChatBroadcast(payload)
                handleChatBroadcast(event)
            }
            MessageType.SERVER_MESSAGE -> {
                val event = ProtocolSerializer.deserializeServerMessage(payload)
                handleServerMessage(event)
            }
            MessageType.KEY_ROTATION_REQUEST -> {
                val event = ProtocolSerializer.deserializeKeyRotationRequest(payload)
                handleKeyRotation(event)
            }
            MessageType.SPEAKING_STATE -> Unit
            else -> Unit
        }
    }

    private fun handleChannelListUpdate(update: ChannelListUpdate) {
        _channelListUpdates.tryEmit(update)
    }

    private fun handleUserJoined(event: UserJoinedChannel) {
        _userJoinedChannel.tryEmit(event)
    }

    private fun handleUserLeft(event: UserLeftChannel) {
        _userLeftChannel.tryEmit(event)
    }

    private fun handleUserSpeaking(event: UserSpeaking) {
        _userSpeaking.tryEmit(event)
    }

    private fun handleChatBroadcast(event: ChatBroadcast) {
        _chatMessages.tryEmit(event)
    }

    private fun handleServerMessage(event: ServerMessage) {
        _serverMessages.tryEmit(event)
    }

    private fun handleKeyRotation(event: com.nevo.voip.core.model.KeyRotationRequest) {
        val decrypted = cryptoManager.decryptSealed(
            event.encryptedSessionKey,
            currentPrivateKey
        )
        if (decrypted != null) {
            scope.launch {
                val newKeyPair = cryptoManager.generateKeyPair()
                val response = com.nevo.voip.core.model.KeyRotationResponse(
                    newClientPublicKey = newKeyPair.first,
                    keyEpoch = event.keyEpoch
                )
                currentPrivateKey = newKeyPair.second
                val serialized = ProtocolSerializer.serializeKeyRotationResponse(response)
                tcpConnectionManager.sendMessage(
                    MessageType.KEY_ROTATION_RESPONSE.id,
                    serialized
                )
            }
        }
    }

    private fun startNetworkMonitor() {
        scope.launch {
            networkMonitor.isNetworkAvailable.collect { available ->
                if (!available && _connectionState.value is ConnectionState.Connected) {
                    _connectionState.value = ConnectionState.Error("Network unavailable")
                } else if (available && _connectionState.value is ConnectionState.Error) {
                    attemptReconnect()
                }
            }
        }
    }

    private fun attemptReconnect() {
        if (reconnectJob?.isActive == true) return
        reconnectJob = scope.launch {
            var attempt = 0
            val maxAttempts = 5
            val baseDelay = 2000L

            while (isActive && attempt < maxAttempts) {
                attempt++
                delay(baseDelay * attempt.coerceAtMost(5))

                val host = preferences.lastConnectedHost.firstOrNull() ?: break
                val port = preferences.lastConnectedPort.firstOrNull() ?: break
                val username = preferences.lastUsername.firstOrNull() ?: break

                if (host.isBlank() || port == 0) break

                val result = tcpConnectionManager.connect(host, port)
                if (result.isSuccess) {
                    startReceiveLoop()
                    break
                }
            }

            if (attempt >= maxAttempts) {
                _connectionState.value = ConnectionState.Disconnected
            }
        }
    }
}