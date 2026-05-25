from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class ResultCode(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = ()
    OK: _ClassVar[ResultCode]
    ERROR_UNKNOWN: _ClassVar[ResultCode]
    ERROR_AUTH_FAILED: _ClassVar[ResultCode]
    ERROR_PERMISSION_DENIED: _ClassVar[ResultCode]
    ERROR_CHANNEL_NOT_FOUND: _ClassVar[ResultCode]
    ERROR_ALREADY_IN_CHANNEL: _ClassVar[ResultCode]
    ERROR_INVALID_REQUEST: _ClassVar[ResultCode]
    ERROR_CONNECTION_FAILED: _ClassVar[ResultCode]
    ERROR_TIMEOUT: _ClassVar[ResultCode]
    ERROR_CRYPTO_ERROR: _ClassVar[ResultCode]
    ERROR_NAT_TRAVERSAL_FAILED: _ClassVar[ResultCode]
    ERROR_USER_NOT_FOUND: _ClassVar[ResultCode]

class UserStatus(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = ()
    OFFLINE: _ClassVar[UserStatus]
    ONLINE: _ClassVar[UserStatus]
    AWAY: _ClassVar[UserStatus]
    MUTED: _ClassVar[UserStatus]
    DEAFENED: _ClassVar[UserStatus]
OK: ResultCode
ERROR_UNKNOWN: ResultCode
ERROR_AUTH_FAILED: ResultCode
ERROR_PERMISSION_DENIED: ResultCode
ERROR_CHANNEL_NOT_FOUND: ResultCode
ERROR_ALREADY_IN_CHANNEL: ResultCode
ERROR_INVALID_REQUEST: ResultCode
ERROR_CONNECTION_FAILED: ResultCode
ERROR_TIMEOUT: ResultCode
ERROR_CRYPTO_ERROR: ResultCode
ERROR_NAT_TRAVERSAL_FAILED: ResultCode
ERROR_USER_NOT_FOUND: ResultCode
OFFLINE: UserStatus
ONLINE: UserStatus
AWAY: UserStatus
MUTED: UserStatus
DEAFENED: UserStatus

class UserIdMsg(_message.Message):
    __slots__ = ("id",)
    ID_FIELD_NUMBER: _ClassVar[int]
    id: int
    def __init__(self, id: _Optional[int] = ...) -> None: ...

class ChannelIdMsg(_message.Message):
    __slots__ = ("id",)
    ID_FIELD_NUMBER: _ClassVar[int]
    id: int
    def __init__(self, id: _Optional[int] = ...) -> None: ...

class UserInfo(_message.Message):
    __slots__ = ("id", "username", "status", "muted", "deafened", "group_id")
    ID_FIELD_NUMBER: _ClassVar[int]
    USERNAME_FIELD_NUMBER: _ClassVar[int]
    STATUS_FIELD_NUMBER: _ClassVar[int]
    MUTED_FIELD_NUMBER: _ClassVar[int]
    DEAFENED_FIELD_NUMBER: _ClassVar[int]
    GROUP_ID_FIELD_NUMBER: _ClassVar[int]
    id: int
    username: str
    status: UserStatus
    muted: bool
    deafened: bool
    group_id: int
    def __init__(self, id: _Optional[int] = ..., username: _Optional[str] = ..., status: _Optional[_Union[UserStatus, str]] = ..., muted: bool = ..., deafened: bool = ..., group_id: _Optional[int] = ...) -> None: ...

class ChannelInfo(_message.Message):
    __slots__ = ("id", "name", "parent_id", "children", "users")
    ID_FIELD_NUMBER: _ClassVar[int]
    NAME_FIELD_NUMBER: _ClassVar[int]
    PARENT_ID_FIELD_NUMBER: _ClassVar[int]
    CHILDREN_FIELD_NUMBER: _ClassVar[int]
    USERS_FIELD_NUMBER: _ClassVar[int]
    id: int
    name: str
    parent_id: int
    children: _containers.RepeatedCompositeFieldContainer[ChannelInfo]
    users: _containers.RepeatedCompositeFieldContainer[UserInfo]
    def __init__(self, id: _Optional[int] = ..., name: _Optional[str] = ..., parent_id: _Optional[int] = ..., children: _Optional[_Iterable[_Union[ChannelInfo, _Mapping]]] = ..., users: _Optional[_Iterable[_Union[UserInfo, _Mapping]]] = ...) -> None: ...
