package com.nevo.voip.core.protocol

import com.nevo.voip.core.model.*

object ProtocolSerializer {

    fun serializeUserInfo(msg: UserInfo): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.id)
        buf.writeString(msg.username)
        buf.writeU32(msg.status)
        buf.writeBool(msg.muted)
        buf.writeBool(msg.deafened)
        buf.writeU32(msg.groupId)
        return buf.getBytes()
    }

    fun deserializeUserInfo(data: ByteArray): UserInfo {
        val buf = NevoBuffer(data)
        return UserInfo(
            id = buf.readU64(),
            username = buf.readString(),
            status = buf.readU32(),
            muted = buf.readBool(),
            deafened = buf.readBool(),
            groupId = buf.readU32()
        )
    }

    fun serializeChannelInfo(msg: ChannelInfo): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.id)
        buf.writeString(msg.name)
        buf.writeU64(msg.parentId)
        buf.writeU32(msg.children.size)
        for (child in msg.children) {
            val childData = serializeChannelInfo(child)
            buf.writeU32(childData.size)
            buf.writeBytesRaw(childData)
        }
        buf.writeU32(msg.users.size)
        for (user in msg.users) {
            val userData = serializeUserInfo(user)
            buf.writeU32(userData.size)
            buf.writeBytesRaw(userData)
        }
        return buf.getBytes()
    }

    fun deserializeChannelInfo(data: ByteArray): ChannelInfo {
        val buf = NevoBuffer(data)
        val id = buf.readU64()
        val name = buf.readString()
        val parentId = buf.readU64()
        val childrenCount = buf.readU32()
        val children = mutableListOf<ChannelInfo>()
        for (i in 0 until childrenCount) {
            val size = buf.readU32()
            val childData = buf.readRaw(size)
            children.add(deserializeChannelInfo(childData))
        }
        val usersCount = buf.readU32()
        val users = mutableListOf<UserInfo>()
        for (i in 0 until usersCount) {
            val size = buf.readU32()
            val userData = buf.readRaw(size)
            users.add(deserializeUserInfo(userData))
        }
        return ChannelInfo(
            id = id,
            name = name,
            parentId = parentId,
            children = children,
            users = users
        )
    }

    fun serializeLoginRequest(msg: LoginRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeString(msg.username)
        buf.writeBytes(msg.authCredential)
        buf.writeU32(msg.keyExchangeMethods.size)
        for (method in msg.keyExchangeMethods) {
            buf.writeString(method)
        }
        buf.writeBytes(msg.clientPublicKey)
        buf.writeU16(msg.clientUdpPort)
        if (msg.clientVideoUdpPort > 0) {
            buf.writeU16(msg.clientVideoUdpPort)
        }
        return buf.getBytes()
    }

    fun deserializeLoginRequest(data: ByteArray): LoginRequest {
        val buf = NevoBuffer(data)
        val username = buf.readString()
        val authCredential = buf.readBytes()
        val count = buf.readU32()
        val keyExchangeMethods = mutableListOf<String>()
        for (i in 0 until count) {
            keyExchangeMethods.add(buf.readString())
        }
        val clientPublicKey = buf.readBytes()
        val clientUdpPort = try {
            buf.readU16()
        } catch (e: Exception) {
            0
        }
        val clientVideoUdpPort = try {
            buf.readU16()
        } catch (e: Exception) {
            0
        }
        return LoginRequest(
            username = username,
            authCredential = authCredential,
            keyExchangeMethods = keyExchangeMethods,
            clientPublicKey = clientPublicKey,
            clientUdpPort = clientUdpPort,
            clientVideoUdpPort = clientVideoUdpPort
        )
    }

    fun serializeLoginResponse(msg: LoginResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.result)
        if (msg.userInfo != null) {
            val userData = serializeUserInfo(msg.userInfo)
            buf.writeU32(userData.size)
            buf.writeBytesRaw(userData)
        } else {
            buf.writeU32(0)
        }
        buf.writeString(msg.sessionToken)
        buf.writeBytes(msg.serverPublicKey)
        buf.writeString(msg.keyExchangeMethod)
        buf.writeBytes(msg.encryptedSessionKey)
        buf.writeU32(msg.ownerExists)
        buf.writeU16(msg.serverUdpPort)
        if (msg.serverVideoUdpPort > 0) {
            buf.writeU16(msg.serverVideoUdpPort)
        }
        return buf.getBytes()
    }

    fun deserializeLoginResponse(data: ByteArray): LoginResponse {
        val buf = NevoBuffer(data)
        val result = buf.readU32()
        val userSize = buf.readU32()
        val userInfo = if (userSize > 0) {
            val userData = buf.readRaw(userSize)
            deserializeUserInfo(userData)
        } else {
            null
        }
        val sessionToken = buf.readString()
        val serverPublicKey = buf.readBytes()
        val keyExchangeMethod = buf.readString()
        val encryptedSessionKey = buf.readBytes()
        val ownerExists = buf.readU32()
        val serverUdpPort = try {
            buf.readU16()
        } catch (e: Exception) {
            0
        }
        val serverVideoUdpPort = try {
            buf.readU16()
        } catch (e: Exception) {
            0
        }
        return LoginResponse(
            result = result,
            userInfo = userInfo,
            sessionToken = sessionToken,
            serverPublicKey = serverPublicKey,
            keyExchangeMethod = keyExchangeMethod,
            encryptedSessionKey = encryptedSessionKey,
            ownerExists = ownerExists,
            serverUdpPort = serverUdpPort,
            serverVideoUdpPort = serverVideoUdpPort
        )
    }

    fun serializeJoinChannelRequest(msg: JoinChannelRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.channelId)
        return buf.getBytes()
    }

    fun deserializeJoinChannelRequest(data: ByteArray): JoinChannelRequest {
        val buf = NevoBuffer(data)
        return JoinChannelRequest(channelId = buf.readU64())
    }

    fun serializeLeaveChannelRequest(msg: LeaveChannelRequest): ByteArray {
        return ByteArray(0)
    }

    fun deserializeLeaveChannelRequest(data: ByteArray): LeaveChannelRequest {
        return LeaveChannelRequest()
    }

    fun serializeCreateChannelRequest(msg: CreateChannelRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.parentId)
        buf.writeString(msg.name)
        return buf.getBytes()
    }

    fun deserializeCreateChannelRequest(data: ByteArray): CreateChannelRequest {
        val buf = NevoBuffer(data)
        return CreateChannelRequest(
            parentId = buf.readU64(),
            name = buf.readString()
        )
    }

    fun serializeDeleteChannelRequest(msg: DeleteChannelRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.channelId)
        return buf.getBytes()
    }

    fun deserializeDeleteChannelRequest(data: ByteArray): DeleteChannelRequest {
        val buf = NevoBuffer(data)
        return DeleteChannelRequest(channelId = buf.readU64())
    }

    fun serializeRenameChannelRequest(msg: RenameChannelRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.channelId)
        buf.writeString(msg.newName)
        return buf.getBytes()
    }

    fun deserializeRenameChannelRequest(data: ByteArray): RenameChannelRequest {
        val buf = NevoBuffer(data)
        return RenameChannelRequest(
            channelId = buf.readU64(),
            newName = buf.readString()
        )
    }

    fun serializeRenameChannelResponse(msg: RenameChannelResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.channelId)
        buf.writeString(msg.newName)
        return buf.getBytes()
    }

    fun deserializeRenameChannelResponse(data: ByteArray): RenameChannelResponse {
        val buf = NevoBuffer(data)
        return RenameChannelResponse(
            channelId = buf.readU64(),
            newName = buf.readString()
        )
    }

    fun serializeChannelListUpdate(msg: ChannelListUpdate): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.channels.size)
        for (channel in msg.channels) {
            val channelData = serializeChannelInfo(channel)
            buf.writeU32(channelData.size)
            buf.writeBytesRaw(channelData)
        }
        return buf.getBytes()
    }

    fun deserializeChannelListUpdate(data: ByteArray): ChannelListUpdate {
        val buf = NevoBuffer(data)
        val count = buf.readU32()
        val channels = mutableListOf<ChannelInfo>()
        for (i in 0 until count) {
            val size = buf.readU32()
            val channelData = buf.readRaw(size)
            channels.add(deserializeChannelInfo(channelData))
        }
        return ChannelListUpdate(channels = channels)
    }

    fun serializeUserJoinedChannel(msg: UserJoinedChannel): ByteArray {
        val buf = NevoBuffer()
        if (msg.user != null) {
            val userData = serializeUserInfo(msg.user)
            buf.writeU32(userData.size)
            buf.writeBytesRaw(userData)
        } else {
            buf.writeU32(0)
        }
        buf.writeU64(msg.channelId)
        return buf.getBytes()
    }

    fun deserializeUserJoinedChannel(data: ByteArray): UserJoinedChannel {
        val buf = NevoBuffer(data)
        val userSize = buf.readU32()
        val user = if (userSize > 0) {
            val userData = buf.readRaw(userSize)
            deserializeUserInfo(userData)
        } else {
            null
        }
        val channelId = buf.readU64()
        return UserJoinedChannel(user = user, channelId = channelId)
    }

    fun serializeUserLeftChannel(msg: UserLeftChannel): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.userId)
        buf.writeU64(msg.channelId)
        return buf.getBytes()
    }

    fun deserializeUserLeftChannel(data: ByteArray): UserLeftChannel {
        val buf = NevoBuffer(data)
        return UserLeftChannel(
            userId = buf.readU64(),
            channelId = buf.readU64()
        )
    }

    fun serializeUserSpeaking(msg: UserSpeaking): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.userId)
        buf.writeBool(msg.speaking)
        return buf.getBytes()
    }

    fun deserializeUserSpeaking(data: ByteArray): UserSpeaking {
        val buf = NevoBuffer(data)
        return UserSpeaking(
            userId = buf.readU64(),
            speaking = buf.readBool()
        )
    }

    fun serializePttToggle(msg: PttToggle): ByteArray {
        val buf = NevoBuffer()
        buf.writeBool(msg.active)
        return buf.getBytes()
    }

    fun deserializePttToggle(data: ByteArray): PttToggle {
        val buf = NevoBuffer(data)
        return PttToggle(active = buf.readBool())
    }

    fun serializeUserMuteToggle(msg: UserMuteToggle): ByteArray {
        val buf = NevoBuffer()
        buf.writeBool(msg.muted)
        return buf.getBytes()
    }

    fun deserializeUserMuteToggle(data: ByteArray): UserMuteToggle {
        val buf = NevoBuffer(data)
        return UserMuteToggle(muted = buf.readBool())
    }

    fun serializeSpeakingState(msg: SpeakingState): ByteArray {
        val buf = NevoBuffer()
        buf.writeBool(msg.speaking)
        return buf.getBytes()
    }

    fun deserializeSpeakingState(data: ByteArray): SpeakingState {
        val buf = NevoBuffer(data)
        return SpeakingState(speaking = buf.readBool())
    }

    fun serializeServerMessage(msg: ServerMessage): ByteArray {
        val buf = NevoBuffer()
        buf.writeString(msg.text)
        return buf.getBytes()
    }

    fun deserializeServerMessage(data: ByteArray): ServerMessage {
        val buf = NevoBuffer(data)
        return ServerMessage(text = buf.readString())
    }

    fun serializeStunBindRequest(msg: StunBindRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.transactionId)
        return buf.getBytes()
    }

    fun deserializeStunBindRequest(data: ByteArray): StunBindRequest {
        val buf = NevoBuffer(data)
        return StunBindRequest(transactionId = buf.readU32())
    }

    fun serializeStunBindResponse(msg: StunBindResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.transactionId)
        buf.writeBytes(msg.mappedAddress)
        buf.writeU32(msg.natType)
        return buf.getBytes()
    }

    fun deserializeStunBindResponse(data: ByteArray): StunBindResponse {
        val buf = NevoBuffer(data)
        return StunBindResponse(
            transactionId = buf.readU32(),
            mappedAddress = buf.readBytes(),
            natType = buf.readU32()
        )
    }

    fun serializeUdpPingRequest(msg: UdpPingRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.sequence)
        buf.writeBytes(msg.clientUdpKey)
        return buf.getBytes()
    }

    fun deserializeUdpPingRequest(data: ByteArray): UdpPingRequest {
        val buf = NevoBuffer(data)
        return UdpPingRequest(
            sequence = buf.readU32(),
            clientUdpKey = buf.readBytes()
        )
    }

    fun serializeUdpPingResponse(msg: UdpPingResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.sequence)
        buf.writeBool(msg.udpReachable)
        return buf.getBytes()
    }

    fun deserializeUdpPingResponse(data: ByteArray): UdpPingResponse {
        val buf = NevoBuffer(data)
        return UdpPingResponse(
            sequence = buf.readU32(),
            udpReachable = buf.readBool()
        )
    }

    fun serializeKeyRotationRequest(msg: KeyRotationRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeBytes(msg.newServerPublicKey)
        buf.writeU64(msg.keyEpoch)
        buf.writeBytes(msg.encryptedSessionKey)
        return buf.getBytes()
    }

    fun deserializeKeyRotationRequest(data: ByteArray): KeyRotationRequest {
        val buf = NevoBuffer(data)
        return KeyRotationRequest(
            newServerPublicKey = buf.readBytes(),
            keyEpoch = buf.readU64(),
            encryptedSessionKey = buf.readBytes()
        )
    }

    fun serializeKeyRotationResponse(msg: KeyRotationResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeBytes(msg.newClientPublicKey)
        buf.writeU64(msg.keyEpoch)
        return buf.getBytes()
    }

    fun deserializeKeyRotationResponse(data: ByteArray): KeyRotationResponse {
        val buf = NevoBuffer(data)
        return KeyRotationResponse(
            newClientPublicKey = buf.readBytes(),
            keyEpoch = buf.readU64()
        )
    }

    fun serializeAdminAuthRequest(msg: AdminAuthRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeString(msg.password)
        return buf.getBytes()
    }

    fun deserializeAdminAuthRequest(data: ByteArray): AdminAuthRequest {
        val buf = NevoBuffer(data)
        return AdminAuthRequest(password = buf.readString())
    }

    fun serializeAdminAuthResponse(msg: AdminAuthResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.result)
        buf.writeString(msg.message)
        return buf.getBytes()
    }

    fun deserializeAdminAuthResponse(data: ByteArray): AdminAuthResponse {
        val buf = NevoBuffer(data)
        return AdminAuthResponse(
            result = buf.readU32(),
            message = buf.readString()
        )
    }

    fun serializeSetAdminRequest(msg: SetAdminRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.userId)
        buf.writeBool(msg.setAdmin)
        return buf.getBytes()
    }

    fun deserializeSetAdminRequest(data: ByteArray): SetAdminRequest {
        val buf = NevoBuffer(data)
        return SetAdminRequest(
            userId = buf.readU64(),
            setAdmin = buf.readBool()
        )
    }

    fun serializeSetAdminResponse(msg: SetAdminResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.result)
        buf.writeString(msg.message)
        return buf.getBytes()
    }

    fun deserializeSetAdminResponse(data: ByteArray): SetAdminResponse {
        val buf = NevoBuffer(data)
        return SetAdminResponse(
            result = buf.readU32(),
            message = buf.readString()
        )
    }

    fun serializeSetServerNameRequest(msg: SetServerNameRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeString(msg.serverName)
        return buf.getBytes()
    }

    fun deserializeSetServerNameRequest(data: ByteArray): SetServerNameRequest {
        val buf = NevoBuffer(data)
        return SetServerNameRequest(serverName = buf.readString())
    }

    fun serializeSetServerNameResponse(msg: SetServerNameResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.result)
        buf.writeString(msg.message)
        return buf.getBytes()
    }

    fun deserializeSetServerNameResponse(data: ByteArray): SetServerNameResponse {
        val buf = NevoBuffer(data)
        return SetServerNameResponse(
            result = buf.readU32(),
            message = buf.readString()
        )
    }

    fun serializeKickUserRequest(msg: KickUserRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.userId)
        buf.writeString(msg.reason)
        return buf.getBytes()
    }

    fun deserializeKickUserRequest(data: ByteArray): KickUserRequest {
        val buf = NevoBuffer(data)
        return KickUserRequest(
            userId = buf.readU64(),
            reason = buf.readString()
        )
    }

    fun serializeKickUserResponse(msg: KickUserResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.result)
        buf.writeString(msg.message)
        return buf.getBytes()
    }

    fun deserializeKickUserResponse(data: ByteArray): KickUserResponse {
        val buf = NevoBuffer(data)
        return KickUserResponse(
            result = buf.readU32(),
            message = buf.readString()
        )
    }

    fun serializeBanUserRequest(msg: BanUserRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.userId)
        buf.writeString(msg.reason)
        buf.writeU64(msg.expiresAt)
        return buf.getBytes()
    }

    fun deserializeBanUserRequest(data: ByteArray): BanUserRequest {
        val buf = NevoBuffer(data)
        return BanUserRequest(
            userId = buf.readU64(),
            reason = buf.readString(),
            expiresAt = buf.readU64()
        )
    }

    fun serializeBanUserResponse(msg: BanUserResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.result)
        buf.writeString(msg.message)
        return buf.getBytes()
    }

    fun deserializeBanUserResponse(data: ByteArray): BanUserResponse {
        val buf = NevoBuffer(data)
        return BanUserResponse(
            result = buf.readU32(),
            message = buf.readString()
        )
    }

    fun serializeMoveUserRequest(msg: MoveUserRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.userId)
        buf.writeU64(msg.channelId)
        return buf.getBytes()
    }

    fun deserializeMoveUserRequest(data: ByteArray): MoveUserRequest {
        val buf = NevoBuffer(data)
        return MoveUserRequest(
            userId = buf.readU64(),
            channelId = buf.readU64()
        )
    }

    fun serializeMoveUserResponse(msg: MoveUserResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.result)
        buf.writeString(msg.message)
        return buf.getBytes()
    }

    fun deserializeMoveUserResponse(data: ByteArray): MoveUserResponse {
        val buf = NevoBuffer(data)
        return MoveUserResponse(
            result = buf.readU32(),
            message = buf.readString()
        )
    }

    fun serializeChatSendRequest(msg: ChatSendRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.channelId)
        buf.writeString(msg.text)
        return buf.getBytes()
    }

    fun deserializeChatSendRequest(data: ByteArray): ChatSendRequest {
        val buf = NevoBuffer(data)
        return ChatSendRequest(
            channelId = buf.readU64(),
            text = buf.readString()
        )
    }

    fun serializeChatBroadcast(msg: ChatBroadcast): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.senderId)
        buf.writeString(msg.senderName)
        buf.writeU64(msg.channelId)
        buf.writeString(msg.text)
        buf.writeU64(msg.timestamp)
        return buf.getBytes()
    }

    fun deserializeChatBroadcast(data: ByteArray): ChatBroadcast {
        val buf = NevoBuffer(data)
        return ChatBroadcast(
            senderId = buf.readU64(),
            senderName = buf.readString(),
            channelId = buf.readU64(),
            text = buf.readString(),
            timestamp = buf.readU64()
        )
    }

    fun serializeFileEntry(msg: FileEntry): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.id)
        buf.writeU64(msg.channelId)
        buf.writeU64(msg.uploaderId)
        buf.writeString(msg.filename)
        buf.writeU64(msg.fileSize)
        buf.writeU64(msg.uploadTime)
        return buf.getBytes()
    }

    fun deserializeFileEntry(data: ByteArray): FileEntry {
        val buf = NevoBuffer(data)
        return FileEntry(
            id = buf.readU64(),
            channelId = buf.readU64(),
            uploaderId = buf.readU64(),
            filename = buf.readString(),
            fileSize = buf.readU64(),
            uploadTime = buf.readU64()
        )
    }

    fun serializeFileListRequest(msg: FileListRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.channelId)
        return buf.getBytes()
    }

    fun deserializeFileListRequest(data: ByteArray): FileListRequest {
        val buf = NevoBuffer(data)
        return FileListRequest(channelId = buf.readU64())
    }

    fun serializeFileListResponse(msg: FileListResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.entries.size)
        for (entry in msg.entries) {
            val entryData = serializeFileEntry(entry)
            buf.writeBytesRaw(entryData)
        }
        return buf.getBytes()
    }

    fun deserializeFileListResponse(data: ByteArray): FileListResponse {
        val buf = NevoBuffer(data)
        val count = buf.readU32()
        val entries = mutableListOf<FileEntry>()
        for (i in 0 until count) {
            entries.add(
                FileEntry(
                    id = buf.readU64(),
                    channelId = buf.readU64(),
                    uploaderId = buf.readU64(),
                    filename = buf.readString(),
                    fileSize = buf.readU64(),
                    uploadTime = buf.readU64()
                )
            )
        }
        return FileListResponse(entries = entries)
    }

    fun serializeFileUploadRequest(msg: FileUploadRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.channelId)
        buf.writeString(msg.filename)
        buf.writeU64(msg.fileSize)
        return buf.getBytes()
    }

    fun deserializeFileUploadRequest(data: ByteArray): FileUploadRequest {
        val buf = NevoBuffer(data)
        return FileUploadRequest(
            channelId = buf.readU64(),
            filename = buf.readString(),
            fileSize = buf.readU64()
        )
    }

    fun serializeFileUploadResponse(msg: FileUploadResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.result)
        buf.writeString(msg.message)
        buf.writeU64(msg.fileId)
        return buf.getBytes()
    }

    fun deserializeFileUploadResponse(data: ByteArray): FileUploadResponse {
        val buf = NevoBuffer(data)
        return FileUploadResponse(
            result = buf.readU32(),
            message = buf.readString(),
            fileId = buf.readU64()
        )
    }

    fun serializeFileDeleteRequest(msg: FileDeleteRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.fileId)
        return buf.getBytes()
    }

    fun deserializeFileDeleteRequest(data: ByteArray): FileDeleteRequest {
        val buf = NevoBuffer(data)
        return FileDeleteRequest(fileId = buf.readU64())
    }

    fun serializeFileDeleteResponse(msg: FileDeleteResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.result)
        buf.writeString(msg.message)
        return buf.getBytes()
    }

    fun deserializeFileDeleteResponse(data: ByteArray): FileDeleteResponse {
        val buf = NevoBuffer(data)
        return FileDeleteResponse(
            result = buf.readU32(),
            message = buf.readString()
        )
    }

    fun serializeFileDownloadRequest(msg: FileDownloadRequest): ByteArray {
        val buf = NevoBuffer()
        buf.writeU64(msg.fileId)
        return buf.getBytes()
    }

    fun deserializeFileDownloadRequest(data: ByteArray): FileDownloadRequest {
        val buf = NevoBuffer(data)
        return FileDownloadRequest(fileId = buf.readU64())
    }

    fun serializeFileDownloadResponse(msg: FileDownloadResponse): ByteArray {
        val buf = NevoBuffer()
        buf.writeU32(msg.result)
        buf.writeString(msg.message)
        return buf.getBytes()
    }

    fun deserializeFileDownloadResponse(data: ByteArray): FileDownloadResponse {
        val buf = NevoBuffer(data)
        return FileDownloadResponse(
            result = buf.readU32(),
            message = buf.readString()
        )
    }

    data class MessageSerializerEntry(
        val caseValue: Int,
        val serializer: (Any) -> ByteArray,
        val deserializer: (ByteArray) -> Any
    )

    data class CaseDeserializerEntry(
        val messageType: MessageType,
        val deserializer: (ByteArray) -> Any
    )

    val MESSAGE_TYPE_MAP: Map<MessageType, MessageSerializerEntry> = mapOf(
        MessageType.LOGIN_REQUEST to MessageSerializerEntry(1, { serializeLoginRequest(it as LoginRequest) }, { deserializeLoginRequest(it) }),
        MessageType.LOGIN_RESPONSE to MessageSerializerEntry(2, { serializeLoginResponse(it as LoginResponse) }, { deserializeLoginResponse(it) }),
        MessageType.JOIN_CHANNEL_REQUEST to MessageSerializerEntry(3, { serializeJoinChannelRequest(it as JoinChannelRequest) }, { deserializeJoinChannelRequest(it) }),
        MessageType.LEAVE_CHANNEL_REQUEST to MessageSerializerEntry(4, { serializeLeaveChannelRequest(it as LeaveChannelRequest) }, { deserializeLeaveChannelRequest(it) }),
        MessageType.CREATE_CHANNEL_REQUEST to MessageSerializerEntry(5, { serializeCreateChannelRequest(it as CreateChannelRequest) }, { deserializeCreateChannelRequest(it) }),
        MessageType.DELETE_CHANNEL_REQUEST to MessageSerializerEntry(6, { serializeDeleteChannelRequest(it as DeleteChannelRequest) }, { deserializeDeleteChannelRequest(it) }),
        MessageType.CHANNEL_LIST_UPDATE to MessageSerializerEntry(7, { serializeChannelListUpdate(it as ChannelListUpdate) }, { deserializeChannelListUpdate(it) }),
        MessageType.USER_JOINED_CHANNEL to MessageSerializerEntry(8, { serializeUserJoinedChannel(it as UserJoinedChannel) }, { deserializeUserJoinedChannel(it) }),
        MessageType.USER_LEFT_CHANNEL to MessageSerializerEntry(9, { serializeUserLeftChannel(it as UserLeftChannel) }, { deserializeUserLeftChannel(it) }),
        MessageType.USER_SPEAKING to MessageSerializerEntry(10, { serializeUserSpeaking(it as UserSpeaking) }, { deserializeUserSpeaking(it) }),
        MessageType.PTT_TOGGLE to MessageSerializerEntry(11, { serializePttToggle(it as PttToggle) }, { deserializePttToggle(it) }),
        MessageType.USER_MUTE_TOGGLE to MessageSerializerEntry(12, { serializeUserMuteToggle(it as UserMuteToggle) }, { deserializeUserMuteToggle(it) }),
        MessageType.SERVER_MESSAGE to MessageSerializerEntry(13, { serializeServerMessage(it as ServerMessage) }, { deserializeServerMessage(it) }),
        MessageType.STUN_BIND_REQUEST to MessageSerializerEntry(14, { serializeStunBindRequest(it as StunBindRequest) }, { deserializeStunBindRequest(it) }),
        MessageType.STUN_BIND_RESPONSE to MessageSerializerEntry(15, { serializeStunBindResponse(it as StunBindResponse) }, { deserializeStunBindResponse(it) }),
        MessageType.UDP_PING_REQUEST to MessageSerializerEntry(16, { serializeUdpPingRequest(it as UdpPingRequest) }, { deserializeUdpPingRequest(it) }),
        MessageType.UDP_PING_RESPONSE to MessageSerializerEntry(17, { serializeUdpPingResponse(it as UdpPingResponse) }, { deserializeUdpPingResponse(it) }),
        MessageType.KEY_ROTATION_REQUEST to MessageSerializerEntry(18, { serializeKeyRotationRequest(it as KeyRotationRequest) }, { deserializeKeyRotationRequest(it) }),
        MessageType.KEY_ROTATION_RESPONSE to MessageSerializerEntry(19, { serializeKeyRotationResponse(it as KeyRotationResponse) }, { deserializeKeyRotationResponse(it) }),
        MessageType.ADMIN_AUTH_REQUEST to MessageSerializerEntry(20, { serializeAdminAuthRequest(it as AdminAuthRequest) }, { deserializeAdminAuthRequest(it) }),
        MessageType.ADMIN_AUTH_RESPONSE to MessageSerializerEntry(21, { serializeAdminAuthResponse(it as AdminAuthResponse) }, { deserializeAdminAuthResponse(it) }),
        MessageType.SET_ADMIN_REQUEST to MessageSerializerEntry(22, { serializeSetAdminRequest(it as SetAdminRequest) }, { deserializeSetAdminRequest(it) }),
        MessageType.SET_ADMIN_RESPONSE to MessageSerializerEntry(23, { serializeSetAdminResponse(it as SetAdminResponse) }, { deserializeSetAdminResponse(it) }),
        MessageType.KICK_USER_REQUEST to MessageSerializerEntry(24, { serializeKickUserRequest(it as KickUserRequest) }, { deserializeKickUserRequest(it) }),
        MessageType.KICK_USER_RESPONSE to MessageSerializerEntry(25, { serializeKickUserResponse(it as KickUserResponse) }, { deserializeKickUserResponse(it) }),
        MessageType.BAN_USER_REQUEST to MessageSerializerEntry(26, { serializeBanUserRequest(it as BanUserRequest) }, { deserializeBanUserRequest(it) }),
        MessageType.BAN_USER_RESPONSE to MessageSerializerEntry(27, { serializeBanUserResponse(it as BanUserResponse) }, { deserializeBanUserResponse(it) }),
        MessageType.MOVE_USER_REQUEST to MessageSerializerEntry(28, { serializeMoveUserRequest(it as MoveUserRequest) }, { deserializeMoveUserRequest(it) }),
        MessageType.MOVE_USER_RESPONSE to MessageSerializerEntry(29, { serializeMoveUserResponse(it as MoveUserResponse) }, { deserializeMoveUserResponse(it) }),
        MessageType.CHAT_SEND_REQUEST to MessageSerializerEntry(30, { serializeChatSendRequest(it as ChatSendRequest) }, { deserializeChatSendRequest(it) }),
        MessageType.CHAT_BROADCAST to MessageSerializerEntry(31, { serializeChatBroadcast(it as ChatBroadcast) }, { deserializeChatBroadcast(it) }),
        MessageType.SET_SERVER_NAME_REQUEST to MessageSerializerEntry(32, { serializeSetServerNameRequest(it as SetServerNameRequest) }, { deserializeSetServerNameRequest(it) }),
        MessageType.SET_SERVER_NAME_RESPONSE to MessageSerializerEntry(33, { serializeSetServerNameResponse(it as SetServerNameResponse) }, { deserializeSetServerNameResponse(it) }),
        MessageType.RENAME_CHANNEL_REQUEST to MessageSerializerEntry(34, { serializeRenameChannelRequest(it as RenameChannelRequest) }, { deserializeRenameChannelRequest(it) }),
        MessageType.RENAME_CHANNEL_RESPONSE to MessageSerializerEntry(35, { serializeRenameChannelResponse(it as RenameChannelResponse) }, { deserializeRenameChannelResponse(it) }),
        MessageType.SPEAKING_STATE to MessageSerializerEntry(36, { serializeSpeakingState(it as SpeakingState) }, { deserializeSpeakingState(it) }),
        MessageType.FILE_LIST_REQUEST to MessageSerializerEntry(40, { serializeFileListRequest(it as FileListRequest) }, { deserializeFileListRequest(it) }),
        MessageType.FILE_LIST_RESPONSE to MessageSerializerEntry(41, { serializeFileListResponse(it as FileListResponse) }, { deserializeFileListResponse(it) }),
        MessageType.FILE_UPLOAD_REQUEST to MessageSerializerEntry(42, { serializeFileUploadRequest(it as FileUploadRequest) }, { deserializeFileUploadRequest(it) }),
        MessageType.FILE_UPLOAD_RESPONSE to MessageSerializerEntry(43, { serializeFileUploadResponse(it as FileUploadResponse) }, { deserializeFileUploadResponse(it) }),
        MessageType.FILE_DOWNLOAD_REQUEST to MessageSerializerEntry(46, { serializeFileDownloadRequest(it as FileDownloadRequest) }, { deserializeFileDownloadRequest(it) }),
        MessageType.FILE_DOWNLOAD_RESPONSE to MessageSerializerEntry(47, { serializeFileDownloadResponse(it as FileDownloadResponse) }, { deserializeFileDownloadResponse(it) }),
        MessageType.FILE_DELETE_REQUEST to MessageSerializerEntry(49, { serializeFileDeleteRequest(it as FileDeleteRequest) }, { deserializeFileDeleteRequest(it) }),
        MessageType.FILE_DELETE_RESPONSE to MessageSerializerEntry(50, { serializeFileDeleteResponse(it as FileDeleteResponse) }, { deserializeFileDeleteResponse(it) })
    )

    val CASE_TO_DESERIALIZER: Map<Int, CaseDeserializerEntry> = mapOf(
        1 to CaseDeserializerEntry(MessageType.LOGIN_REQUEST, { deserializeLoginRequest(it) }),
        2 to CaseDeserializerEntry(MessageType.LOGIN_RESPONSE, { deserializeLoginResponse(it) }),
        3 to CaseDeserializerEntry(MessageType.JOIN_CHANNEL_REQUEST, { deserializeJoinChannelRequest(it) }),
        4 to CaseDeserializerEntry(MessageType.LEAVE_CHANNEL_REQUEST, { deserializeLeaveChannelRequest(it) }),
        5 to CaseDeserializerEntry(MessageType.CREATE_CHANNEL_REQUEST, { deserializeCreateChannelRequest(it) }),
        6 to CaseDeserializerEntry(MessageType.DELETE_CHANNEL_REQUEST, { deserializeDeleteChannelRequest(it) }),
        7 to CaseDeserializerEntry(MessageType.CHANNEL_LIST_UPDATE, { deserializeChannelListUpdate(it) }),
        8 to CaseDeserializerEntry(MessageType.USER_JOINED_CHANNEL, { deserializeUserJoinedChannel(it) }),
        9 to CaseDeserializerEntry(MessageType.USER_LEFT_CHANNEL, { deserializeUserLeftChannel(it) }),
        10 to CaseDeserializerEntry(MessageType.USER_SPEAKING, { deserializeUserSpeaking(it) }),
        11 to CaseDeserializerEntry(MessageType.PTT_TOGGLE, { deserializePttToggle(it) }),
        12 to CaseDeserializerEntry(MessageType.USER_MUTE_TOGGLE, { deserializeUserMuteToggle(it) }),
        13 to CaseDeserializerEntry(MessageType.SERVER_MESSAGE, { deserializeServerMessage(it) }),
        14 to CaseDeserializerEntry(MessageType.STUN_BIND_REQUEST, { deserializeStunBindRequest(it) }),
        15 to CaseDeserializerEntry(MessageType.STUN_BIND_RESPONSE, { deserializeStunBindResponse(it) }),
        16 to CaseDeserializerEntry(MessageType.UDP_PING_REQUEST, { deserializeUdpPingRequest(it) }),
        17 to CaseDeserializerEntry(MessageType.UDP_PING_RESPONSE, { deserializeUdpPingResponse(it) }),
        18 to CaseDeserializerEntry(MessageType.KEY_ROTATION_REQUEST, { deserializeKeyRotationRequest(it) }),
        19 to CaseDeserializerEntry(MessageType.KEY_ROTATION_RESPONSE, { deserializeKeyRotationResponse(it) }),
        20 to CaseDeserializerEntry(MessageType.ADMIN_AUTH_REQUEST, { deserializeAdminAuthRequest(it) }),
        21 to CaseDeserializerEntry(MessageType.ADMIN_AUTH_RESPONSE, { deserializeAdminAuthResponse(it) }),
        22 to CaseDeserializerEntry(MessageType.SET_ADMIN_REQUEST, { deserializeSetAdminRequest(it) }),
        23 to CaseDeserializerEntry(MessageType.SET_ADMIN_RESPONSE, { deserializeSetAdminResponse(it) }),
        24 to CaseDeserializerEntry(MessageType.KICK_USER_REQUEST, { deserializeKickUserRequest(it) }),
        25 to CaseDeserializerEntry(MessageType.KICK_USER_RESPONSE, { deserializeKickUserResponse(it) }),
        26 to CaseDeserializerEntry(MessageType.BAN_USER_REQUEST, { deserializeBanUserRequest(it) }),
        27 to CaseDeserializerEntry(MessageType.BAN_USER_RESPONSE, { deserializeBanUserResponse(it) }),
        28 to CaseDeserializerEntry(MessageType.MOVE_USER_REQUEST, { deserializeMoveUserRequest(it) }),
        29 to CaseDeserializerEntry(MessageType.MOVE_USER_RESPONSE, { deserializeMoveUserResponse(it) }),
        30 to CaseDeserializerEntry(MessageType.CHAT_SEND_REQUEST, { deserializeChatSendRequest(it) }),
        31 to CaseDeserializerEntry(MessageType.CHAT_BROADCAST, { deserializeChatBroadcast(it) }),
        32 to CaseDeserializerEntry(MessageType.SET_SERVER_NAME_REQUEST, { deserializeSetServerNameRequest(it) }),
        33 to CaseDeserializerEntry(MessageType.SET_SERVER_NAME_RESPONSE, { deserializeSetServerNameResponse(it) }),
        34 to CaseDeserializerEntry(MessageType.RENAME_CHANNEL_REQUEST, { deserializeRenameChannelRequest(it) }),
        35 to CaseDeserializerEntry(MessageType.RENAME_CHANNEL_RESPONSE, { deserializeRenameChannelResponse(it) }),
        36 to CaseDeserializerEntry(MessageType.SPEAKING_STATE, { deserializeSpeakingState(it) }),
        40 to CaseDeserializerEntry(MessageType.FILE_LIST_REQUEST, { deserializeFileListRequest(it) }),
        41 to CaseDeserializerEntry(MessageType.FILE_LIST_RESPONSE, { deserializeFileListResponse(it) }),
        42 to CaseDeserializerEntry(MessageType.FILE_UPLOAD_REQUEST, { deserializeFileUploadRequest(it) }),
        43 to CaseDeserializerEntry(MessageType.FILE_UPLOAD_RESPONSE, { deserializeFileUploadResponse(it) }),
        46 to CaseDeserializerEntry(MessageType.FILE_DOWNLOAD_REQUEST, { deserializeFileDownloadRequest(it) }),
        47 to CaseDeserializerEntry(MessageType.FILE_DOWNLOAD_RESPONSE, { deserializeFileDownloadResponse(it) }),
        49 to CaseDeserializerEntry(MessageType.FILE_DELETE_REQUEST, { deserializeFileDeleteRequest(it) }),
        50 to CaseDeserializerEntry(MessageType.FILE_DELETE_RESPONSE, { deserializeFileDeleteResponse(it) })
    )

    fun serializeControlMessage(msgType: MessageType, msg: Any): ByteArray {
        val entry = MESSAGE_TYPE_MAP[msgType]
            ?: throw IllegalArgumentException("Unknown message type: $msgType")
        val payload = entry.serializer(msg)
        val buf = NevoBuffer()
        buf.writeU32(entry.caseValue)
        buf.writeU32(payload.size)
        buf.writeBytesRaw(payload)
        return buf.getBytes()
    }

    fun deserializeControlMessage(data: ByteArray): Pair<MessageType?, Any?> {
        val buf = NevoBuffer(data)
        val caseValue = buf.readU32()
        val payloadSize = buf.readU32()
        if (buf.remaining() < payloadSize) {
            return Pair(null, null)
        }
        val payloadData = buf.readRaw(payloadSize)
        val entry = CASE_TO_DESERIALIZER[caseValue] ?: return Pair(null, null)
        val msg = entry.deserializer(payloadData)
        return Pair(entry.messageType, msg)
    }
}