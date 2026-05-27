package com.nevo.voip.core.model

data class UserInfo(
    val id: Long = 0,
    val username: String = "",
    val status: Int = 0,
    val muted: Boolean = false,
    val deafened: Boolean = false,
    val groupId: Int = 0
)

data class ChannelInfo(
    val id: Long = 0,
    val name: String = "",
    val parentId: Long = 0,
    val children: List<ChannelInfo> = emptyList(),
    val users: List<UserInfo> = emptyList()
)

data class LoginRequest(
    val username: String = "",
    val authCredential: ByteArray = ByteArray(0),
    val keyExchangeMethods: List<String> = emptyList(),
    val clientPublicKey: ByteArray = ByteArray(0),
    val clientUdpPort: Int = 0,
    val clientVideoUdpPort: Int = 0
)

data class LoginResponse(
    val result: Int = 0,
    val userInfo: UserInfo? = null,
    val sessionToken: String = "",
    val serverPublicKey: ByteArray = ByteArray(0),
    val keyExchangeMethod: String = "",
    val encryptedSessionKey: ByteArray = ByteArray(0),
    val ownerExists: Int = 0,
    val serverUdpPort: Int = 0,
    val serverVideoUdpPort: Int = 0
)

data class JoinChannelRequest(
    val channelId: Long = 0
)

class LeaveChannelRequest

data class CreateChannelRequest(
    val parentId: Long = 0,
    val name: String = ""
)

data class DeleteChannelRequest(
    val channelId: Long = 0
)

data class RenameChannelRequest(
    val channelId: Long = 0,
    val newName: String = ""
)

data class RenameChannelResponse(
    val channelId: Long = 0,
    val newName: String = ""
)

data class ChannelListUpdate(
    val channels: List<ChannelInfo> = emptyList()
)

data class UserJoinedChannel(
    val user: UserInfo? = null,
    val channelId: Long = 0
)

data class UserLeftChannel(
    val userId: Long = 0,
    val channelId: Long = 0
)

data class UserSpeaking(
    val userId: Long = 0,
    val speaking: Boolean = false
)

data class PttToggle(
    val active: Boolean = false
)

data class UserMuteToggle(
    val muted: Boolean = false
)

data class SpeakingState(
    val speaking: Boolean = false
)

data class ServerMessage(
    val text: String = ""
)

data class StunBindRequest(
    val transactionId: Int = 0
)

data class StunBindResponse(
    val transactionId: Int = 0,
    val mappedAddress: ByteArray = ByteArray(0),
    val natType: Int = 0
)

data class UdpPingRequest(
    val sequence: Int = 0,
    val clientUdpKey: ByteArray = ByteArray(0)
)

data class UdpPingResponse(
    val sequence: Int = 0,
    val udpReachable: Boolean = false
)

data class KeyRotationRequest(
    val newServerPublicKey: ByteArray = ByteArray(0),
    val keyEpoch: Long = 0,
    val encryptedSessionKey: ByteArray = ByteArray(0)
)

data class KeyRotationResponse(
    val newClientPublicKey: ByteArray = ByteArray(0),
    val keyEpoch: Long = 0
)

data class AdminAuthRequest(
    val password: String = ""
)

data class AdminAuthResponse(
    val result: Int = 0,
    val message: String = ""
)

data class SetAdminRequest(
    val userId: Long = 0,
    val setAdmin: Boolean = false
)

data class SetAdminResponse(
    val result: Int = 0,
    val message: String = ""
)

data class SetServerNameRequest(
    val serverName: String = ""
)

data class SetServerNameResponse(
    val result: Int = 0,
    val message: String = ""
)

data class KickUserRequest(
    val userId: Long = 0,
    val reason: String = ""
)

data class KickUserResponse(
    val result: Int = 0,
    val message: String = ""
)

data class BanUserRequest(
    val userId: Long = 0,
    val reason: String = "",
    val expiresAt: Long = 0
)

data class BanUserResponse(
    val result: Int = 0,
    val message: String = ""
)

data class MoveUserRequest(
    val userId: Long = 0,
    val channelId: Long = 0
)

data class MoveUserResponse(
    val result: Int = 0,
    val message: String = ""
)

data class ChatSendRequest(
    val channelId: Long = 0,
    val text: String = ""
)

data class ChatBroadcast(
    val senderId: Long = 0,
    val senderName: String = "",
    val channelId: Long = 0,
    val text: String = "",
    val timestamp: Long = 0
)

data class FileEntry(
    val id: Long = 0,
    val channelId: Long = 0,
    val uploaderId: Long = 0,
    val filename: String = "",
    val fileSize: Long = 0,
    val uploadTime: Long = 0
)

data class FileListRequest(
    val channelId: Long = 0
)

data class FileListResponse(
    val entries: List<FileEntry> = emptyList()
)

data class FileUploadRequest(
    val channelId: Long = 0,
    val filename: String = "",
    val fileSize: Long = 0
)

data class FileUploadResponse(
    val result: Int = 0,
    val message: String = "",
    val fileId: Long = 0
)

data class FileDownloadRequest(
    val fileId: Long = 0
)

data class FileDownloadResponse(
    val result: Int = 0,
    val message: String = ""
)

data class FileDeleteRequest(
    val fileId: Long = 0
)

data class FileDeleteResponse(
    val result: Int = 0,
    val message: String = ""
)