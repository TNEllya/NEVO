import struct
from dataclasses import dataclass, field
from enum import IntEnum
from typing import List, Optional, Tuple


class ResultCode(IntEnum):
    OK = 0
    ERROR_UNKNOWN = 1
    ERROR_AUTH_FAILED = 2
    ERROR_PERMISSION_DENIED = 3
    ERROR_CHANNEL_NOT_FOUND = 4
    ERROR_ALREADY_IN_CHANNEL = 5
    ERROR_INVALID_REQUEST = 6
    ERROR_CONNECTION_FAILED = 7
    ERROR_TIMEOUT = 8


class PbUserStatus(IntEnum):
    OFFLINE = 0
    ONLINE = 1
    AWAY = 2
    MUTED = 3
    DEAFENED = 4


class MessageType(IntEnum):
    LOGIN_REQUEST = 1
    LOGIN_RESPONSE = 2
    JOIN_CHANNEL_REQUEST = 3
    LEAVE_CHANNEL_REQUEST = 4
    CREATE_CHANNEL_REQUEST = 5
    DELETE_CHANNEL_REQUEST = 6
    CHANNEL_LIST_UPDATE = 7
    USER_JOINED_CHANNEL = 8
    USER_LEFT_CHANNEL = 9
    USER_SPEAKING = 10
    PTT_TOGGLE = 11
    USER_MUTE_TOGGLE = 12
    SERVER_MESSAGE = 13
    STUN_BIND_REQUEST = 14
    STUN_BIND_RESPONSE = 15
    UDP_PING_REQUEST = 16
    UDP_PING_RESPONSE = 17
    KEY_ROTATION_REQUEST = 18
    KEY_ROTATION_RESPONSE = 19
    ADMIN_AUTH_REQUEST = 20
    ADMIN_AUTH_RESPONSE = 21
    SET_ADMIN_REQUEST = 22
    SET_ADMIN_RESPONSE = 23
    KICK_USER_REQUEST = 24
    KICK_USER_RESPONSE = 25
    BAN_USER_REQUEST = 26
    BAN_USER_RESPONSE = 27
    MOVE_USER_REQUEST = 28
    MOVE_USER_RESPONSE = 29
    CHAT_SEND_REQUEST = 30
    CHAT_BROADCAST = 31
    SET_SERVER_NAME_REQUEST = 32
    SET_SERVER_NAME_RESPONSE = 33
    RENAME_CHANNEL_REQUEST = 34
    RENAME_CHANNEL_RESPONSE = 35
    SPEAKING_STATE = 36
    FILE_LIST_REQUEST = 40
    FILE_LIST_RESPONSE = 41
    FILE_UPLOAD_REQUEST = 42
    FILE_UPLOAD_RESPONSE = 43
    # 44/45 在 proto/control.proto 与 C++ PacketTypes.h 中均未占用：
    # 作为"追加编号"承载文件字节分片（当前服务端 CASE_DECODERS 尚未实现，
    # 客户端实时传输走聊天中继分片标记，见文件下方 FileChunkMarker 注释）。
    FILE_UPLOAD_CHUNK_REQUEST = 44
    FILE_UPLOAD_CHUNK_ACK = 45
    FILE_DOWNLOAD_REQUEST = 46
    FILE_DOWNLOAD_RESPONSE = 47
    FILE_DELETE_REQUEST = 49
    FILE_DELETE_RESPONSE = 50
    SCREEN_SHARE_START = 60
    SCREEN_SHARE_STOP = 61
    SCREEN_SHARE_STATE = 62

    # 视频通话控制消息（与 C++ PacketTypes.h 对齐）
    VIDEO_CALL_REQUEST = 80
    VIDEO_CALL_RESPONSE = 81
    VIDEO_CALL_HANGUP = 82
    VIDEO_CALL_PROFILE_UPDATE = 83


@dataclass
class VideoProfile:
    """视频通话协商参数。"""
    codec: int = 0
    width: int = 640
    height: int = 480
    fps: int = 30
    target_bitrate_kbps: int = 1000


@dataclass
class VideoCallRequest:
    """发起一对一视频通话请求。"""
    caller_id: int = 0
    callee_id: int = 0
    call_id: int = 0
    requested_profile: Optional[VideoProfile] = None


@dataclass
class VideoCallResponse:
    """视频通话请求响应。"""
    call_id: int = 0
    accepted: bool = False
    negotiated_profile: Optional[VideoProfile] = None
    reason: str = ""


@dataclass
class VideoCallHangup:
    """挂断视频通话。"""
    call_id: int = 0
    reason: int = 0


@dataclass
class VideoCallProfileUpdate:
    """视频通话过程中更新码率/分辨率。"""
    call_id: int = 0
    new_profile: Optional[VideoProfile] = None


@dataclass
class UserInfo:
    id: int = 0
    username: str = ""
    status: int = 0
    muted: bool = False
    deafened: bool = False
    group_id: int = 0


@dataclass
class ChannelInfo:
    id: int = 0
    name: str = ""
    parent_id: int = 0
    children: List['ChannelInfo'] = field(default_factory=list)
    users: List[UserInfo] = field(default_factory=list)


@dataclass
class LoginRequest:
    username: str = ""
    auth_credential: bytes = b""
    key_exchange_methods: List[str] = field(default_factory=list)
    client_public_key: bytes = b""
    client_udp_port: int = 0
    client_video_udp_port: int = 0


@dataclass
class LoginResponse:
    result: int = 0
    user_info: Optional[UserInfo] = None
    session_token: str = ""
    server_public_key: bytes = b""
    key_exchange_method: str = ""
    encrypted_session_key: bytes = b""
    owner_exists: int = 0
    server_udp_port: int = 0
    server_video_udp_port: int = 0


@dataclass
class JoinChannelRequest:
    channel_id: int = 0


@dataclass
class LeaveChannelRequest:
    pass


@dataclass
class CreateChannelRequest:
    parent_id: int = 0
    name: str = ""


@dataclass
class DeleteChannelRequest:
    channel_id: int = 0


@dataclass
class RenameChannelRequest:
    channel_id: int = 0
    new_name: str = ""


@dataclass
class RenameChannelResponse:
    result: int = 0
    message: str = ""


@dataclass
class ChannelListUpdate:
    channels: List[ChannelInfo] = field(default_factory=list)


@dataclass
class UserJoinedChannel:
    user: Optional[UserInfo] = None
    channel_id: int = 0


@dataclass
class UserLeftChannel:
    user_id: int = 0
    channel_id: int = 0


@dataclass
class UserSpeaking:
    user_id: int = 0
    speaking: bool = False


@dataclass
class PttToggle:
    active: bool = False


@dataclass
class UserMuteToggle:
    muted: bool = False


@dataclass
class SpeakingState:
    speaking: bool = False


@dataclass
class ServerMessage:
    text: str = ""


@dataclass
class StunBindRequest:
    transaction_id: int = 0


@dataclass
class StunBindResponse:
    transaction_id: int = 0
    mapped_address: bytes = b""
    nat_type: int = 0


@dataclass
class UdpPingRequest:
    sequence: int = 0
    client_udp_key: bytes = b""


@dataclass
class UdpPingResponse:
    sequence: int = 0
    udp_reachable: bool = False


@dataclass
class KeyRotationRequest:
    new_server_public_key: bytes = b""
    key_epoch: int = 0
    encrypted_session_key: bytes = b""


@dataclass
class KeyRotationResponse:
    new_client_public_key: bytes = b""
    key_epoch: int = 0


@dataclass
class AdminAuthRequest:
    password: str = ""


@dataclass
class AdminAuthResponse:
    result: int = 0
    message: str = ""


@dataclass
class SetAdminRequest:
    user_id: int = 0
    set_admin: bool = False


@dataclass
class SetAdminResponse:
    result: int = 0
    message: str = ""


@dataclass
class SetServerNameRequest:
    server_name: str = ""


@dataclass
class SetServerNameResponse:
    result: int = 0
    message: str = ""


