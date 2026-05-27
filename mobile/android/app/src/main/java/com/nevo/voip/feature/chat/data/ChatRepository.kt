package com.nevo.voip.feature.chat.data

import com.nevo.voip.core.database.dao.ChatMessageDao
import com.nevo.voip.core.database.entity.ChatMessageEntity
import com.nevo.voip.core.model.ChatBroadcast
import com.nevo.voip.core.model.ChatSendRequest
import com.nevo.voip.core.network.TcpConnectionManager
import com.nevo.voip.core.protocol.MessageType
import com.nevo.voip.core.protocol.ProtocolSerializer
import com.nevo.voip.feature.connection.data.ConnectionRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.withContext
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ChatRepository @Inject constructor(
    private val chatMessageDao: ChatMessageDao,
    private val tcpConnectionManager: TcpConnectionManager,
    private val connectionRepository: ConnectionRepository
) {
    val chatBroadcasts: SharedFlow<ChatBroadcast> = connectionRepository.chatMessages

    suspend fun sendMessage(channelId: Long, text: String): Result<Unit> = withContext(Dispatchers.IO) {
        val request = ChatSendRequest(channelId = channelId, text = text)
        val payload = ProtocolSerializer.serializeChatSendRequest(request)
        tcpConnectionManager.sendMessage(MessageType.CHAT_SEND_REQUEST.id, payload)
    }

    fun getMessages(channelId: Long): Flow<List<ChatMessageEntity>> {
        return chatMessageDao.getMessagesByChannel(channelId.toInt())
    }

    suspend fun insertMessage(entity: ChatMessageEntity) {
        chatMessageDao.insertMessage(entity)
    }

    suspend fun markMessageSent(messageId: Long) {
        chatMessageDao.markSent(messageId)
    }
}