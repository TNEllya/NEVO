package com.nevo.voip.feature.channel.data

import com.nevo.voip.core.model.CreateChannelRequest
import com.nevo.voip.core.model.DeleteChannelRequest
import com.nevo.voip.core.model.JoinChannelRequest
import com.nevo.voip.core.model.LeaveChannelRequest
import com.nevo.voip.core.model.RenameChannelRequest
import com.nevo.voip.core.network.TcpConnectionManager
import com.nevo.voip.core.protocol.MessageType
import com.nevo.voip.core.protocol.ProtocolSerializer
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ChannelRepository @Inject constructor(
    private val tcpConnectionManager: TcpConnectionManager
) {
    suspend fun joinChannel(channelId: Long): Result<Unit> = withContext(Dispatchers.IO) {
        val request = JoinChannelRequest(channelId = channelId)
        val payload = ProtocolSerializer.serializeJoinChannelRequest(request)
        tcpConnectionManager.sendMessage(MessageType.JOIN_CHANNEL_REQUEST.id, payload)
    }

    suspend fun leaveChannel(): Result<Unit> = withContext(Dispatchers.IO) {
        val request = LeaveChannelRequest()
        val payload = ProtocolSerializer.serializeLeaveChannelRequest(request)
        tcpConnectionManager.sendMessage(MessageType.LEAVE_CHANNEL_REQUEST.id, payload)
    }

    suspend fun createChannel(name: String, parentId: Long): Result<Unit> = withContext(Dispatchers.IO) {
        val request = CreateChannelRequest(parentId = parentId, name = name)
        val payload = ProtocolSerializer.serializeCreateChannelRequest(request)
        tcpConnectionManager.sendMessage(MessageType.CREATE_CHANNEL_REQUEST.id, payload)
    }

    suspend fun deleteChannel(channelId: Long): Result<Unit> = withContext(Dispatchers.IO) {
        val request = DeleteChannelRequest(channelId = channelId)
        val payload = ProtocolSerializer.serializeDeleteChannelRequest(request)
        tcpConnectionManager.sendMessage(MessageType.DELETE_CHANNEL_REQUEST.id, payload)
    }

    suspend fun renameChannel(channelId: Long, newName: String): Result<Unit> = withContext(Dispatchers.IO) {
        val request = RenameChannelRequest(channelId = channelId, newName = newName)
        val payload = ProtocolSerializer.serializeRenameChannelRequest(request)
        tcpConnectionManager.sendMessage(MessageType.RENAME_CHANNEL_REQUEST.id, payload)
    }
}