@dataclass
class KickUserRequest:
    user_id: int = 0
    reason: str = ""


@dataclass
class KickUserResponse:
    result: int = 0
    message: str = ""


@dataclass
class BanUserRequest:
    user_id: int = 0
    reason: str = ""
    expires_at: int = 0


@dataclass
class BanUserResponse:
    result: int = 0
    message: str = ""


@dataclass
class MoveUserRequest:
    user_id: int = 0
    channel_id: int = 0


@dataclass
class MoveUserResponse:
    result: int = 0
    message: str = ""


@dataclass
class ChatSendRequest:
    channel_id: int = 0
    text: str = ""


@dataclass
class ChatBroadcast:
    sender_id: int = 0
    sender_name: str = ""
    channel_id: int = 0
    text: str = ""
    timestamp: int = 0


@dataclass
class FileEntry:
    id: int = 0
    channel_id: int = 0
    uploader_id: int = 0
    filename: str = ""
    file_size: int = 0
    upload_time: int = 0


@dataclass
class FileListRequest:
    channel_id: int = 0


@dataclass
class FileListResponse:
    entries: List[FileEntry] = field(default_factory=list)


@dataclass
class FileUploadRequest:
    channel_id: int = 0
    filename: str = ""
    file_size: int = 0


@dataclass
class FileUploadResponse:
    result: int = 0
    message: str = ""
    file_id: int = 0


@dataclass
class FileDeleteRequest:
    file_id: int = 0


@dataclass
class FileDeleteResponse:
    result: int = 0
    message: str = ""


# ============================================================
# 文件传输数据通道（分片）— 追加编号，不修改任何既有字段
#
# 背景：FileUploadRequest 只有元数据（channel_id/filename/file_size），
# 服务端 handleFileUploadRequest 只写 DB 记录、不接收字节流；proto 中也
# 没有 FileDownloadRequest/FileDownloadResponse 消息体（MessageType 枚举
# 已预留 46/47）。为满足"真实数据通道"，客户端按如下两层实现：
#
# 1) Wire 分片消息（44/45/46/47，编号为纯追加）：
#    - FileUploadChunkRequest/FileUploadChunkAck：上传字节分片与其确认。
#    - FileDownloadRequest/FileDownloadResponse：下载请求；响应按分片逐条
#      返回（chunk_index/chunk_count），客户端重组。
#    注意：当前服务端 decodeCustomWirePayload 的 CASE_DECODERS 未注册
#    44/45/46/47，这些消息会被服务端丢弃（仅日志告警）。服务端补齐
#    (proto message + CASE_DECODERS + handleFileDownloadRequest) 后，
#    客户端无需再改即可走服务端存储通道。
#
# 2) 聊天中继分片标记（基于现有 ChatSend/ChatBroadcast，服务端已支持）：
#    客户端实时字节流通过聊天文本承载（base64），由同频道所有客户端
#    重组写盘。这是当前可用的真实数据通道。
# ============================================================


@dataclass
class FileUploadChunkRequest:
    file_id: int = 0
    chunk_index: int = 0      # 0-based
    chunk_count: int = 0
    data: bytes = b""


@dataclass
class FileUploadChunkAck:
    file_id: int = 0
    chunk_index: int = 0
    result: int = 0


@dataclass
class FileDownloadRequest:
    file_id: int = 0


@dataclass
class FileDownloadResponse:
    result: int = 0
    message: str = ""
    file_id: int = 0
    filename: str = ""
    file_size: int = 0
    chunk_index: int = 0      # 0-based；单条响应携带一个分片
    chunk_count: int = 0
    data: bytes = b""


@dataclass
class ScreenShareStartRequest:
    channel_id: int = 0
    source_type: int = 0
    source_name: str = ""
    width: int = 0
    height: int = 0
    fps: int = 0
    share_audio: bool = False
    audio_source: int = 0
    audio_app_pid: int = 0


@dataclass
class ScreenShareStopRequest:
    channel_id: int = 0


@dataclass
class ScreenShareState:
    user_id: int = 0
    sharing: bool = False
    source_type: int = 0
    source_name: str = ""
    width: int = 0
    height: int = 0
    has_audio: bool = False


def write_u16(buf: bytearray, value: int):
    buf.extend(struct.pack('<H', value))


def write_u32(buf: bytearray, value: int):
    buf.extend(struct.pack('<I', value))


def write_u64(buf: bytearray, value: int):
    buf.extend(struct.pack('<Q', value))


def write_bool(buf: bytearray, value: bool):
    buf.extend(struct.pack('<?', value))


def write_string(buf: bytearray, value: str):
    encoded = value.encode('utf-8')
    buf.extend(struct.pack('<I', len(encoded)))
    buf.extend(encoded)


def write_bytes(buf: bytearray, value: bytes):
    buf.extend(struct.pack('<I', len(value)))
    buf.extend(value)


def read_u16(data: bytes, offset: int) -> Tuple[int, int]:
    value = struct.unpack_from('<H', data, offset)[0]
    return value, offset + 2


def read_u32(data: bytes, offset: int) -> Tuple[int, int]:
    value = struct.unpack_from('<I', data, offset)[0]
    return value, offset + 4


def read_u64(data: bytes, offset: int) -> Tuple[int, int]:
    value = struct.unpack_from('<Q', data, offset)[0]
    return value, offset + 8


def read_bool(data: bytes, offset: int) -> Tuple[bool, int]:
    value = struct.unpack_from('<?', data, offset)[0]
    return value, offset + 1


def read_string(data: bytes, offset: int) -> Tuple[str, int]:
    length, offset = read_u32(data, offset)
    value = data[offset:offset + length].decode('utf-8')
    return value, offset + length


def read_bytes(data: bytes, offset: int) -> Tuple[bytes, int]:
    length, offset = read_u32(data, offset)
    value = data[offset:offset + length]
    return value, offset + length


class Buffer:
    def __init__(self, data: Optional[bytes] = None):
        if data is None:
            self._write_buf = bytearray()
            self._read_buf = b""
            self._read_offset = 0
        else:
            self._write_buf = None
            self._read_buf = bytes(data)
            self._read_offset = 0

    def write_u16(self, value: int):
        self._write_buf.extend(struct.pack('<H', value))

    def write_u32(self, value: int):
        self._write_buf.extend(struct.pack('<I', value))

    def write_u64(self, value: int):
        self._write_buf.extend(struct.pack('<Q', value))

    def write_bool(self, value: bool):
        self._write_buf.extend(struct.pack('<?', value))

    def write_string(self, value: str):
        encoded = value.encode('utf-8')
        self._write_buf.extend(struct.pack('<I', len(encoded)))
        self._write_buf.extend(encoded)

    def write_bytes(self, value: bytes):
        self._write_buf.extend(struct.pack('<I', len(value)))
        self._write_buf.extend(value)

    def read_u16(self) -> int:
        value = struct.unpack_from('<H', self._read_buf, self._read_offset)[0]
        self._read_offset += 2
        return value

    def read_u32(self) -> int:
        value = struct.unpack_from('<I', self._read_buf, self._read_offset)[0]
        self._read_offset += 4
        return value

    def read_u64(self) -> int:
        value = struct.unpack_from('<Q', self._read_buf, self._read_offset)[0]
        self._read_offset += 8
        return value

    def read_bool(self) -> bool:
        value = struct.unpack_from('<?', self._read_buf, self._read_offset)[0]
        self._read_offset += 1
        return value

    def read_string(self) -> str:
        length = self.read_u32()
        value = self._read_buf[self._read_offset:self._read_offset + length].decode('utf-8')
        self._read_offset += length
        return value

    def read_bytes(self) -> bytes:
        length = self.read_u32()
        value = self._read_buf[self._read_offset:self._read_offset + length]
        self._read_offset += length
        return value

    def read_raw(self, n: int) -> bytes:
        if self._read_offset + n > len(self._read_buf):
            raise ValueError(f"read_raw: requested {n} bytes but only {len(self._read_buf) - self._read_offset} bytes remaining")
        value = self._read_buf[self._read_offset:self._read_offset + n]
        self._read_offset += n
        return value

    def get_bytes(self) -> bytes:
        return bytes(self._write_buf)


