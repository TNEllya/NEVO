import common_pb2 as _common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class MessageType(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = ()
    MSG_UNKNOWN: _ClassVar[MessageType]
    MSG_LOGIN_REQUEST: _ClassVar[MessageType]
    MSG_LOGIN_RESPONSE: _ClassVar[MessageType]
    MSG_JOIN_CHANNEL: _ClassVar[MessageType]
    MSG_LEAVE_CHANNEL: _ClassVar[MessageType]
    MSG_CREATE_CHANNEL: _ClassVar[MessageType]
    MSG_DELETE_CHANNEL: _ClassVar[MessageType]
    MSG_CHANNEL_LIST: _ClassVar[MessageType]
    MSG_USER_JOINED: _ClassVar[MessageType]
    MSG_USER_LEFT: _ClassVar[MessageType]
    MSG_USER_SPEAKING: _ClassVar[MessageType]
    MSG_PTT_TOGGLE: _ClassVar[MessageType]
    MSG_MUTE_TOGGLE: _ClassVar[MessageType]
    MSG_SERVER_MESSAGE: _ClassVar[MessageType]
    MSG_STUN_BIND_REQUEST: _ClassVar[MessageType]
    MSG_STUN_BIND_RESPONSE: _ClassVar[MessageType]
    MSG_UDP_PING_REQUEST: _ClassVar[MessageType]
    MSG_UDP_PING_RESPONSE: _ClassVar[MessageType]
    MSG_KEY_ROTATION_REQUEST: _ClassVar[MessageType]
    MSG_KEY_ROTATION_RESPONSE: _ClassVar[MessageType]
    MSG_ADMIN_AUTH_REQUEST: _ClassVar[MessageType]
    MSG_ADMIN_AUTH_RESPONSE: _ClassVar[MessageType]
    MSG_SET_ADMIN_REQUEST: _ClassVar[MessageType]
    MSG_SET_ADMIN_RESPONSE: _ClassVar[MessageType]
    MSG_KICK_USER_REQUEST: _ClassVar[MessageType]
    MSG_KICK_USER_RESPONSE: _ClassVar[MessageType]
    MSG_BAN_USER_REQUEST: _ClassVar[MessageType]
    MSG_BAN_USER_RESPONSE: _ClassVar[MessageType]
    MSG_MOVE_USER_REQUEST: _ClassVar[MessageType]
    MSG_MOVE_USER_RESPONSE: _ClassVar[MessageType]
    MSG_CHAT_SEND: _ClassVar[MessageType]
    MSG_CHAT_BROADCAST: _ClassVar[MessageType]
    MSG_SET_SERVER_NAME_REQUEST: _ClassVar[MessageType]
    MSG_SET_SERVER_NAME_RESPONSE: _ClassVar[MessageType]
    MSG_RENAME_CHANNEL_REQUEST: _ClassVar[MessageType]
    MSG_RENAME_CHANNEL_RESPONSE: _ClassVar[MessageType]
    MSG_SPEAKING_STATE: _ClassVar[MessageType]
    MSG_FILE_LIST_REQUEST: _ClassVar[MessageType]
    MSG_FILE_LIST_RESPONSE: _ClassVar[MessageType]
    MSG_FILE_UPLOAD_REQUEST: _ClassVar[MessageType]
    MSG_FILE_UPLOAD_RESPONSE: _ClassVar[MessageType]
    MSG_FILE_DOWNLOAD_REQUEST: _ClassVar[MessageType]
    MSG_FILE_DOWNLOAD_RESPONSE: _ClassVar[MessageType]
    MSG_FILE_DELETE_REQUEST: _ClassVar[MessageType]
    MSG_FILE_DELETE_RESPONSE: _ClassVar[MessageType]
MSG_UNKNOWN: MessageType
MSG_LOGIN_REQUEST: MessageType
MSG_LOGIN_RESPONSE: MessageType
MSG_JOIN_CHANNEL: MessageType
MSG_LEAVE_CHANNEL: MessageType
MSG_CREATE_CHANNEL: MessageType
MSG_DELETE_CHANNEL: MessageType
MSG_CHANNEL_LIST: MessageType
MSG_USER_JOINED: MessageType
MSG_USER_LEFT: MessageType
MSG_USER_SPEAKING: MessageType
MSG_PTT_TOGGLE: MessageType
MSG_MUTE_TOGGLE: MessageType
MSG_SERVER_MESSAGE: MessageType
MSG_STUN_BIND_REQUEST: MessageType
MSG_STUN_BIND_RESPONSE: MessageType
MSG_UDP_PING_REQUEST: MessageType
MSG_UDP_PING_RESPONSE: MessageType
MSG_KEY_ROTATION_REQUEST: MessageType
MSG_KEY_ROTATION_RESPONSE: MessageType
MSG_ADMIN_AUTH_REQUEST: MessageType
MSG_ADMIN_AUTH_RESPONSE: MessageType
MSG_SET_ADMIN_REQUEST: MessageType
MSG_SET_ADMIN_RESPONSE: MessageType
MSG_KICK_USER_REQUEST: MessageType
MSG_KICK_USER_RESPONSE: MessageType
MSG_BAN_USER_REQUEST: MessageType
MSG_BAN_USER_RESPONSE: MessageType
MSG_MOVE_USER_REQUEST: MessageType
MSG_MOVE_USER_RESPONSE: MessageType
MSG_CHAT_SEND: MessageType
MSG_CHAT_BROADCAST: MessageType
MSG_SET_SERVER_NAME_REQUEST: MessageType
MSG_SET_SERVER_NAME_RESPONSE: MessageType
MSG_RENAME_CHANNEL_REQUEST: MessageType
MSG_RENAME_CHANNEL_RESPONSE: MessageType
MSG_SPEAKING_STATE: MessageType
MSG_FILE_LIST_REQUEST: MessageType
MSG_FILE_LIST_RESPONSE: MessageType
MSG_FILE_UPLOAD_REQUEST: MessageType
MSG_FILE_UPLOAD_RESPONSE: MessageType
MSG_FILE_DOWNLOAD_REQUEST: MessageType
MSG_FILE_DOWNLOAD_RESPONSE: MessageType
MSG_FILE_DELETE_REQUEST: MessageType
MSG_FILE_DELETE_RESPONSE: MessageType

class TcpPacketHeader(_message.Message):
    __slots__ = ("payload_length", "message_type", "request_id")
    PAYLOAD_LENGTH_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_TYPE_FIELD_NUMBER: _ClassVar[int]
    REQUEST_ID_FIELD_NUMBER: _ClassVar[int]
    payload_length: int
    message_type: int
    request_id: int
    def __init__(self, payload_length: _Optional[int] = ..., message_type: _Optional[int] = ..., request_id: _Optional[int] = ...) -> None: ...

class LoginRequest(_message.Message):
    __slots__ = ("username", "auth_credential", "key_exchange_methods", "client_public_key", "client_udp_port")
    USERNAME_FIELD_NUMBER: _ClassVar[int]
    AUTH_CREDENTIAL_FIELD_NUMBER: _ClassVar[int]
    KEY_EXCHANGE_METHODS_FIELD_NUMBER: _ClassVar[int]
    CLIENT_PUBLIC_KEY_FIELD_NUMBER: _ClassVar[int]
    CLIENT_UDP_PORT_FIELD_NUMBER: _ClassVar[int]
    username: str
    auth_credential: bytes
    key_exchange_methods: _containers.RepeatedScalarFieldContainer[str]
    client_public_key: bytes
    client_udp_port: int
    def __init__(self, username: _Optional[str] = ..., auth_credential: _Optional[bytes] = ..., key_exchange_methods: _Optional[_Iterable[str]] = ..., client_public_key: _Optional[bytes] = ..., client_udp_port: _Optional[int] = ...) -> None: ...

class JoinChannelRequest(_message.Message):
    __slots__ = ("channel_id",)
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    channel_id: int
    def __init__(self, channel_id: _Optional[int] = ...) -> None: ...

class LeaveChannelRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class CreateChannelRequest(_message.Message):
    __slots__ = ("parent_id", "name")
    PARENT_ID_FIELD_NUMBER: _ClassVar[int]
    NAME_FIELD_NUMBER: _ClassVar[int]
    parent_id: int
    name: str
    def __init__(self, parent_id: _Optional[int] = ..., name: _Optional[str] = ...) -> None: ...

class DeleteChannelRequest(_message.Message):
    __slots__ = ("channel_id",)
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    channel_id: int
    def __init__(self, channel_id: _Optional[int] = ...) -> None: ...

class RenameChannelRequest(_message.Message):
    __slots__ = ("channel_id", "new_name")
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    NEW_NAME_FIELD_NUMBER: _ClassVar[int]
    channel_id: int
    new_name: str
    def __init__(self, channel_id: _Optional[int] = ..., new_name: _Optional[str] = ...) -> None: ...

class RenameChannelResponse(_message.Message):
    __slots__ = ("result", "message")
    RESULT_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    result: _common_pb2.ResultCode
    message: str
    def __init__(self, result: _Optional[_Union[_common_pb2.ResultCode, str]] = ..., message: _Optional[str] = ...) -> None: ...

class PttToggle(_message.Message):
    __slots__ = ("active",)
    ACTIVE_FIELD_NUMBER: _ClassVar[int]
    active: bool
    def __init__(self, active: bool = ...) -> None: ...

class UserMuteToggle(_message.Message):
    __slots__ = ("muted",)
    MUTED_FIELD_NUMBER: _ClassVar[int]
    muted: bool
    def __init__(self, muted: bool = ...) -> None: ...

class StunBindRequest(_message.Message):
    __slots__ = ("transaction_id",)
    TRANSACTION_ID_FIELD_NUMBER: _ClassVar[int]
    transaction_id: int
    def __init__(self, transaction_id: _Optional[int] = ...) -> None: ...

class UdpPingRequest(_message.Message):
    __slots__ = ("sequence", "client_udp_key")
    SEQUENCE_FIELD_NUMBER: _ClassVar[int]
    CLIENT_UDP_KEY_FIELD_NUMBER: _ClassVar[int]
    sequence: int
    client_udp_key: bytes
    def __init__(self, sequence: _Optional[int] = ..., client_udp_key: _Optional[bytes] = ...) -> None: ...

class LoginResponse(_message.Message):
    __slots__ = ("result", "user_info", "session_token", "server_public_key", "key_exchange_method", "encrypted_session_key", "owner_exists", "server_udp_port")
    RESULT_FIELD_NUMBER: _ClassVar[int]
    USER_INFO_FIELD_NUMBER: _ClassVar[int]
    SESSION_TOKEN_FIELD_NUMBER: _ClassVar[int]
    SERVER_PUBLIC_KEY_FIELD_NUMBER: _ClassVar[int]
    KEY_EXCHANGE_METHOD_FIELD_NUMBER: _ClassVar[int]
    ENCRYPTED_SESSION_KEY_FIELD_NUMBER: _ClassVar[int]
    OWNER_EXISTS_FIELD_NUMBER: _ClassVar[int]
    SERVER_UDP_PORT_FIELD_NUMBER: _ClassVar[int]
    result: _common_pb2.ResultCode
    user_info: _common_pb2.UserInfo
    session_token: str
    server_public_key: bytes
    key_exchange_method: str
    encrypted_session_key: bytes
    owner_exists: bool
    server_udp_port: int
    def __init__(self, result: _Optional[_Union[_common_pb2.ResultCode, str]] = ..., user_info: _Optional[_Union[_common_pb2.UserInfo, _Mapping]] = ..., session_token: _Optional[str] = ..., server_public_key: _Optional[bytes] = ..., key_exchange_method: _Optional[str] = ..., encrypted_session_key: _Optional[bytes] = ..., owner_exists: bool = ..., server_udp_port: _Optional[int] = ...) -> None: ...

class ChannelListUpdate(_message.Message):
    __slots__ = ("channels",)
    CHANNELS_FIELD_NUMBER: _ClassVar[int]
    channels: _containers.RepeatedCompositeFieldContainer[_common_pb2.ChannelInfo]
    def __init__(self, channels: _Optional[_Iterable[_Union[_common_pb2.ChannelInfo, _Mapping]]] = ...) -> None: ...

class UserJoinedChannel(_message.Message):
    __slots__ = ("user", "channel_id")
    USER_FIELD_NUMBER: _ClassVar[int]
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    user: _common_pb2.UserInfo
    channel_id: int
    def __init__(self, user: _Optional[_Union[_common_pb2.UserInfo, _Mapping]] = ..., channel_id: _Optional[int] = ...) -> None: ...

class UserLeftChannel(_message.Message):
    __slots__ = ("user_id", "channel_id")
    USER_ID_FIELD_NUMBER: _ClassVar[int]
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    user_id: int
    channel_id: int
    def __init__(self, user_id: _Optional[int] = ..., channel_id: _Optional[int] = ...) -> None: ...

class UserSpeaking(_message.Message):
    __slots__ = ("user_id", "speaking")
    USER_ID_FIELD_NUMBER: _ClassVar[int]
    SPEAKING_FIELD_NUMBER: _ClassVar[int]
    user_id: int
    speaking: bool
    def __init__(self, user_id: _Optional[int] = ..., speaking: bool = ...) -> None: ...

class ServerMessage(_message.Message):
    __slots__ = ("text",)
    TEXT_FIELD_NUMBER: _ClassVar[int]
    text: str
    def __init__(self, text: _Optional[str] = ...) -> None: ...

class StunBindResponse(_message.Message):
    __slots__ = ("transaction_id", "mapped_address", "nat_type")
    TRANSACTION_ID_FIELD_NUMBER: _ClassVar[int]
    MAPPED_ADDRESS_FIELD_NUMBER: _ClassVar[int]
    NAT_TYPE_FIELD_NUMBER: _ClassVar[int]
    transaction_id: int
    mapped_address: bytes
    nat_type: int
    def __init__(self, transaction_id: _Optional[int] = ..., mapped_address: _Optional[bytes] = ..., nat_type: _Optional[int] = ...) -> None: ...

class UdpPingResponse(_message.Message):
    __slots__ = ("sequence", "udp_reachable")
    SEQUENCE_FIELD_NUMBER: _ClassVar[int]
    UDP_REACHABLE_FIELD_NUMBER: _ClassVar[int]
    sequence: int
    udp_reachable: bool
    def __init__(self, sequence: _Optional[int] = ..., udp_reachable: bool = ...) -> None: ...

class KeyRotationRequest(_message.Message):
    __slots__ = ("new_server_public_key", "key_epoch", "encrypted_session_key")
    NEW_SERVER_PUBLIC_KEY_FIELD_NUMBER: _ClassVar[int]
    KEY_EPOCH_FIELD_NUMBER: _ClassVar[int]
    ENCRYPTED_SESSION_KEY_FIELD_NUMBER: _ClassVar[int]
    new_server_public_key: bytes
    key_epoch: int
    encrypted_session_key: bytes
    def __init__(self, new_server_public_key: _Optional[bytes] = ..., key_epoch: _Optional[int] = ..., encrypted_session_key: _Optional[bytes] = ...) -> None: ...

class KeyRotationResponse(_message.Message):
    __slots__ = ("new_client_public_key", "key_epoch")
    NEW_CLIENT_PUBLIC_KEY_FIELD_NUMBER: _ClassVar[int]
    KEY_EPOCH_FIELD_NUMBER: _ClassVar[int]
    new_client_public_key: bytes
    key_epoch: int
    def __init__(self, new_client_public_key: _Optional[bytes] = ..., key_epoch: _Optional[int] = ...) -> None: ...

class AdminAuthRequest(_message.Message):
    __slots__ = ("password",)
    PASSWORD_FIELD_NUMBER: _ClassVar[int]
    password: str
    def __init__(self, password: _Optional[str] = ...) -> None: ...

class AdminAuthResponse(_message.Message):
    __slots__ = ("result", "message")
    RESULT_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    result: _common_pb2.ResultCode
    message: str
    def __init__(self, result: _Optional[_Union[_common_pb2.ResultCode, str]] = ..., message: _Optional[str] = ...) -> None: ...

class SetAdminRequest(_message.Message):
    __slots__ = ("user_id", "set_admin")
    USER_ID_FIELD_NUMBER: _ClassVar[int]
    SET_ADMIN_FIELD_NUMBER: _ClassVar[int]
    user_id: int
    set_admin: bool
    def __init__(self, user_id: _Optional[int] = ..., set_admin: bool = ...) -> None: ...

class SetAdminResponse(_message.Message):
    __slots__ = ("result", "message")
    RESULT_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    result: _common_pb2.ResultCode
    message: str
    def __init__(self, result: _Optional[_Union[_common_pb2.ResultCode, str]] = ..., message: _Optional[str] = ...) -> None: ...

class SetServerNameRequest(_message.Message):
    __slots__ = ("server_name",)
    SERVER_NAME_FIELD_NUMBER: _ClassVar[int]
    server_name: str
    def __init__(self, server_name: _Optional[str] = ...) -> None: ...

class SetServerNameResponse(_message.Message):
    __slots__ = ("result", "message")
    RESULT_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    result: _common_pb2.ResultCode
    message: str
    def __init__(self, result: _Optional[_Union[_common_pb2.ResultCode, str]] = ..., message: _Optional[str] = ...) -> None: ...

class KickUserRequest(_message.Message):
    __slots__ = ("user_id", "reason")
    USER_ID_FIELD_NUMBER: _ClassVar[int]
    REASON_FIELD_NUMBER: _ClassVar[int]
    user_id: int
    reason: str
    def __init__(self, user_id: _Optional[int] = ..., reason: _Optional[str] = ...) -> None: ...

class KickUserResponse(_message.Message):
    __slots__ = ("result", "message")
    RESULT_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    result: _common_pb2.ResultCode
    message: str
    def __init__(self, result: _Optional[_Union[_common_pb2.ResultCode, str]] = ..., message: _Optional[str] = ...) -> None: ...

class BanUserRequest(_message.Message):
    __slots__ = ("user_id", "reason", "expires_at")
    USER_ID_FIELD_NUMBER: _ClassVar[int]
    REASON_FIELD_NUMBER: _ClassVar[int]
    EXPIRES_AT_FIELD_NUMBER: _ClassVar[int]
    user_id: int
    reason: str
    expires_at: int
    def __init__(self, user_id: _Optional[int] = ..., reason: _Optional[str] = ..., expires_at: _Optional[int] = ...) -> None: ...

class BanUserResponse(_message.Message):
    __slots__ = ("result", "message")
    RESULT_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    result: _common_pb2.ResultCode
    message: str
    def __init__(self, result: _Optional[_Union[_common_pb2.ResultCode, str]] = ..., message: _Optional[str] = ...) -> None: ...

class MoveUserRequest(_message.Message):
    __slots__ = ("user_id", "channel_id")
    USER_ID_FIELD_NUMBER: _ClassVar[int]
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    user_id: int
    channel_id: int
    def __init__(self, user_id: _Optional[int] = ..., channel_id: _Optional[int] = ...) -> None: ...

class MoveUserResponse(_message.Message):
    __slots__ = ("result", "message")
    RESULT_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    result: _common_pb2.ResultCode
    message: str
    def __init__(self, result: _Optional[_Union[_common_pb2.ResultCode, str]] = ..., message: _Optional[str] = ...) -> None: ...

class ChatSendRequest(_message.Message):
    __slots__ = ("channel_id", "text")
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    TEXT_FIELD_NUMBER: _ClassVar[int]
    channel_id: int
    text: str
    def __init__(self, channel_id: _Optional[int] = ..., text: _Optional[str] = ...) -> None: ...

class ChatBroadcast(_message.Message):
    __slots__ = ("sender_id", "sender_name", "channel_id", "text", "timestamp")
    SENDER_ID_FIELD_NUMBER: _ClassVar[int]
    SENDER_NAME_FIELD_NUMBER: _ClassVar[int]
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    TEXT_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    sender_id: int
    sender_name: str
    channel_id: int
    text: str
    timestamp: int
    def __init__(self, sender_id: _Optional[int] = ..., sender_name: _Optional[str] = ..., channel_id: _Optional[int] = ..., text: _Optional[str] = ..., timestamp: _Optional[int] = ...) -> None: ...

class FileEntry(_message.Message):
    __slots__ = ("id", "channel_id", "uploader_id", "filename", "file_size", "upload_time")
    ID_FIELD_NUMBER: _ClassVar[int]
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    UPLOADER_ID_FIELD_NUMBER: _ClassVar[int]
    FILENAME_FIELD_NUMBER: _ClassVar[int]
    FILE_SIZE_FIELD_NUMBER: _ClassVar[int]
    UPLOAD_TIME_FIELD_NUMBER: _ClassVar[int]
    id: int
    channel_id: int
    uploader_id: int
    filename: str
    file_size: int
    upload_time: int
    def __init__(self, id: _Optional[int] = ..., channel_id: _Optional[int] = ..., uploader_id: _Optional[int] = ..., filename: _Optional[str] = ..., file_size: _Optional[int] = ..., upload_time: _Optional[int] = ...) -> None: ...

class SpeakingState(_message.Message):
    __slots__ = ("speaking",)
    SPEAKING_FIELD_NUMBER: _ClassVar[int]
    speaking: bool
    def __init__(self, speaking: bool = ...) -> None: ...

class FileListRequest(_message.Message):
    __slots__ = ("channel_id",)
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    channel_id: int
    def __init__(self, channel_id: _Optional[int] = ...) -> None: ...

class FileListResponse(_message.Message):
    __slots__ = ("entries",)
    ENTRIES_FIELD_NUMBER: _ClassVar[int]
    entries: _containers.RepeatedCompositeFieldContainer[FileEntry]
    def __init__(self, entries: _Optional[_Iterable[_Union[FileEntry, _Mapping]]] = ...) -> None: ...

class FileUploadRequest(_message.Message):
    __slots__ = ("channel_id", "filename", "file_size")
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    FILENAME_FIELD_NUMBER: _ClassVar[int]
    FILE_SIZE_FIELD_NUMBER: _ClassVar[int]
    channel_id: int
    filename: str
    file_size: int
    def __init__(self, channel_id: _Optional[int] = ..., filename: _Optional[str] = ..., file_size: _Optional[int] = ...) -> None: ...

class FileUploadResponse(_message.Message):
    __slots__ = ("result", "message", "file_id")
    RESULT_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    FILE_ID_FIELD_NUMBER: _ClassVar[int]
    result: _common_pb2.ResultCode
    message: str
    file_id: int
    def __init__(self, result: _Optional[_Union[_common_pb2.ResultCode, str]] = ..., message: _Optional[str] = ..., file_id: _Optional[int] = ...) -> None: ...

class FileDeleteRequest(_message.Message):
    __slots__ = ("file_id",)
    FILE_ID_FIELD_NUMBER: _ClassVar[int]
    file_id: int
    def __init__(self, file_id: _Optional[int] = ...) -> None: ...

class FileDeleteResponse(_message.Message):
    __slots__ = ("result", "message")
    RESULT_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    result: _common_pb2.ResultCode
    message: str
    def __init__(self, result: _Optional[_Union[_common_pb2.ResultCode, str]] = ..., message: _Optional[str] = ...) -> None: ...

class ControlMessage(_message.Message):
    __slots__ = ("login_request", "login_response", "join_channel", "leave_channel", "create_channel", "delete_channel", "channel_list", "user_joined", "user_left", "user_speaking", "ptt_toggle", "mute_toggle", "server_message", "stun_bind_request", "stun_bind_response", "udp_ping_request", "udp_ping_response", "key_rotation_request", "key_rotation_response", "admin_auth_request", "admin_auth_response", "set_admin_request", "set_admin_response", "kick_user_request", "kick_user_response", "ban_user_request", "ban_user_response", "move_user_request", "move_user_response", "chat_send", "chat_broadcast", "set_server_name_request", "set_server_name_response", "rename_channel", "rename_channel_response", "file_list_request", "file_list_response", "file_upload_request", "file_upload_response", "file_delete_request", "file_delete_response")
    LOGIN_REQUEST_FIELD_NUMBER: _ClassVar[int]
    LOGIN_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    JOIN_CHANNEL_FIELD_NUMBER: _ClassVar[int]
    LEAVE_CHANNEL_FIELD_NUMBER: _ClassVar[int]
    CREATE_CHANNEL_FIELD_NUMBER: _ClassVar[int]
    DELETE_CHANNEL_FIELD_NUMBER: _ClassVar[int]
    CHANNEL_LIST_FIELD_NUMBER: _ClassVar[int]
    USER_JOINED_FIELD_NUMBER: _ClassVar[int]
    USER_LEFT_FIELD_NUMBER: _ClassVar[int]
    USER_SPEAKING_FIELD_NUMBER: _ClassVar[int]
    PTT_TOGGLE_FIELD_NUMBER: _ClassVar[int]
    MUTE_TOGGLE_FIELD_NUMBER: _ClassVar[int]
    SERVER_MESSAGE_FIELD_NUMBER: _ClassVar[int]
    STUN_BIND_REQUEST_FIELD_NUMBER: _ClassVar[int]
    STUN_BIND_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    UDP_PING_REQUEST_FIELD_NUMBER: _ClassVar[int]
    UDP_PING_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    KEY_ROTATION_REQUEST_FIELD_NUMBER: _ClassVar[int]
    KEY_ROTATION_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    ADMIN_AUTH_REQUEST_FIELD_NUMBER: _ClassVar[int]
    ADMIN_AUTH_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    SET_ADMIN_REQUEST_FIELD_NUMBER: _ClassVar[int]
    SET_ADMIN_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    KICK_USER_REQUEST_FIELD_NUMBER: _ClassVar[int]
    KICK_USER_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    BAN_USER_REQUEST_FIELD_NUMBER: _ClassVar[int]
    BAN_USER_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    MOVE_USER_REQUEST_FIELD_NUMBER: _ClassVar[int]
    MOVE_USER_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    CHAT_SEND_FIELD_NUMBER: _ClassVar[int]
    CHAT_BROADCAST_FIELD_NUMBER: _ClassVar[int]
    SET_SERVER_NAME_REQUEST_FIELD_NUMBER: _ClassVar[int]
    SET_SERVER_NAME_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    RENAME_CHANNEL_FIELD_NUMBER: _ClassVar[int]
    RENAME_CHANNEL_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    FILE_LIST_REQUEST_FIELD_NUMBER: _ClassVar[int]
    FILE_LIST_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    FILE_UPLOAD_REQUEST_FIELD_NUMBER: _ClassVar[int]
    FILE_UPLOAD_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    FILE_DELETE_REQUEST_FIELD_NUMBER: _ClassVar[int]
    FILE_DELETE_RESPONSE_FIELD_NUMBER: _ClassVar[int]
    login_request: LoginRequest
    login_response: LoginResponse
    join_channel: JoinChannelRequest
    leave_channel: LeaveChannelRequest
    create_channel: CreateChannelRequest
    delete_channel: DeleteChannelRequest
    channel_list: ChannelListUpdate
    user_joined: UserJoinedChannel
    user_left: UserLeftChannel
    user_speaking: UserSpeaking
    ptt_toggle: PttToggle
    mute_toggle: UserMuteToggle
    server_message: ServerMessage
    stun_bind_request: StunBindRequest
    stun_bind_response: StunBindResponse
    udp_ping_request: UdpPingRequest
    udp_ping_response: UdpPingResponse
    key_rotation_request: KeyRotationRequest
    key_rotation_response: KeyRotationResponse
    admin_auth_request: AdminAuthRequest
    admin_auth_response: AdminAuthResponse
    set_admin_request: SetAdminRequest
    set_admin_response: SetAdminResponse
    kick_user_request: KickUserRequest
    kick_user_response: KickUserResponse
    ban_user_request: BanUserRequest
    ban_user_response: BanUserResponse
    move_user_request: MoveUserRequest
    move_user_response: MoveUserResponse
    chat_send: ChatSendRequest
    chat_broadcast: ChatBroadcast
    set_server_name_request: SetServerNameRequest
    set_server_name_response: SetServerNameResponse
    rename_channel: RenameChannelRequest
    rename_channel_response: RenameChannelResponse
    file_list_request: FileListRequest
    file_list_response: FileListResponse
    file_upload_request: FileUploadRequest
    file_upload_response: FileUploadResponse
    file_delete_request: FileDeleteRequest
    file_delete_response: FileDeleteResponse
    def __init__(self, login_request: _Optional[_Union[LoginRequest, _Mapping]] = ..., login_response: _Optional[_Union[LoginResponse, _Mapping]] = ..., join_channel: _Optional[_Union[JoinChannelRequest, _Mapping]] = ..., leave_channel: _Optional[_Union[LeaveChannelRequest, _Mapping]] = ..., create_channel: _Optional[_Union[CreateChannelRequest, _Mapping]] = ..., delete_channel: _Optional[_Union[DeleteChannelRequest, _Mapping]] = ..., channel_list: _Optional[_Union[ChannelListUpdate, _Mapping]] = ..., user_joined: _Optional[_Union[UserJoinedChannel, _Mapping]] = ..., user_left: _Optional[_Union[UserLeftChannel, _Mapping]] = ..., user_speaking: _Optional[_Union[UserSpeaking, _Mapping]] = ..., ptt_toggle: _Optional[_Union[PttToggle, _Mapping]] = ..., mute_toggle: _Optional[_Union[UserMuteToggle, _Mapping]] = ..., server_message: _Optional[_Union[ServerMessage, _Mapping]] = ..., stun_bind_request: _Optional[_Union[StunBindRequest, _Mapping]] = ..., stun_bind_response: _Optional[_Union[StunBindResponse, _Mapping]] = ..., udp_ping_request: _Optional[_Union[UdpPingRequest, _Mapping]] = ..., udp_ping_response: _Optional[_Union[UdpPingResponse, _Mapping]] = ..., key_rotation_request: _Optional[_Union[KeyRotationRequest, _Mapping]] = ..., key_rotation_response: _Optional[_Union[KeyRotationResponse, _Mapping]] = ..., admin_auth_request: _Optional[_Union[AdminAuthRequest, _Mapping]] = ..., admin_auth_response: _Optional[_Union[AdminAuthResponse, _Mapping]] = ..., set_admin_request: _Optional[_Union[SetAdminRequest, _Mapping]] = ..., set_admin_response: _Optional[_Union[SetAdminResponse, _Mapping]] = ..., kick_user_request: _Optional[_Union[KickUserRequest, _Mapping]] = ..., kick_user_response: _Optional[_Union[KickUserResponse, _Mapping]] = ..., ban_user_request: _Optional[_Union[BanUserRequest, _Mapping]] = ..., ban_user_response: _Optional[_Union[BanUserResponse, _Mapping]] = ..., move_user_request: _Optional[_Union[MoveUserRequest, _Mapping]] = ..., move_user_response: _Optional[_Union[MoveUserResponse, _Mapping]] = ..., chat_send: _Optional[_Union[ChatSendRequest, _Mapping]] = ..., chat_broadcast: _Optional[_Union[ChatBroadcast, _Mapping]] = ..., set_server_name_request: _Optional[_Union[SetServerNameRequest, _Mapping]] = ..., set_server_name_response: _Optional[_Union[SetServerNameResponse, _Mapping]] = ..., rename_channel: _Optional[_Union[RenameChannelRequest, _Mapping]] = ..., rename_channel_response: _Optional[_Union[RenameChannelResponse, _Mapping]] = ..., file_list_request: _Optional[_Union[FileListRequest, _Mapping]] = ..., file_list_response: _Optional[_Union[FileListResponse, _Mapping]] = ..., file_upload_request: _Optional[_Union[FileUploadRequest, _Mapping]] = ..., file_upload_response: _Optional[_Union[FileUploadResponse, _Mapping]] = ..., file_delete_request: _Optional[_Union[FileDeleteRequest, _Mapping]] = ..., file_delete_response: _Optional[_Union[FileDeleteResponse, _Mapping]] = ...) -> None: ...
