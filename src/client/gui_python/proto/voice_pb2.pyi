from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional

DESCRIPTOR: _descriptor.FileDescriptor

class VoicePacketHeader(_message.Message):
    __slots__ = ("sequence_number", "sender_id", "channel_id", "timestamp", "last_frame", "fec_payload_size", "nonce", "auth_tag", "tcp_tunnel")
    SEQUENCE_NUMBER_FIELD_NUMBER: _ClassVar[int]
    SENDER_ID_FIELD_NUMBER: _ClassVar[int]
    CHANNEL_ID_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    LAST_FRAME_FIELD_NUMBER: _ClassVar[int]
    FEC_PAYLOAD_SIZE_FIELD_NUMBER: _ClassVar[int]
    NONCE_FIELD_NUMBER: _ClassVar[int]
    AUTH_TAG_FIELD_NUMBER: _ClassVar[int]
    TCP_TUNNEL_FIELD_NUMBER: _ClassVar[int]
    sequence_number: int
    sender_id: int
    channel_id: int
    timestamp: int
    last_frame: bool
    fec_payload_size: int
    nonce: bytes
    auth_tag: bytes
    tcp_tunnel: bool
    def __init__(self, sequence_number: _Optional[int] = ..., sender_id: _Optional[int] = ..., channel_id: _Optional[int] = ..., timestamp: _Optional[int] = ..., last_frame: bool = ..., fec_payload_size: _Optional[int] = ..., nonce: _Optional[bytes] = ..., auth_tag: _Optional[bytes] = ..., tcp_tunnel: bool = ...) -> None: ...