def serialize_user_info(msg: UserInfo) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.id)
    write_string(buf, msg.username)
    write_u32(buf, msg.status)
    write_bool(buf, msg.muted)
    write_bool(buf, msg.deafened)
    write_u32(buf, msg.group_id)
    return bytes(buf)


def deserialize_user_info(data: bytes) -> UserInfo:
    buf = Buffer(data)
    return UserInfo(
        id=buf.read_u64(),
        username=buf.read_string(),
        status=buf.read_u32(),
        muted=buf.read_bool(),
        deafened=buf.read_bool(),
        group_id=buf.read_u32(),
    )


def serialize_channel_info(msg: ChannelInfo) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.id)
    write_string(buf, msg.name)
    write_u64(buf, msg.parent_id)
    write_u32(buf, len(msg.children))
    for child in msg.children:
        child_data = serialize_channel_info(child)
        write_u32(buf, len(child_data))
        buf.extend(child_data)
    write_u32(buf, len(msg.users))
    for user in msg.users:
        user_data = serialize_user_info(user)
        write_u32(buf, len(user_data))
        buf.extend(user_data)
    return bytes(buf)


def deserialize_channel_info(data: bytes) -> ChannelInfo:
    buf = Buffer(data)
    _id = buf.read_u64()
    _name = buf.read_string()
    _parent_id = buf.read_u64()
    children_count = buf.read_u32()
    children = []
    for _ in range(children_count):
        size = buf.read_u32()
        child_data = buf.read_raw(size)
        children.append(deserialize_channel_info(child_data))
    users_count = buf.read_u32()
    users = []
    for _ in range(users_count):
        size = buf.read_u32()
        user_data = buf.read_raw(size)
        users.append(deserialize_user_info(user_data))
    return ChannelInfo(
        id=_id,
        name=_name,
        parent_id=_parent_id,
        children=children,
        users=users,
    )


def serialize_login_request(msg: LoginRequest) -> bytes:
    buf = bytearray()
    write_string(buf, msg.username)
    write_bytes(buf, msg.auth_credential)
    write_u32(buf, len(msg.key_exchange_methods))
    for method in msg.key_exchange_methods:
        write_string(buf, method)
    write_bytes(buf, msg.client_public_key)
    write_u16(buf, msg.client_udp_port)
    if hasattr(msg, 'client_video_udp_port') and msg.client_video_udp_port:
        write_u16(buf, msg.client_video_udp_port)
    return bytes(buf)


def deserialize_login_request(data: bytes) -> LoginRequest:
    buf = Buffer(data)
    username = buf.read_string()
    auth_credential = buf.read_bytes()
    count = buf.read_u32()
    key_exchange_methods = []
    for _ in range(count):
        key_exchange_methods.append(buf.read_string())
    client_public_key = buf.read_bytes()
    try:
        client_udp_port = buf.read_u16()
    except Exception:
        client_udp_port = 0
    client_video_udp_port = 0
    try:
        client_video_udp_port = buf.read_u16()
    except Exception:
        client_video_udp_port = 0
    return LoginRequest(
        username=username,
        auth_credential=auth_credential,
        key_exchange_methods=key_exchange_methods,
        client_public_key=client_public_key,
        client_udp_port=client_udp_port,
        client_video_udp_port=client_video_udp_port,
    )


def serialize_login_response(msg: LoginResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    if msg.user_info is not None:
        user_data = serialize_user_info(msg.user_info)
        write_u32(buf, len(user_data))
        buf.extend(user_data)
    else:
        write_u32(buf, 0)
    write_string(buf, msg.session_token)
    write_bytes(buf, msg.server_public_key)
    write_string(buf, msg.key_exchange_method)
    write_bytes(buf, msg.encrypted_session_key)
    write_u32(buf, msg.owner_exists)
    write_u16(buf, msg.server_udp_port)
    if hasattr(msg, 'server_video_udp_port') and msg.server_video_udp_port:
        write_u16(buf, msg.server_video_udp_port)
    return bytes(buf)


def deserialize_login_response(data: bytes) -> LoginResponse:
    buf = Buffer(data)
    result = buf.read_u32()
    user_size = buf.read_u32()
    user_info = None
    if user_size > 0:
        user_data = buf.read_raw(user_size)
        user_info = deserialize_user_info(user_data)
    session_token = buf.read_string()
    server_public_key = buf.read_bytes()
    key_exchange_method = buf.read_string()
    encrypted_session_key = buf.read_bytes()
    owner_exists = buf.read_u32()
    try:
        server_udp_port = buf.read_u16()
    except Exception:
        server_udp_port = 0
    server_video_udp_port = 0
    try:
        server_video_udp_port = buf.read_u16()
    except Exception:
        server_video_udp_port = 0
    return LoginResponse(
        result=result,
        user_info=user_info,
        session_token=session_token,
        server_public_key=server_public_key,
        key_exchange_method=key_exchange_method,
        encrypted_session_key=encrypted_session_key,
        owner_exists=owner_exists,
        server_udp_port=server_udp_port,
        server_video_udp_port=server_video_udp_port,
    )


def serialize_join_channel_request(msg: JoinChannelRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.channel_id)
    return bytes(buf)


def deserialize_join_channel_request(data: bytes) -> JoinChannelRequest:
    buf = Buffer(data)
    return JoinChannelRequest(channel_id=buf.read_u64())


def serialize_leave_channel_request(msg: LeaveChannelRequest) -> bytes:
    return b""


def deserialize_leave_channel_request(data: bytes) -> LeaveChannelRequest:
    return LeaveChannelRequest()


def serialize_create_channel_request(msg: CreateChannelRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.parent_id)
    write_string(buf, msg.name)
    return bytes(buf)


def deserialize_create_channel_request(data: bytes) -> CreateChannelRequest:
    buf = Buffer(data)
    return CreateChannelRequest(
        parent_id=buf.read_u64(),
        name=buf.read_string(),
    )


def serialize_delete_channel_request(msg: DeleteChannelRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.channel_id)
    return bytes(buf)


def deserialize_delete_channel_request(data: bytes) -> DeleteChannelRequest:
    buf = Buffer(data)
    return DeleteChannelRequest(channel_id=buf.read_u64())


def serialize_rename_channel_request(msg: RenameChannelRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.channel_id)
    write_string(buf, msg.new_name)
    return bytes(buf)


def deserialize_rename_channel_request(data: bytes) -> RenameChannelRequest:
    buf = Buffer(data)
    return RenameChannelRequest(
        channel_id=buf.read_u64(),
        new_name=buf.read_string(),
    )


def serialize_rename_channel_response(msg: RenameChannelResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    write_string(buf, msg.message)
    return bytes(buf)


def deserialize_rename_channel_response(data: bytes) -> RenameChannelResponse:
    buf = Buffer(data)
    return RenameChannelResponse(
        result=buf.read_u32(),
        message=buf.read_string(),
    )


def serialize_channel_list_update(msg: ChannelListUpdate) -> bytes:
    buf = bytearray()
    write_u32(buf, len(msg.channels))
    for channel in msg.channels:
        channel_data = serialize_channel_info(channel)
        write_u32(buf, len(channel_data))
        buf.extend(channel_data)
    return bytes(buf)


def deserialize_channel_list_update(data: bytes) -> ChannelListUpdate:
    buf = Buffer(data)
    count = buf.read_u32()
    channels = []
    for _ in range(count):
        size = buf.read_u32()
        channel_data = buf.read_raw(size)
        channels.append(deserialize_channel_info(channel_data))
    return ChannelListUpdate(channels=channels)


def serialize_user_joined_channel(msg: UserJoinedChannel) -> bytes:
    buf = bytearray()
    if msg.user is not None:
        user_data = serialize_user_info(msg.user)
        write_u32(buf, len(user_data))
        buf.extend(user_data)
    else:
        write_u32(buf, 0)
    write_u64(buf, msg.channel_id)
    return bytes(buf)


def deserialize_user_joined_channel(data: bytes) -> UserJoinedChannel:
    buf = Buffer(data)
    user_size = buf.read_u32()
    user = None
    if user_size > 0:
        user_data = buf.read_raw(user_size)
        user = deserialize_user_info(user_data)
    channel_id = buf.read_u64()
    return UserJoinedChannel(user=user, channel_id=channel_id)


def serialize_user_left_channel(msg: UserLeftChannel) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.user_id)
    write_u64(buf, msg.channel_id)
    return bytes(buf)


def deserialize_user_left_channel(data: bytes) -> UserLeftChannel:
    buf = Buffer(data)
    return UserLeftChannel(
        user_id=buf.read_u64(),
        channel_id=buf.read_u64(),
    )


def serialize_user_speaking(msg: UserSpeaking) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.user_id)
    write_bool(buf, msg.speaking)
    return bytes(buf)


def deserialize_user_speaking(data: bytes) -> UserSpeaking:
    buf = Buffer(data)
    return UserSpeaking(
        user_id=buf.read_u64(),
        speaking=buf.read_bool(),
    )


def serialize_ptt_toggle(msg: PttToggle) -> bytes:
    buf = bytearray()
    write_bool(buf, msg.active)
    return bytes(buf)


def deserialize_ptt_toggle(data: bytes) -> PttToggle:
    buf = Buffer(data)
    return PttToggle(active=buf.read_bool())


def serialize_user_mute_toggle(msg: UserMuteToggle) -> bytes:
    buf = bytearray()
    write_bool(buf, msg.muted)
    return bytes(buf)


def deserialize_user_mute_toggle(data: bytes) -> UserMuteToggle:
    buf = Buffer(data)
    return UserMuteToggle(muted=buf.read_bool())


def serialize_speaking_state(msg: SpeakingState) -> bytes:
    buf = bytearray()
    write_bool(buf, msg.speaking)
    return bytes(buf)


def deserialize_speaking_state(data: bytes) -> SpeakingState:
    buf = Buffer(data)
    return SpeakingState(speaking=buf.read_bool())


def serialize_server_message(msg: ServerMessage) -> bytes:
    buf = bytearray()
    write_string(buf, msg.text)
    return bytes(buf)


def deserialize_server_message(data: bytes) -> ServerMessage:
    buf = Buffer(data)
    return ServerMessage(text=buf.read_string())


def serialize_stun_bind_request(msg: StunBindRequest) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.transaction_id)
    return bytes(buf)


def deserialize_stun_bind_request(data: bytes) -> StunBindRequest:
    buf = Buffer(data)
    return StunBindRequest(transaction_id=buf.read_u32())


def serialize_stun_bind_response(msg: StunBindResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.transaction_id)
    write_bytes(buf, msg.mapped_address)
    write_u32(buf, msg.nat_type)
    return bytes(buf)


def deserialize_stun_bind_response(data: bytes) -> StunBindResponse:
    buf = Buffer(data)
    return StunBindResponse(
        transaction_id=buf.read_u32(),
        mapped_address=buf.read_bytes(),
        nat_type=buf.read_u32(),
    )


def serialize_udp_ping_request(msg: UdpPingRequest) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.sequence)
    write_bytes(buf, msg.client_udp_key)
    return bytes(buf)


def deserialize_udp_ping_request(data: bytes) -> UdpPingRequest:
    buf = Buffer(data)
    return UdpPingRequest(
        sequence=buf.read_u32(),
        client_udp_key=buf.read_bytes(),
    )


def serialize_udp_ping_response(msg: UdpPingResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.sequence)
    write_bool(buf, msg.udp_reachable)
    return bytes(buf)


def deserialize_udp_ping_response(data: bytes) -> UdpPingResponse:
    buf = Buffer(data)
    return UdpPingResponse(
        sequence=buf.read_u32(),
        udp_reachable=buf.read_bool(),
    )


def serialize_key_rotation_request(msg: KeyRotationRequest) -> bytes:
    buf = bytearray()
    write_bytes(buf, msg.new_server_public_key)
    write_u64(buf, msg.key_epoch)
    write_bytes(buf, msg.encrypted_session_key)
    return bytes(buf)


def deserialize_key_rotation_request(data: bytes) -> KeyRotationRequest:
    buf = Buffer(data)
    return KeyRotationRequest(
        new_server_public_key=buf.read_bytes(),
        key_epoch=buf.read_u64(),
        encrypted_session_key=buf.read_bytes(),
    )


def serialize_key_rotation_response(msg: KeyRotationResponse) -> bytes:
    buf = bytearray()
    write_bytes(buf, msg.new_client_public_key)
    write_u64(buf, msg.key_epoch)
    return bytes(buf)


def deserialize_key_rotation_response(data: bytes) -> KeyRotationResponse:
    buf = Buffer(data)
    return KeyRotationResponse(
        new_client_public_key=buf.read_bytes(),
        key_epoch=buf.read_u64(),
    )


def serialize_admin_auth_request(msg: AdminAuthRequest) -> bytes:
    buf = bytearray()
    write_string(buf, msg.password)
    return bytes(buf)


def deserialize_admin_auth_request(data: bytes) -> AdminAuthRequest:
    buf = Buffer(data)
    return AdminAuthRequest(password=buf.read_string())


def serialize_admin_auth_response(msg: AdminAuthResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    write_string(buf, msg.message)
    return bytes(buf)


def deserialize_admin_auth_response(data: bytes) -> AdminAuthResponse:
    buf = Buffer(data)
    return AdminAuthResponse(
        result=buf.read_u32(),
        message=buf.read_string(),
    )


def serialize_set_admin_request(msg: SetAdminRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.user_id)
    write_bool(buf, msg.set_admin)
    return bytes(buf)


def deserialize_set_admin_request(data: bytes) -> SetAdminRequest:
    buf = Buffer(data)
    return SetAdminRequest(
        user_id=buf.read_u64(),
        set_admin=buf.read_bool(),
    )


def serialize_set_admin_response(msg: SetAdminResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    write_string(buf, msg.message)
    return bytes(buf)


def deserialize_set_admin_response(data: bytes) -> SetAdminResponse:
    buf = Buffer(data)
    return SetAdminResponse(
        result=buf.read_u32(),
        message=buf.read_string(),
    )


def serialize_set_server_name_request(msg: SetServerNameRequest) -> bytes:
    buf = bytearray()
    write_string(buf, msg.server_name)
    return bytes(buf)


def deserialize_set_server_name_request(data: bytes) -> SetServerNameRequest:
    buf = Buffer(data)
    return SetServerNameRequest(server_name=buf.read_string())


def serialize_set_server_name_response(msg: SetServerNameResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    write_string(buf, msg.message)
    return bytes(buf)


def deserialize_set_server_name_response(data: bytes) -> SetServerNameResponse:
    buf = Buffer(data)
    return SetServerNameResponse(
        result=buf.read_u32(),
        message=buf.read_string(),
    )


def serialize_kick_user_request(msg: KickUserRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.user_id)
    write_string(buf, msg.reason)
    return bytes(buf)


def deserialize_kick_user_request(data: bytes) -> KickUserRequest:
    buf = Buffer(data)
    return KickUserRequest(
        user_id=buf.read_u64(),
        reason=buf.read_string(),
    )


def serialize_kick_user_response(msg: KickUserResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    write_string(buf, msg.message)
    return bytes(buf)


def deserialize_kick_user_response(data: bytes) -> KickUserResponse:
    buf = Buffer(data)
    return KickUserResponse(
        result=buf.read_u32(),
        message=buf.read_string(),
    )


def serialize_ban_user_request(msg: BanUserRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.user_id)
    write_string(buf, msg.reason)
    write_u64(buf, msg.expires_at)
    return bytes(buf)


def deserialize_ban_user_request(data: bytes) -> BanUserRequest:
    buf = Buffer(data)
    return BanUserRequest(
        user_id=buf.read_u64(),
        reason=buf.read_string(),
        expires_at=buf.read_u64(),
    )


def serialize_ban_user_response(msg: BanUserResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    write_string(buf, msg.message)
    return bytes(buf)


def deserialize_ban_user_response(data: bytes) -> BanUserResponse:
    buf = Buffer(data)
    return BanUserResponse(
        result=buf.read_u32(),
        message=buf.read_string(),
    )


def serialize_move_user_request(msg: MoveUserRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.user_id)
    write_u64(buf, msg.channel_id)
    return bytes(buf)


def deserialize_move_user_request(data: bytes) -> MoveUserRequest:
    buf = Buffer(data)
    return MoveUserRequest(
        user_id=buf.read_u64(),
        channel_id=buf.read_u64(),
    )


def serialize_move_user_response(msg: MoveUserResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    write_string(buf, msg.message)
    return bytes(buf)


def deserialize_move_user_response(data: bytes) -> MoveUserResponse:
    buf = Buffer(data)
    return MoveUserResponse(
        result=buf.read_u32(),
        message=buf.read_string(),
    )


def serialize_chat_send_request(msg: ChatSendRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.channel_id)
    write_string(buf, msg.text)
    return bytes(buf)


def deserialize_chat_send_request(data: bytes) -> ChatSendRequest:
    buf = Buffer(data)
    return ChatSendRequest(
        channel_id=buf.read_u64(),
        text=buf.read_string(),
    )


def serialize_chat_broadcast(msg: ChatBroadcast) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.sender_id)
    write_string(buf, msg.sender_name)
    write_u64(buf, msg.channel_id)
    write_string(buf, msg.text)
    write_u64(buf, msg.timestamp)
    return bytes(buf)


def deserialize_chat_broadcast(data: bytes) -> ChatBroadcast:
    buf = Buffer(data)
    return ChatBroadcast(
        sender_id=buf.read_u64(),
        sender_name=buf.read_string(),
        channel_id=buf.read_u64(),
        text=buf.read_string(),
        timestamp=buf.read_u64(),
    )


def serialize_file_entry(msg: FileEntry) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.id)
    write_u64(buf, msg.channel_id)
    write_u64(buf, msg.uploader_id)
    write_string(buf, msg.filename)
    write_u64(buf, msg.file_size)
    write_u64(buf, msg.upload_time)
    return bytes(buf)


def deserialize_file_entry(data: bytes) -> FileEntry:
    buf = Buffer(data)
    return FileEntry(
        id=buf.read_u64(),
        channel_id=buf.read_u64(),
        uploader_id=buf.read_u64(),
        filename=buf.read_string(),
        file_size=buf.read_u64(),
        upload_time=buf.read_u64(),
    )


def serialize_file_list_request(msg: FileListRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.channel_id)
    return bytes(buf)


def deserialize_file_list_request(data: bytes) -> FileListRequest:
    buf = Buffer(data)
    return FileListRequest(channel_id=buf.read_u64())


def serialize_file_list_response(msg: FileListResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, len(msg.entries))
    for entry in msg.entries:
        entry_data = serialize_file_entry(entry)
        buf.extend(entry_data)
    return bytes(buf)


def deserialize_file_list_response(data: bytes) -> FileListResponse:
    buf = Buffer(data)
    count = buf.read_u32()
    entries = []
    for _ in range(count):
        entry = FileEntry(
            id=buf.read_u64(),
            channel_id=buf.read_u64(),
            uploader_id=buf.read_u64(),
            filename=buf.read_string(),
            file_size=buf.read_u64(),
            upload_time=buf.read_u64(),
        )
        entries.append(entry)
    return FileListResponse(entries=entries)


def serialize_file_upload_request(msg: FileUploadRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.channel_id)
    write_string(buf, msg.filename)
    write_u64(buf, msg.file_size)
    return bytes(buf)


def deserialize_file_upload_request(data: bytes) -> FileUploadRequest:
    buf = Buffer(data)
    return FileUploadRequest(
        channel_id=buf.read_u64(),
        filename=buf.read_string(),
        file_size=buf.read_u64(),
    )


def serialize_file_upload_response(msg: FileUploadResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    write_string(buf, msg.message)
    write_u64(buf, msg.file_id)
    return bytes(buf)


def deserialize_file_upload_response(data: bytes) -> FileUploadResponse:
    buf = Buffer(data)
    return FileUploadResponse(
        result=buf.read_u32(),
        message=buf.read_string(),
        file_id=buf.read_u64(),
    )


def serialize_file_delete_request(msg: FileDeleteRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.file_id)
    return bytes(buf)


def deserialize_file_delete_request(data: bytes) -> FileDeleteRequest:
    buf = Buffer(data)
    return FileDeleteRequest(file_id=buf.read_u64())


def serialize_file_delete_response(msg: FileDeleteResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    write_string(buf, msg.message)
    return bytes(buf)


def deserialize_file_delete_response(data: bytes) -> FileDeleteResponse:
    buf = Buffer(data)
    return FileDeleteResponse(
        result=buf.read_u32(),
        message=buf.read_string(),
    )


def serialize_file_upload_chunk_request(msg: FileUploadChunkRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.file_id)
    write_u32(buf, msg.chunk_index)
    write_u32(buf, msg.chunk_count)
    write_bytes(buf, msg.data)
    return bytes(buf)


def deserialize_file_upload_chunk_request(data: bytes) -> FileUploadChunkRequest:
    buf = Buffer(data)
    return FileUploadChunkRequest(
        file_id=buf.read_u64(),
        chunk_index=buf.read_u32(),
        chunk_count=buf.read_u32(),
        data=buf.read_bytes(),
    )


def serialize_file_upload_chunk_ack(msg: FileUploadChunkAck) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.file_id)
    write_u32(buf, msg.chunk_index)
    write_u32(buf, msg.result)
    return bytes(buf)


def deserialize_file_upload_chunk_ack(data: bytes) -> FileUploadChunkAck:
    buf = Buffer(data)
    return FileUploadChunkAck(
        file_id=buf.read_u64(),
        chunk_index=buf.read_u32(),
        result=buf.read_u32(),
    )


def serialize_file_download_request(msg: FileDownloadRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.file_id)
    return bytes(buf)


def deserialize_file_download_request(data: bytes) -> FileDownloadRequest:
    buf = Buffer(data)
    return FileDownloadRequest(file_id=buf.read_u64())


def serialize_file_download_response(msg: FileDownloadResponse) -> bytes:
    buf = bytearray()
    write_u32(buf, msg.result)
    write_string(buf, msg.message)
    write_u64(buf, msg.file_id)
    write_string(buf, msg.filename)
    write_u64(buf, msg.file_size)
    write_u32(buf, msg.chunk_index)
    write_u32(buf, msg.chunk_count)
    write_bytes(buf, msg.data)
    return bytes(buf)


def deserialize_file_download_response(data: bytes) -> FileDownloadResponse:
    buf = Buffer(data)
    return FileDownloadResponse(
        result=buf.read_u32(),
        message=buf.read_string(),
        file_id=buf.read_u64(),
        filename=buf.read_string(),
        file_size=buf.read_u64(),
        chunk_index=buf.read_u32(),
        chunk_count=buf.read_u32(),
        data=buf.read_bytes(),
    )


def serialize_screen_share_start(msg: ScreenShareStartRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.channel_id)
    write_u32(buf, msg.source_type)
    write_string(buf, msg.source_name)
    write_u32(buf, msg.width)
    write_u32(buf, msg.height)
    write_u32(buf, msg.fps)
    write_bool(buf, msg.share_audio)
    write_u32(buf, msg.audio_source)
    write_u32(buf, msg.audio_app_pid)
    return bytes(buf)


def deserialize_screen_share_start(data: bytes) -> ScreenShareStartRequest:
    buf = Buffer(data)
    return ScreenShareStartRequest(
        channel_id=buf.read_u64(),
        source_type=buf.read_u32(),
        source_name=buf.read_string(),
        width=buf.read_u32(),
        height=buf.read_u32(),
        fps=buf.read_u32(),
        share_audio=buf.read_bool(),
        audio_source=buf.read_u32(),
        audio_app_pid=buf.read_u32(),
    )


def serialize_screen_share_stop(msg: ScreenShareStopRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.channel_id)
    return bytes(buf)


def deserialize_screen_share_stop(data: bytes) -> ScreenShareStopRequest:
    buf = Buffer(data)
    return ScreenShareStopRequest(channel_id=buf.read_u64())


def serialize_screen_share_state(msg: ScreenShareState) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.user_id)
    write_bool(buf, msg.sharing)
    write_u32(buf, msg.source_type)
    write_string(buf, msg.source_name)
    write_u32(buf, msg.width)
    write_u32(buf, msg.height)
    write_bool(buf, msg.has_audio)
    return bytes(buf)


def deserialize_screen_share_state(data: bytes) -> ScreenShareState:
    buf = Buffer(data)
    return ScreenShareState(
        user_id=buf.read_u64(),
        sharing=buf.read_bool(),
        source_type=buf.read_u32(),
        source_name=buf.read_string(),
        width=buf.read_u32(),
        height=buf.read_u32(),
        has_audio=buf.read_bool(),
    )


def _serialize_video_profile(buf: bytearray, profile: VideoProfile):
    """序列化 VideoProfile：codec(u32) + width(u32) + height(u32) + fps(u32) + target_bitrate_kbps(u32)。"""
    write_u32(buf, profile.codec)
    write_u32(buf, profile.width)
    write_u32(buf, profile.height)
    write_u32(buf, profile.fps)
    write_u32(buf, profile.target_bitrate_kbps)


def _deserialize_video_profile(buf: Buffer) -> VideoProfile:
    return VideoProfile(
        codec=buf.read_u32(),
        width=buf.read_u32(),
        height=buf.read_u32(),
        fps=buf.read_u32(),
        target_bitrate_kbps=buf.read_u32(),
    )


def serialize_video_call_request(msg: VideoCallRequest) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.caller_id)
    write_u64(buf, msg.callee_id)
    write_u64(buf, msg.call_id)
    if msg.requested_profile is not None:
        _serialize_video_profile(buf, msg.requested_profile)
    else:
        _serialize_video_profile(buf, VideoProfile())
    return bytes(buf)


def deserialize_video_call_request(data: bytes) -> VideoCallRequest:
    buf = Buffer(data)
    return VideoCallRequest(
        caller_id=buf.read_u64(),
        callee_id=buf.read_u64(),
        call_id=buf.read_u64(),
        requested_profile=_deserialize_video_profile(buf),
    )


def serialize_video_call_response(msg: VideoCallResponse) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.call_id)
    write_bool(buf, msg.accepted)
    if msg.negotiated_profile is not None:
        _serialize_video_profile(buf, msg.negotiated_profile)
    else:
        _serialize_video_profile(buf, VideoProfile())
    write_string(buf, msg.reason)
    return bytes(buf)


def deserialize_video_call_response(data: bytes) -> VideoCallResponse:
    buf = Buffer(data)
    return VideoCallResponse(
        call_id=buf.read_u64(),
        accepted=buf.read_bool(),
        negotiated_profile=_deserialize_video_profile(buf),
        reason=buf.read_string(),
    )


def serialize_video_call_hangup(msg: VideoCallHangup) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.call_id)
    write_u32(buf, msg.reason)
    return bytes(buf)


def deserialize_video_call_hangup(data: bytes) -> VideoCallHangup:
    buf = Buffer(data)
    return VideoCallHangup(
        call_id=buf.read_u64(),
        reason=buf.read_u32(),
    )


def serialize_video_call_profile_update(msg: VideoCallProfileUpdate) -> bytes:
    buf = bytearray()
    write_u64(buf, msg.call_id)
    if msg.new_profile is not None:
        _serialize_video_profile(buf, msg.new_profile)
    else:
        _serialize_video_profile(buf, VideoProfile())
    return bytes(buf)


def deserialize_video_call_profile_update(data: bytes) -> VideoCallProfileUpdate:
    buf = Buffer(data)
    return VideoCallProfileUpdate(
        call_id=buf.read_u64(),
        new_profile=_deserialize_video_profile(buf),
    )


def serialize_control_message(msg_type: MessageType, msg) -> bytes:
    case_value, serializer, _ = MESSAGE_TYPE_MAP[msg_type]
    payload = serializer(msg)
    buf = bytearray()
    write_u32(buf, case_value)
    write_u32(buf, len(payload))
    buf.extend(payload)
    return bytes(buf)


def deserialize_control_message(data: bytes):
    """反序列化控制消息。

    网络数据不可信：损坏/截断/畸形输入一律安全失败返回 (None, None)，
    绝不抛出异常（否则会杀死客户端接收线程）。
    """
    try:
        buf = Buffer(data)
        case_value = buf.read_u32()
        payload_size = buf.read_u32()
        payload_data = buf.read_raw(payload_size)
        if case_value in CASE_TO_DESERIALIZER:
            msg_type, deserializer = CASE_TO_DESERIALIZER[case_value]
            msg = deserializer(payload_data)
            return msg_type, msg
        return None, None
    except (struct.error, IndexError, ValueError, TypeError, UnicodeDecodeError):
        return None, None


MESSAGE_TYPE_MAP = {
    MessageType.LOGIN_REQUEST: (1, serialize_login_request, deserialize_login_request),
    MessageType.LOGIN_RESPONSE: (2, serialize_login_response, deserialize_login_response),
    MessageType.JOIN_CHANNEL_REQUEST: (3, serialize_join_channel_request, deserialize_join_channel_request),
    MessageType.LEAVE_CHANNEL_REQUEST: (4, serialize_leave_channel_request, deserialize_leave_channel_request),
    MessageType.CREATE_CHANNEL_REQUEST: (5, serialize_create_channel_request, deserialize_create_channel_request),
    MessageType.DELETE_CHANNEL_REQUEST: (6, serialize_delete_channel_request, deserialize_delete_channel_request),
    MessageType.CHANNEL_LIST_UPDATE: (7, serialize_channel_list_update, deserialize_channel_list_update),
    MessageType.USER_JOINED_CHANNEL: (8, serialize_user_joined_channel, deserialize_user_joined_channel),
    MessageType.USER_LEFT_CHANNEL: (9, serialize_user_left_channel, deserialize_user_left_channel),
    MessageType.USER_SPEAKING: (10, serialize_user_speaking, deserialize_user_speaking),
    MessageType.PTT_TOGGLE: (11, serialize_ptt_toggle, deserialize_ptt_toggle),
    MessageType.USER_MUTE_TOGGLE: (12, serialize_user_mute_toggle, deserialize_user_mute_toggle),
    MessageType.SERVER_MESSAGE: (13, serialize_server_message, deserialize_server_message),
    MessageType.STUN_BIND_REQUEST: (14, serialize_stun_bind_request, deserialize_stun_bind_request),
    MessageType.STUN_BIND_RESPONSE: (15, serialize_stun_bind_response, deserialize_stun_bind_response),
    MessageType.UDP_PING_REQUEST: (16, serialize_udp_ping_request, deserialize_udp_ping_request),
    MessageType.UDP_PING_RESPONSE: (17, serialize_udp_ping_response, deserialize_udp_ping_response),
    MessageType.KEY_ROTATION_REQUEST: (18, serialize_key_rotation_request, deserialize_key_rotation_request),
    MessageType.KEY_ROTATION_RESPONSE: (19, serialize_key_rotation_response, deserialize_key_rotation_response),
    MessageType.ADMIN_AUTH_REQUEST: (20, serialize_admin_auth_request, deserialize_admin_auth_request),
    MessageType.ADMIN_AUTH_RESPONSE: (21, serialize_admin_auth_response, deserialize_admin_auth_response),
    MessageType.SET_ADMIN_REQUEST: (22, serialize_set_admin_request, deserialize_set_admin_request),
    MessageType.SET_ADMIN_RESPONSE: (23, serialize_set_admin_response, deserialize_set_admin_response),
    MessageType.KICK_USER_REQUEST: (24, serialize_kick_user_request, deserialize_kick_user_request),
    MessageType.KICK_USER_RESPONSE: (25, serialize_kick_user_response, deserialize_kick_user_response),
    MessageType.BAN_USER_REQUEST: (26, serialize_ban_user_request, deserialize_ban_user_request),
    MessageType.BAN_USER_RESPONSE: (27, serialize_ban_user_response, deserialize_ban_user_response),
    MessageType.MOVE_USER_REQUEST: (28, serialize_move_user_request, deserialize_move_user_request),
    MessageType.MOVE_USER_RESPONSE: (29, serialize_move_user_response, deserialize_move_user_response),
    MessageType.CHAT_SEND_REQUEST: (30, serialize_chat_send_request, deserialize_chat_send_request),
    MessageType.CHAT_BROADCAST: (31, serialize_chat_broadcast, deserialize_chat_broadcast),
    MessageType.SET_SERVER_NAME_REQUEST: (32, serialize_set_server_name_request, deserialize_set_server_name_request),
    MessageType.SET_SERVER_NAME_RESPONSE: (33, serialize_set_server_name_response, deserialize_set_server_name_response),
    MessageType.RENAME_CHANNEL_REQUEST: (34, serialize_rename_channel_request, deserialize_rename_channel_request),
    MessageType.RENAME_CHANNEL_RESPONSE: (35, serialize_rename_channel_response, deserialize_rename_channel_response),
    MessageType.SPEAKING_STATE: (36, serialize_speaking_state, deserialize_speaking_state),
    MessageType.FILE_LIST_REQUEST: (40, serialize_file_list_request, deserialize_file_list_request),
    MessageType.FILE_LIST_RESPONSE: (41, serialize_file_list_response, deserialize_file_list_response),
    MessageType.FILE_UPLOAD_REQUEST: (42, serialize_file_upload_request, deserialize_file_upload_request),
    MessageType.FILE_UPLOAD_RESPONSE: (43, serialize_file_upload_response, deserialize_file_upload_response),
    MessageType.FILE_UPLOAD_CHUNK_REQUEST: (44, serialize_file_upload_chunk_request, deserialize_file_upload_chunk_request),
    MessageType.FILE_UPLOAD_CHUNK_ACK: (45, serialize_file_upload_chunk_ack, deserialize_file_upload_chunk_ack),
    MessageType.FILE_DOWNLOAD_REQUEST: (46, serialize_file_download_request, deserialize_file_download_request),
    MessageType.FILE_DOWNLOAD_RESPONSE: (47, serialize_file_download_response, deserialize_file_download_response),
    MessageType.FILE_DELETE_REQUEST: (49, serialize_file_delete_request, deserialize_file_delete_request),
    MessageType.FILE_DELETE_RESPONSE: (50, serialize_file_delete_response, deserialize_file_delete_response),
    MessageType.SCREEN_SHARE_START: (60, serialize_screen_share_start, deserialize_screen_share_start),
    MessageType.SCREEN_SHARE_STOP: (61, serialize_screen_share_stop, deserialize_screen_share_stop),
    MessageType.SCREEN_SHARE_STATE: (62, serialize_screen_share_state, deserialize_screen_share_state),
    MessageType.VIDEO_CALL_REQUEST: (80, serialize_video_call_request, deserialize_video_call_request),
    MessageType.VIDEO_CALL_RESPONSE: (81, serialize_video_call_response, deserialize_video_call_response),
    MessageType.VIDEO_CALL_HANGUP: (82, serialize_video_call_hangup, deserialize_video_call_hangup),
    MessageType.VIDEO_CALL_PROFILE_UPDATE: (83, serialize_video_call_profile_update, deserialize_video_call_profile_update),
}

CASE_TO_DESERIALIZER = {
    1: (MessageType.LOGIN_REQUEST, deserialize_login_request),
    2: (MessageType.LOGIN_RESPONSE, deserialize_login_response),
    3: (MessageType.JOIN_CHANNEL_REQUEST, deserialize_join_channel_request),
    4: (MessageType.LEAVE_CHANNEL_REQUEST, deserialize_leave_channel_request),
    5: (MessageType.CREATE_CHANNEL_REQUEST, deserialize_create_channel_request),
    6: (MessageType.DELETE_CHANNEL_REQUEST, deserialize_delete_channel_request),
    7: (MessageType.CHANNEL_LIST_UPDATE, deserialize_channel_list_update),
    8: (MessageType.USER_JOINED_CHANNEL, deserialize_user_joined_channel),
    9: (MessageType.USER_LEFT_CHANNEL, deserialize_user_left_channel),
    10: (MessageType.USER_SPEAKING, deserialize_user_speaking),
    11: (MessageType.PTT_TOGGLE, deserialize_ptt_toggle),
    12: (MessageType.USER_MUTE_TOGGLE, deserialize_user_mute_toggle),
    13: (MessageType.SERVER_MESSAGE, deserialize_server_message),
    14: (MessageType.STUN_BIND_REQUEST, deserialize_stun_bind_request),
    15: (MessageType.STUN_BIND_RESPONSE, deserialize_stun_bind_response),
    16: (MessageType.UDP_PING_REQUEST, deserialize_udp_ping_request),
    17: (MessageType.UDP_PING_RESPONSE, deserialize_udp_ping_response),
    18: (MessageType.KEY_ROTATION_REQUEST, deserialize_key_rotation_request),
    19: (MessageType.KEY_ROTATION_RESPONSE, deserialize_key_rotation_response),
    20: (MessageType.ADMIN_AUTH_REQUEST, deserialize_admin_auth_request),
    21: (MessageType.ADMIN_AUTH_RESPONSE, deserialize_admin_auth_response),
    22: (MessageType.SET_ADMIN_REQUEST, deserialize_set_admin_request),
    23: (MessageType.SET_ADMIN_RESPONSE, deserialize_set_admin_response),
    24: (MessageType.KICK_USER_REQUEST, deserialize_kick_user_request),
    25: (MessageType.KICK_USER_RESPONSE, deserialize_kick_user_response),
    26: (MessageType.BAN_USER_REQUEST, deserialize_ban_user_request),
    27: (MessageType.BAN_USER_RESPONSE, deserialize_ban_user_response),
    28: (MessageType.MOVE_USER_REQUEST, deserialize_move_user_request),
    29: (MessageType.MOVE_USER_RESPONSE, deserialize_move_user_response),
    30: (MessageType.CHAT_SEND_REQUEST, deserialize_chat_send_request),
    31: (MessageType.CHAT_BROADCAST, deserialize_chat_broadcast),
    32: (MessageType.SET_SERVER_NAME_REQUEST, deserialize_set_server_name_request),
    33: (MessageType.SET_SERVER_NAME_RESPONSE, deserialize_set_server_name_response),
    34: (MessageType.RENAME_CHANNEL_REQUEST, deserialize_rename_channel_request),
    35: (MessageType.RENAME_CHANNEL_RESPONSE, deserialize_rename_channel_response),
    36: (MessageType.SPEAKING_STATE, deserialize_speaking_state),
    40: (MessageType.FILE_LIST_REQUEST, deserialize_file_list_request),
    41: (MessageType.FILE_LIST_RESPONSE, deserialize_file_list_response),
    42: (MessageType.FILE_UPLOAD_REQUEST, deserialize_file_upload_request),
    43: (MessageType.FILE_UPLOAD_RESPONSE, deserialize_file_upload_response),
    44: (MessageType.FILE_UPLOAD_CHUNK_REQUEST, deserialize_file_upload_chunk_request),
    45: (MessageType.FILE_UPLOAD_CHUNK_ACK, deserialize_file_upload_chunk_ack),
    46: (MessageType.FILE_DOWNLOAD_REQUEST, deserialize_file_download_request),
    47: (MessageType.FILE_DOWNLOAD_RESPONSE, deserialize_file_download_response),
    49: (MessageType.FILE_DELETE_REQUEST, deserialize_file_delete_request),
    50: (MessageType.FILE_DELETE_RESPONSE, deserialize_file_delete_response),
    60: (MessageType.SCREEN_SHARE_START, deserialize_screen_share_start),
    61: (MessageType.SCREEN_SHARE_STOP, deserialize_screen_share_stop),
    62: (MessageType.SCREEN_SHARE_STATE, deserialize_screen_share_state),
    80: (MessageType.VIDEO_CALL_REQUEST, deserialize_video_call_request),
    81: (MessageType.VIDEO_CALL_RESPONSE, deserialize_video_call_response),
    82: (MessageType.VIDEO_CALL_HANGUP, deserialize_video_call_hangup),
    83: (MessageType.VIDEO_CALL_PROFILE_UPDATE, deserialize_video_call_profile_update),
}


# ============================================================
# 聊天中继分片标记（真实字节流通道，纯逻辑，便于单元测试）
#
# 服务端当前不支持文件字节流（FileUploadRequest 无数据字段、无分片消息、
# 无下载处理），但 ChatSend/ChatBroadcast 是完整可用的中继通道：
#  - 服务端 handleChatSend 限制单条文本 ≤ 4096 字节；
#  - ChatBroadcast 会回显给发送者本人（客户端侧按 sender_id 跳过自身）。
#
# 标记格式（base64 承载二进制，长度受控）：
#   数据分片（chunk_index == 0 时额外携带 base64(文件名)）：
#     [NEVOFD|1|<file_id>|<chunk_count>|<chunk_index>|<b64(filename)>|<b64(data)>]
#     [NEVOFD|1|<file_id>|<chunk_count>|<chunk_index>|<b64(data)>]
#   取回请求（接收方缺数据时向频道广播，由原始上传者响应）：
#     [NEVOFGET|1|<file_id>]
# 上述标记在 _handle_chat_broadcast 中被拦截处理，不会作为聊天内容显示。
# ============================================================

import base64 as _base64
import re as _re

FILE_CHUNK_MARKER_PREFIX = "[NEVOFD|1|"
FILE_FETCH_MARKER_PREFIX = "[NEVOFGET|1|"

# 聊天通道单条文本的服务端上限 4096 字节；标记自身保留 ~256 字节余量
FILE_CHAT_MARKER_MAX_TEXT = 4096
FILE_CHAT_MARKER_OVERHEAD = 256

# 默认聊天通道每片原始字节数（base64 后 3200 字符 + 前缀/编号 ≈ 3240 < 4096）
FILE_CHAT_CHUNK_SIZE = 2400
# Wire 通道每片原始字节数（TCP 帧上限 1MB，取 16KB 与任务建议一致）
FILE_WIRE_CHUNK_SIZE = 16 * 1024

# 与 ServerConfig.file_transfer.max_file_size_mb（默认 100MB）保持一致
FILE_MAX_SIZE_BYTES = 100 * 1024 * 1024


def encode_file_chunk_marker(file_id: int, chunk_count: int, chunk_index: int,
                             data: bytes, filename: str = "") -> str:
    """把一块文件数据编码成聊天分片标记文本。

    chunk_index == 0 且提供 filename 时，标记中附带 base64 文件名，
    接收方据此决定写盘扩展名。
    """
    payload = _base64.b64encode(data).decode("ascii")
    if chunk_index == 0 and filename:
        name_b64 = _base64.b64encode(filename.encode("utf-8")).decode("ascii")
        return f"{FILE_CHUNK_MARKER_PREFIX}{file_id}|{chunk_count}|{chunk_index}|{name_b64}|{payload}]"
    return f"{FILE_CHUNK_MARKER_PREFIX}{file_id}|{chunk_count}|{chunk_index}|{payload}]"


_FETCH_RE = _re.compile(r"^\[NEVOFGET\|1\|(\d+)\]$")
_CHUNK_RE = _re.compile(r"^\[NEVOFD\|1\|(\d+)\|(\d+)\|(\d+)\|([^\]]+)\]$")


def decode_file_chunk_marker(text: str):
    """解析聊天分片标记；不是分片标记时返回 None。

    返回 dict: file_id, chunk_count, chunk_index, filename(可空), data
    """
    m = _CHUNK_RE.match(text)
    if not m:
        return None
    file_id = int(m.group(1))
    chunk_count = int(m.group(2))
    chunk_index = int(m.group(3))
    payload = m.group(4)
    filename = ""
    # chunk 0 可能携带 "b64(filename)|b64(data)" 两段
    if chunk_index == 0 and "|" in payload:
        name_part, _, data_part = payload.partition("|")
        try:
            filename = _base64.b64decode(name_part).decode("utf-8")
        except Exception:
            filename = ""
        payload = data_part
    try:
        data = _base64.b64decode(payload)
    except Exception:
        return None
    return {
        "file_id": file_id,
        "chunk_count": chunk_count,
        "chunk_index": chunk_index,
        "filename": filename,
        "data": data,
    }


def decode_file_fetch_marker(text: str):
    """解析取回请求标记；不是取回标记时返回 None。返回 file_id。"""
    m = _FETCH_RE.match(text)
    if not m:
        return None
    return int(m.group(1))


def is_file_transfer_marker(text: str) -> bool:
    """判断聊天文本是否为文件传输标记（分片或取回请求）。"""
    if not text:
        return False
    return text.startswith(FILE_CHUNK_MARKER_PREFIX) or text.startswith(FILE_FETCH_MARKER_PREFIX)


def pick_chat_chunk_size(filename: str = "") -> int:
    """按文件名长度挑选安全的分片大小，保证单条聊天文本不超服务端上限。"""
    size = FILE_CHAT_CHUNK_SIZE
    while size >= 1024:
        worst = len(encode_file_chunk_marker(9 * 10 ** 15, 10 ** 9, 0, b"x" * size, filename))
        if worst <= FILE_CHAT_MARKER_MAX_TEXT - FILE_CHAT_MARKER_OVERHEAD:
            return size
        size -= 512
    return 1024


def iter_file_chunks(data: bytes, chunk_size: int):
    """按固定大小切分字节流，产出 (chunk_index, chunk_data) 生成器。"""
    total = len(data)
    index = 0
    offset = 0
    while offset < total:
        yield index, data[offset:offset + chunk_size]
        index += 1
        offset += chunk_size


class FileChunkAssembler:
    """按分片重组文件字节流（纯内存实现，无网络/Qt 依赖）。

    用法：对每个收到的分片调用 add_chunk()，complete 为 True 后
    取 data/filename 写盘。用于：
      - 聊天中继分片标记重组（nevo_client._handle_chat_broadcast）
      - Wire FileDownloadResponse 分片重组（服务端实现 46/47 后）
    """

    def __init__(self, file_id: int, chunk_count: int, max_bytes: int = FILE_MAX_SIZE_BYTES):
        self.file_id = file_id
        self.chunk_count = int(chunk_count)
        self.max_bytes = int(max_bytes)
        self.filename = ""
        self._chunks: dict = {}
        self._bytes_received = 0

    @property
    def complete(self) -> bool:
        return self.chunk_count > 0 and len(self._chunks) >= self.chunk_count

    @property
    def received_count(self) -> int:
        return len(self._chunks)

    @property
    def data(self) -> bytes:
        if not self.complete:
            return b""
        return b"".join(self._chunks[i] for i in range(self.chunk_count))

    def add_chunk(self, chunk_index: int, data: bytes, filename: str = "") -> bool:
        """写入一块分片；返回是否因此重组完成。"""
        if self.complete:
            return True
        if chunk_index < 0 or chunk_index >= self.chunk_count:
            return False
        if filename:
            self.filename = filename
        if chunk_index in self._chunks:
            return self.complete
        if self._bytes_received + len(data) > self.max_bytes:
            return False
        self._chunks[chunk_index] = data
        self._bytes_received += len(data)
        return self.complete

