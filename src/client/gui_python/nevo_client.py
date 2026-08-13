import struct
import socket
import threading
import time
import traceback
import logging
import secrets
from enum import IntEnum
from typing import Optional, Callable

import sys
import os
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# 配置日志记录器
logger = logging.getLogger("nevo_client")

# ★ 启动标记 — 如果这行没出现在日志里，说明运行的代码不是这个文件
print(f"[BOOTSTRAP] nevo_client.py loaded from: {__file__}")

def _dlog(msg: str):
    """调试日志（DEBUG 级别）。

    不写独立文件（消除打包目录写权限问题与不必要的路径面），
    统一走 logging，由 logging_setup 的文件/控制台 handler 输出。
    """
    logger.debug("%s", msg)

CLIENT_BUILD_VERSION = "2026.07.04-fix-v3"
_dlog(f"=== nevo_client.py loaded (build {CLIENT_BUILD_VERSION}) from {__file__} ===")

from nevo_wire import (
    MessageType as WireMessageType,
    ResultCode,
    LoginRequest, LoginResponse,
    JoinChannelRequest, LeaveChannelRequest,
    CreateChannelRequest, DeleteChannelRequest, RenameChannelRequest,
    PttToggle, UserMuteToggle, SpeakingState,
    ChatSendRequest, ChatBroadcast,
    AdminAuthRequest, AdminAuthResponse,
    SetAdminRequest, SetAdminResponse,
    KickUserRequest, KickUserResponse,
    BanUserRequest, BanUserResponse,
    MoveUserRequest, MoveUserResponse,
    SetServerNameRequest, SetServerNameResponse,
    FileListRequest, FileListResponse,
    FileUploadRequest, FileUploadResponse,
    FileUploadChunkRequest, FileUploadChunkAck,
    FileDownloadRequest, FileDownloadResponse,
    FileDeleteRequest, FileDeleteResponse,
    ScreenShareStartRequest, ScreenShareStopRequest, ScreenShareState,
    ChannelListUpdate, UserJoinedChannel, UserLeftChannel,
    UserSpeaking, ServerMessage,
    KeyRotationRequest, KeyRotationResponse,
    StunBindRequest, StunBindResponse,
    UdpPingRequest, UdpPingResponse,
    VideoProfile,
    VideoCallRequest, VideoCallResponse, VideoCallHangup, VideoCallProfileUpdate,
    serialize_control_message, deserialize_control_message,
    MESSAGE_TYPE_MAP, CASE_TO_DESERIALIZER,
    # 文件传输数据通道（分片）
    encode_file_chunk_marker, decode_file_chunk_marker,
    decode_file_fetch_marker, is_file_transfer_marker,
    pick_chat_chunk_size, FileChunkAssembler,
    FILE_WIRE_CHUNK_SIZE, FILE_MAX_SIZE_BYTES,
    FILE_CHUNK_MARKER_PREFIX, FILE_FETCH_MARKER_PREFIX,
)
import socket as _socket

try:
    from nacl.public import PrivateKey as _PrivateKey, SealedBox as _SealedBox
    _HAS_SEALED_BOX = True
except Exception as _sealed_import_err:
    _PrivateKey = None
    _SealedBox = None
    _HAS_SEALED_BOX = False
    try:
        _dlog(f"[KEY_EXCHANGE] nacl.public import FAILED: {_sealed_import_err!r}")
    except Exception:
        pass


class ClientState(IntEnum):
    Disconnected = 0
    Connecting = 1
    Connected = 2
    InChannel = 3


class VideoCallState(IntEnum):
    """一对一视频通话状态机。"""
    Idle = 0
    Calling = 1          # 已发送请求，等待响应
    Ringing = 2          # 收到来电，等待用户接听/拒绝
    Connecting = 3       # 已接受/对方已接受，准备建立媒体
    Connected = 4        # 媒体通道已建立
    Ended = 5            # 通话已结束


TCP_HEADER_SIZE = 12
TCP_MAX_PAYLOAD_SIZE = 1024 * 1024

# TCP 语音帧类型（与服务端 TcpVoiceTunnel::TCP_VOICE_FRAME_TYPE 一致）
# 外网/NAT 场景（frp 内网穿透）UDP 回程不可靠，媒体帧经 TCP 控制连接传输
TCP_VOICE_FRAME_TYPE = 0xFF

# ============================================================
# 文件传输数据通道常量
# ============================================================
# 聊天分片发送间隔（秒）：避免短时间向服务端灌入数千条聊天消息
FILE_CHUNK_SEND_INTERVAL = 0.005
# 取回请求等待时间 / 最大重试次数
FILE_FETCH_TIMEOUT = 8.0
FILE_FETCH_MAX_ATTEMPTS = 2
# 同一文件的再次响应冷却（秒）：防止多个请求方引发重复广播
FILE_SERVE_COOLDOWN = 5.0
# 重组缓冲区数量上限（超出丢弃最旧的不完整会话）
FILE_MAX_PENDING_ASSEMBLERS = 16
# 单文件分片数上限（100MB / 最小分片 1KB ≈ 100k，留余量）
FILE_MAX_CHUNK_COUNT = 120000

# 服务端缺口提示（详见 handleFileDownloadRequest 相关注释）
FILE_FETCH_FAILED_MESSAGE = (
    "File unavailable: the server does not store file content yet "
    "(no FileDownload support), and no client in the channel holds this file."
)


def get_file_cache_dir() -> str:
    """本地文件缓存目录（与 views/chat_widget._ImageLabel 共用）。

    约定：<gui_python>/_image_cache；打包运行时为 <exe 目录>/_image_cache。
    缓存文件命名 <file_id><ext>，按前缀 <file_id>. 查找。
    """
    if getattr(sys, "frozen", False):
        base = os.path.join(os.path.dirname(sys.executable), "_image_cache")
    else:
        base = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_image_cache")
    os.makedirs(base, exist_ok=True)
    return base


def get_cached_file_path(file_id) -> Optional[str]:
    """按 file_id 前缀查找缓存文件路径；未缓存返回 None。"""
    d = get_file_cache_dir()
    prefix = f"{file_id}."
    try:
        for name in os.listdir(d):
            if name.startswith(prefix) or name == str(file_id):
                return os.path.join(d, name)
    except OSError:
        pass
    return None


def cache_source_file(file_id, source_path: str) -> str:
    """把本地源文件复制进缓存目录（<file_id><ext>），返回缓存路径。"""
    d = get_file_cache_dir()
    ext = os.path.splitext(source_path)[1] or ".png"
    cached = os.path.join(d, f"{file_id}{ext}")
    import shutil
    shutil.copy2(source_path, cached)
    return cached


class NevoClient:

    def __init__(self):
        self._sock: Optional[socket.socket] = None
        self._recv_thread: Optional[threading.Thread] = None
        self._connected = False
        self._state = ClientState.Disconnected
        self._lock = threading.Lock()

        self._session_id = 0
        self._user_id = 0
        self._username = ""
        self._current_channel_id = 0
        self._current_channel_name = ""
        self._is_muted = False
        self._is_deafened = False
        self._is_admin = False

        self._session_key = None
        self._encrypted_session_key = None
        self._server_udp_port = 0
        self._server_video_udp_port = 0
        self._private_key = None
        self._public_key = b""
        self._voice_engine = None
        self._video_engine = None

        self._channels: list[dict] = []
        self._channel_users: list[dict] = []

        # Ping / latency measurement
        self._ping_seq = 0
        self._ping_send_time = 0.0
        self._last_latency_ms = -1
        self._ping_timer: Optional[threading.Timer] = None

        # 一对一视频通话状态
        self._video_call_lock = threading.Lock()
        self._video_call_state = VideoCallState.Idle
        self._current_call_id: int = 0
        self._call_peer_id: int = 0
        self._negotiated_profile: Optional[VideoProfile] = None
        # 呼叫发起后 30 秒无响应的超时定时器
        self._video_call_timeout_timer: Optional[threading.Timer] = None

        # 发送锁：文件分片在后台线程发送，防止与其他线程的 sendall 交错
        self._send_lock = threading.Lock()

        # ---- 文件传输数据通道状态 ----
        self._file_lock = threading.Lock()
        # 本人上传过的文件：file_id(str) -> (source_path, filename)
        # 收到 [NEVOFGET] 取回请求时据此向频道重发分片
        self._owned_files: dict = {}
        # 接收中的重组会话：file_id(str) -> FileChunkAssembler
        self._rx_assemblers: dict = {}
        # 取回请求重试计数与超时定时器：file_id(str) -> ...
        self._fetch_attempts: dict = {}
        self._fetch_timers: dict = {}
        # 最近一次响应取回请求的时间戳（冷却控制）
        self._last_serve_time: dict = {}

        self.on_state_changed: Optional[Callable[[ClientState, ClientState], None]] = None
        self.on_channel_list: Optional[Callable[[list], None]] = None
        self.on_user_joined: Optional[Callable[[dict], None]] = None
        self.on_user_left: Optional[Callable[[int], None]] = None
        self.on_user_speaking: Optional[Callable[[int, bool], None]] = None
        self.on_chat_message: Optional[Callable[[int, str, int, str, int], None]] = None
        self.on_server_message: Optional[Callable[[str], None]] = None
        self.on_error: Optional[Callable[[int, str], None]] = None
        # TCP 语音帧到达（外网/NAT 场景媒体走 TCP 控制连接）：
        # payload = 2B 头长 + protobuf 头 + 加密数据（与 UDP 语音包载荷一致）
        self.on_tcp_voice_frame: Optional[Callable[[bytes], None]] = None
        self.on_admin_auth_result: Optional[Callable[[bool, str], None]] = None
        self.on_admin_action_result: Optional[Callable[[bool, str], None]] = None
        self.on_file_upload_response: Optional[Callable[[int, bool, str], None]] = None
        self.on_file_list: Optional[Callable[[list], None]] = None
        # 文件数据到达并已写盘：file_id(str), cached_path, filename
        self.on_file_received: Optional[Callable[[str, str, str], None]] = None
        # 文件取回/接收失败：file_id(str), message
        self.on_file_error: Optional[Callable[[str, str], None]] = None
        self.on_latency_update: Optional[Callable[[int], None]] = None
        self.on_screen_share_state: Optional[Callable[[int, bool, int, str, int, int], None]] = None

        # 视频通话回调（从网络线程触发，UI 层应通过信号切换到 UI 线程）
        self.on_video_call_incoming: Optional[Callable[[int, int, str, VideoProfile], None]] = None
        self.on_video_call_established: Optional[Callable[[int, int, VideoProfile], None]] = None
        self.on_video_call_ended: Optional[Callable[[int, int], None]] = None
        self.on_video_call_error: Optional[Callable[[int, str], None]] = None

    @property
    def state(self) -> ClientState:
        return self._state

    @property
    def connected(self) -> bool:
        return self._state >= ClientState.Connected

    @property
    def in_channel(self) -> bool:
        return self._state == ClientState.InChannel

    @property
    def username(self) -> str:
        return self._username

    @property
    def user_id(self) -> int:
        return self._user_id

    @property
    def current_channel_id(self) -> int:
        return self._current_channel_id

    @property
    def current_channel_name(self) -> str:
        return self._current_channel_name

    @property
    def is_muted(self) -> bool:
        return self._is_muted

    @property
    def is_deafened(self) -> bool:
        return self._is_deafened

    @property
    def is_admin(self) -> bool:
        return self._is_admin

    @property
    def session_key(self):
        return self._session_key

    @property
    def encrypted_session_key(self):
        return self._encrypted_session_key

    @property
    def server_udp_port(self) -> int:
        return self._server_udp_port

    @server_udp_port.setter
    def server_udp_port(self, val: int):
        self._server_udp_port = val
    
    @property
    def server_video_udp_port(self) -> int:
        return self._server_video_udp_port

    @server_video_udp_port.setter
    def server_video_udp_port(self, val: int):
        self._server_video_udp_port = val

    @property
    def video_call_state(self) -> VideoCallState:
        with self._video_call_lock:
            return self._video_call_state

    @property
    def current_call_id(self) -> int:
        with self._video_call_lock:
            return self._current_call_id

    @property
    def call_peer_id(self) -> int:
        with self._video_call_lock:
            return self._call_peer_id

    @property
    def negotiated_profile(self) -> Optional[VideoProfile]:
        with self._video_call_lock:
            return self._negotiated_profile

    def _set_video_call_state(self, new_state: VideoCallState):
        """线程安全地更新视频通话状态。"""
        with self._video_call_lock:
            old = self._video_call_state
            self._video_call_state = new_state
            logger.debug("[VIDEO_CALL_STATE] %s -> %s", old.name, new_state.name)

    @property
    def channels(self) -> list:
        return self._channels

    @property
    def channel_users(self) -> list:
        return self._channel_users

    def _set_state(self, new_state: ClientState):
        old = self._state
        self._state = new_state
        logger.debug("[STATE] %s -> %s", old.name, new_state.name)
        if self.on_state_changed:
            self.on_state_changed(new_state, old)

    def _fail_connection(self, reason: str):
        """终止连接（密钥协商失败等致命错误），并通知 UI。"""
        _dlog(f"[FATAL] {reason}")
        with self._lock:
            self._connected = False
            if self._sock:
                try:
                    self._sock.close()
                except Exception:
                    pass
                self._sock = None
        self._set_state(ClientState.Disconnected)
        if self.on_error:
            self.on_error(7, reason)

    def _decrypt_session_key(self, encrypted_key: bytes):
        if not encrypted_key or not self._private_key or not _HAS_SEALED_BOX:
            return None
        try:
            key = _SealedBox(self._private_key).decrypt(bytes(encrypted_key))
            if len(key) >= 32:
                return key[:32]
        except Exception:
            return None
        return None

    def _apply_session_key_from_response(self, login_resp) -> bool:
        key = None
        if login_resp.encrypted_session_key:
            key = self._decrypt_session_key(login_resp.encrypted_session_key)
        if key is None:
            # 安全铁律：密钥协商失败即断连，不做任何不安全降级
            logger.error(
                "[SESSION_KEY] 密钥协商失败：encrypted_session_key=%s, pk_len=%s",
                bool(login_resp.encrypted_session_key),
                len(login_resp.server_public_key) if login_resp.server_public_key else 0)
            return False
        self._session_key = key
        self._apply_session_key_to_media()
        return True

    def _apply_session_key_to_media(self):
        if not self._session_key:
            return
        for engine in (self._voice_engine, self._video_engine):
            if engine is not None:
                try:
                    engine.set_session_key(self._session_key)
                except Exception:
                    pass

    def _rotate_session_key_in_media(self):
        if not self._session_key:
            return
        for engine in (self._voice_engine, self._video_engine):
            if engine is not None:
                try:
                    rotate = getattr(engine, "rotate_session_key", None)
                    if rotate:
                        rotate(self._session_key)
                    else:
                        engine.set_session_key(self._session_key)
                except Exception:
                    pass

    def connect(self, host: str, port: int, username: str, password: str = "",
                voice_engine=None, video_engine=None,
                client_udp_port: int = 0, client_video_udp_port: int = 0) -> bool:
        with self._lock:
            if self._state != ClientState.Disconnected:
                return False
            self._username = username
            self._voice_engine = voice_engine
            self._video_engine = video_engine

        self._set_state(ClientState.Connecting)

        try:
            addr_info = socket.getaddrinfo(host, port, socket.AF_UNSPEC,
                                           socket.SOCK_STREAM, socket.IPPROTO_TCP)
            last_err = None
            self._sock = None
            for family, socktype, proto, canonname, sockaddr in addr_info:
                try:
                    self._sock = socket.socket(family, socktype, proto)
                    self._sock.settimeout(10)
                    self._sock.connect(sockaddr)
                    self._sock.settimeout(None)
                    last_err = None
                    break
                except Exception as e:
                    last_err = e
                    if self._sock:
                        try:
                            self._sock.close()
                        except Exception:
                            pass
                        self._sock = None
            if last_err or self._sock is None:
                raise RuntimeError(f"Failed to connect to {host}:{port}: {last_err}")

            # 显式传入的 UDP 端口优先（Web 网关预建媒体套接字场景）；
            # 否则沿用 voice_engine/video_engine 预建的端口
            if not client_udp_port:
                if voice_engine is not None:
                    voice_engine.pre_create_udp_socket()
                    client_udp_port = voice_engine.local_udp_port

            if not client_video_udp_port:
                if video_engine is not None:
                    video_engine.pre_create_udp_socket()
                    client_video_udp_port = video_engine.local_udp_port

            key_exchange_methods = ["X25519"]
            client_public_key = b""
            self._private_key = None
            self._public_key = b""
            if _HAS_SEALED_BOX:
                try:
                    self._private_key = _PrivateKey.generate()
                    self._public_key = bytes(self._private_key.public_key)
                    key_exchange_methods = ["X25519+crypto_box_seal", "X25519"]
                    client_public_key = self._public_key
                except Exception as _kx_err:
                    _dlog(f"[KEY_EXCHANGE] X25519 keygen FAILED: {_kx_err!r}")
                    self._private_key = None
                    self._public_key = b""
            else:
                _dlog("[KEY_EXCHANGE] sealed box unavailable, falling back to plain X25519")

            login_msg = LoginRequest(
                username=username,
                auth_credential=password.encode("utf-8"),
                key_exchange_methods=key_exchange_methods,
                client_public_key=client_public_key,
                client_udp_port=client_udp_port,
                client_video_udp_port=client_video_udp_port,
            )
            self._send_message(WireMessageType.LOGIN_REQUEST, login_msg)

            msg_type, payload = self._read_frame()
            if msg_type != 2:
                raise RuntimeError(f"Expected LoginResponse (type 2), got type {msg_type}")

            _, login_resp = deserialize_control_message(payload)
            if login_resp is None:
                raise RuntimeError("Failed to parse LoginResponse")

            if login_resp.result != ResultCode.OK:
                with self._lock:
                    if self._sock:
                        try:
                            self._sock.close()
                        except Exception:
                            pass
                        self._sock = None
                self._set_state(ClientState.Disconnected)
                return False

            if login_resp.user_info:
                self._user_id = login_resp.user_info.id
                self._is_admin = (login_resp.user_info.group_id == 1)
                _dlog(f"[LOGIN] user_id={self._user_id}, username={login_resp.user_info.username}, is_admin={self._is_admin}")

            if login_resp.encrypted_session_key:
                self._encrypted_session_key = login_resp.encrypted_session_key
            if not self._apply_session_key_from_response(login_resp):
                raise RuntimeError("密钥协商失败，连接已终止")
            if getattr(login_resp, 'server_udp_port', 0):
                self._server_udp_port = login_resp.server_udp_port
            
            if getattr(login_resp, 'server_video_udp_port', 0):
                self._server_video_udp_port = login_resp.server_video_udp_port

            if video_engine is not None and hasattr(login_resp, 'server_video_udp_port'):
                video_port = login_resp.server_video_udp_port
                if video_port > 0:
                    video_engine.set_server_udp(host, video_port)
                if self._session_key:
                    video_engine.set_session_key(self._session_key)
                video_engine.set_user_info(self._user_id, 0)
                video_engine.start_receive()

            with self._lock:
                self._connected = True

            self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
            self._recv_thread.start()

            self._set_state(ClientState.Connected)

            # Start ping timer
            self._start_ping_timer()

            return True

        except (ConnectionError, OSError, RuntimeError) as e:
            with self._lock:
                if self._sock:
                    try:
                        self._sock.close()
                    except Exception:
                        pass
                    self._sock = None
                self._connected = False
            self._set_state(ClientState.Disconnected)
            if self.on_error:
                self.on_error(7, str(e))
            return False
        except Exception as e:
            with self._lock:
                if self._sock:
                    try:
                        self._sock.close()
                    except Exception:
                        pass
                    self._sock = None
                self._connected = False
            self._set_state(ClientState.Disconnected)
            if self.on_error:
                self.on_error(7, str(e))
            return False

    def disconnect(self):
        self._stop_ping_timer()

        # 清理视频通话状态
        self._reset_video_call_state(reason=1)

        with self._lock:
            if self._state == ClientState.Disconnected:
                return
            self._connected = False
            self._current_channel_id = 0
            self._current_channel_name = ""
            self._channel_users.clear()
            self._is_muted = False
            self._is_deafened = False
            if self._sock:
                try:
                    self._sock.close()
                except Exception:
                    pass
                self._sock = None

        if self._recv_thread and self._recv_thread.is_alive():
            self._recv_thread.join(timeout=5.0)
            self._recv_thread = None

        # 清理文件传输状态（保留 _owned_files：缓存文件仍在磁盘上，重连后仍可响应取回请求）
        with self._file_lock:
            self._rx_assemblers.clear()
            self._fetch_attempts.clear()
            for timer in self._fetch_timers.values():
                timer.cancel()
            self._fetch_timers.clear()
            self._last_serve_time.clear()

        self._set_state(ClientState.Disconnected)

    def join_channel(self, channel_id: int) -> bool:
        if not self.connected:
            return False
        try:
            msg = JoinChannelRequest(channel_id=channel_id)
            self._send_message(WireMessageType.JOIN_CHANNEL_REQUEST, msg)
            self._current_channel_id = channel_id
            for ch in self._channels:
                if ch["id"] == channel_id:
                    self._current_channel_name = ch.get("name", "")
                    break
            self._set_state(ClientState.InChannel)
            return True
        except Exception:
            return False

    def leave_channel(self):
        if not self.in_channel:
            return
        try:
            msg = LeaveChannelRequest()
            self._send_message(WireMessageType.LEAVE_CHANNEL_REQUEST, msg)
        except Exception:
            pass
        self._current_channel_id = 0
        self._current_channel_name = ""
        self._channel_users.clear()
        self._set_state(ClientState.Connected)

    def set_muted(self, muted: bool):
        if not self.connected:
            return
        self._is_muted = muted
        try:
            msg = UserMuteToggle(muted=muted)
            self._send_message(WireMessageType.USER_MUTE_TOGGLE, msg)
        except Exception:
            pass

    def set_deafened(self, deafened: bool):
        if not self.connected:
            return
        self._is_deafened = deafened
        if deafened:
            self._is_muted = True
            try:
                msg = UserMuteToggle(muted=True)
                self._send_message(WireMessageType.USER_MUTE_TOGGLE, msg)
            except Exception:
                pass

    def send_chat(self, text: str, channel_id: int = 0):
        if not self.connected:
            return
        try:
            msg = ChatSendRequest(channel_id=channel_id, text=text)
            self._send_message(WireMessageType.CHAT_SEND_REQUEST, msg)
        except Exception:
            pass

    def send_speaking_state(self, speaking: bool):
        if not self.connected:
            return
        try:
            # 服务端 PacketCodec.cpp 的 CASE_DECODERS 只注册了 case_value 36
            # (decodeSpeakingState)，它会读取一个 bool 并写入 msg.user_speaking.speaking。
            # ClientSession.cpp:274 检测 msg.has_user_speaking() 后用会话 user_id 广播。
            # case_value 10 (USER_SPEAKING) 未在解码表中注册，会被服务端丢弃，
            # 因此必须使用 SPEAKING_STATE(36) 上报。
            msg = SpeakingState(speaking=speaking)
            self._send_message(WireMessageType.SPEAKING_STATE, msg)
        except Exception:
            pass

    def send_voice_frame_tcp(self, payload: bytes):
        """通过 TCP 控制连接发送语音帧（TCP_VOICE_FRAME_TYPE）。

        外网/NAT 场景（frp 内网穿透）UDP 回程不可靠，
        媒体帧随登录同一条 TCP 连接传输，回程天然可靠。
        payload = 2B 头长 + protobuf 头 + 加密数据（与 UDP 语音包载荷一致）。
        """
        if not self._sock or not self._connected:
            return
        if len(payload) > TCP_MAX_PAYLOAD_SIZE:
            return
        try:
            header = struct.pack(">III", len(payload), TCP_VOICE_FRAME_TYPE, 0)
            with self._send_lock:
                self._sock.sendall(header + payload)
        except Exception:
            pass

    def send_admin_auth(self, password: str):
        logger.debug("[ADMIN] send_admin_auth called, pwd_len=%d", len(password))
        try:
            msg = AdminAuthRequest(password=password)
            self._send_message(WireMessageType.ADMIN_AUTH_REQUEST, msg)
            logger.debug("[ADMIN] send_admin_auth: message sent successfully")
        except Exception as e:
            logger.debug("[ADMIN] send_admin_auth exception: %s", e, exc_info=True)

    def send_create_channel(self, name: str, parent_id: int = 0):
        try:
            msg = CreateChannelRequest(parent_id=parent_id, name=name)
            self._send_message(WireMessageType.CREATE_CHANNEL_REQUEST, msg)
        except Exception:
            pass

    def send_delete_channel(self, channel_id: int):
        try:
            msg = DeleteChannelRequest(channel_id=channel_id)
            self._send_message(WireMessageType.DELETE_CHANNEL_REQUEST, msg)
        except Exception:
            pass

    def send_rename_channel(self, channel_id: int, new_name: str):
        try:
            msg = RenameChannelRequest(channel_id=channel_id, new_name=new_name)
            self._send_message(WireMessageType.RENAME_CHANNEL_REQUEST, msg)
        except Exception:
            pass

    def send_set_server_name(self, server_name: str):
        try:
            msg = SetServerNameRequest(server_name=server_name)
            self._send_message(WireMessageType.SET_SERVER_NAME_REQUEST, msg)
        except Exception:
            pass

    def send_set_admin(self, user_id: int, set_admin: bool):
        try:
            msg = SetAdminRequest(user_id=user_id, set_admin=set_admin)
            self._send_message(WireMessageType.SET_ADMIN_REQUEST, msg)
        except Exception:
            pass

    def send_kick_user(self, user_id: int, reason: str = ""):
        try:
            msg = KickUserRequest(user_id=user_id, reason=reason)
            self._send_message(WireMessageType.KICK_USER_REQUEST, msg)
        except Exception:
            pass

    def send_ban_user(self, user_id: int, reason: str = "", expires_at: int = 0):
        try:
            msg = BanUserRequest(user_id=user_id, reason=reason, expires_at=expires_at)
            self._send_message(WireMessageType.BAN_USER_REQUEST, msg)
        except Exception:
            pass

    def send_move_user(self, user_id: int, channel_id: int):
        try:
            msg = MoveUserRequest(user_id=user_id, channel_id=channel_id)
            self._send_message(WireMessageType.MOVE_USER_REQUEST, msg)
        except Exception:
            pass

    def send_file_list_request(self, channel_id: int):
        try:
            msg = FileListRequest(channel_id=channel_id)
            self._send_message(WireMessageType.FILE_LIST_REQUEST, msg)
        except Exception:
            pass

    def send_file_upload_request(self, channel_id: int, filename: str, file_size: int):
        try:
            msg = FileUploadRequest(channel_id=channel_id, filename=filename, file_size=file_size)
            self._send_message(WireMessageType.FILE_UPLOAD_REQUEST, msg)
        except Exception:
            pass

    def send_file_delete_request(self, file_id: int):
        try:
            msg = FileDeleteRequest(file_id=file_id)
            self._send_message(WireMessageType.FILE_DELETE_REQUEST, msg)
        except Exception:
            pass

    # ============================================================
    # 文件传输数据通道（真实字节流）
    #
    # 服务端现状（只读调研结论，勿改服务端）：
    #  - FileUploadRequest 仅含元数据，服务端 handleFileUploadRequest 只
    #    创建 DB 记录并返回 file_id，从不接收/写盘文件字节；
    #  - proto 无 FileDownloadRequest/FileDownloadResponse 消息体，服务端
    #    decodeCustomWirePayload 未注册 44/45/46/47（直接丢弃），因此
    #    wire 分片消息目前无法到达服务端；
    #  - 唯一现成的字节承载通道是 ChatSend/ChatBroadcast（文本 ≤ 4096 字节）。
    #
    # 客户端策略（协议完整实现 + 当前可用的真实通道）：
    #  - 实时字节流走"聊天中继分片标记"（base64，2400B/片），同频道客户端
    #    重组写盘（见 _handle_chat_broadcast 拦截逻辑）；
    #  - wire 分片消息（44/45/46/47）已按追加编号实现编解码与发送方法，
    #    服务端补齐对应处理后可无缝切换到服务端存储通道；
    #  - 取回（下载）：先发 wire FileDownloadRequest（未来服务端支持时生效），
    #    同时向频道广播 [NEVOFGET] 标记，由持有文件的客户端重发分片。
    # ============================================================

    def register_owned_file(self, file_id: int, source_path: str, filename: str = ""):
        """登记本人上传的文件，并在后台把源文件复制进本地缓存目录。

        上传成功后调用。之后：
          - 本人 UI 可直接从缓存读取（图片即时显示、文件可保存）；
          - 频道内其他客户端发来取回请求时，由本客户端重发分片。
        """
        fid = str(file_id)
        with self._file_lock:
            self._owned_files[fid] = (source_path, filename or os.path.basename(source_path))

        def _copy():
            try:
                cached = cache_source_file(fid, source_path)
                with self._file_lock:
                    entry = self._owned_files.get(fid)
                    if entry:
                        self._owned_files[fid] = (cached, entry[1])
            except Exception as e:
                _dlog(f"[FILE_TX] cache owned file {fid} failed: {e!r}")

        threading.Thread(target=_copy, daemon=True).start()

    def upload_file_data(self, file_id: int, source_path: str, filename: str = ""):
        """把文件真实字节流通过聊天中继分片发送给频道内其他客户端。

        在后台线程执行，逐片发送不阻塞 UI。当前服务端不接收字节流，
        因此这是现有协议下唯一可用的"真实数据通道"。
        """
        fid = str(file_id)
        if not filename:
            filename = os.path.basename(source_path)

        def _run():
            if not self.connected:
                return
            # 频道内只有自己时无需发送（对方加入后可主动取回）
            with self._file_lock:
                peers = [u for u in self._channel_users if u.get("id") != self._user_id]
            if not peers:
                _dlog(f"[FILE_TX] no peers in channel, skip chunk broadcast for {fid}")
                return
            self._send_file_chunks(fid, source_path, filename, via_chat=True)

        threading.Thread(target=_run, daemon=True).start()

    def upload_file_data_wire(self, file_id: int, source_path: str, filename: str = ""):
        """通过 wire FileUploadChunkRequest(44) 逐片上传（16KB/片）。

        前瞻实现：当前服务端 decodeCustomWirePayload 未注册 case 44，
        分片会被静默丢弃；服务端实现"接收分片 + 写盘 + FileUploadChunkAck"
        后此路径即可启用，客户端无需再改。
        """
        fid = str(file_id)

        def _run():
            if not self.connected:
                return
            try:
                total = os.path.getsize(source_path)
            except OSError as e:
                _dlog(f"[FILE_TX] wire upload stat failed {fid}: {e!r}")
                return
            chunk_size = FILE_WIRE_CHUNK_SIZE
            chunk_count = (total + chunk_size - 1) // chunk_size if total else 0
            try:
                with open(source_path, "rb") as fh:
                    index = 0
                    while True:
                        piece = fh.read(chunk_size)
                        if not piece:
                            break
                        msg = FileUploadChunkRequest(
                            file_id=int(fid), chunk_index=index,
                            chunk_count=chunk_count, data=piece,
                        )
                        self._send_message(WireMessageType.FILE_UPLOAD_CHUNK_REQUEST, msg)
                        index += 1
                        time.sleep(FILE_CHUNK_SEND_INTERVAL)
            except (OSError, ConnectionError) as e:
                _dlog(f"[FILE_TX] wire upload failed {fid}: {e!r}")

        threading.Thread(target=_run, daemon=True).start()

    def send_file_download_request(self, file_id: int):
        """向服务端请求文件数据（wire FileDownloadRequest=46）。

        前瞻实现：服务端目前会丢弃该消息；服务端实现后返回
        FileDownloadResponse(47) 分片，客户端自动重组（见
        _handle_file_download_response）。
        """
        try:
            msg = FileDownloadRequest(file_id=file_id)
            self._send_message(WireMessageType.FILE_DOWNLOAD_REQUEST, msg)
        except Exception:
            pass

    def download_file(self, file_id: int):
        """取回文件：点击图片/文件卡片时调用。

        1) 本地缓存已存在 → 直接通知 UI；
        2) 否则向服务端发 wire 下载请求（未来生效）+ 向频道广播取回标记，
           由持有文件的客户端重发分片；超时重试一次后给出友好提示。
        """
        fid = str(file_id)
        cached = get_cached_file_path(fid)
        if cached:
            self._notify_file_received(fid, cached, os.path.basename(cached))
            return

        with self._file_lock:
            if fid in self._fetch_timers:
                return  # 已有取回请求在进行中

        self.send_file_download_request(int(fid))
        try:
            self.send_chat(f"{FILE_FETCH_MARKER_PREFIX}{fid}]")
        except Exception:
            pass
        self._schedule_fetch_retry(fid)

    def _schedule_fetch_retry(self, fid: str):
        def _on_timeout():
            cached = get_cached_file_path(fid)
            with self._file_lock:
                assembler = self._rx_assemblers.get(fid)
                complete = bool(assembler and assembler.complete)
                attempts = self._fetch_attempts.get(fid, 0)
                self._fetch_timers.pop(fid, None)
            if cached or complete:
                return
            if self.connected and attempts < FILE_FETCH_MAX_ATTEMPTS:
                _dlog(f"[FILE_TX] retry fetch for {fid} (attempt {attempts + 1})")
                try:
                    self.send_chat(f"{FILE_FETCH_MARKER_PREFIX}{fid}]")
                except Exception:
                    pass
                self._schedule_fetch_retry(fid)
            else:
                with self._file_lock:
                    self._fetch_attempts.pop(fid, None)
                if self.on_file_error:
                    self.on_file_error(fid, FILE_FETCH_FAILED_MESSAGE)

        timer = threading.Timer(FILE_FETCH_TIMEOUT, _on_timeout)
        timer.daemon = True
        with self._file_lock:
            self._fetch_attempts[fid] = self._fetch_attempts.get(fid, 0) + 1
            self._fetch_timers[fid] = timer
        timer.start()

    # ---- 发送侧 ----

    def _send_file_chunks(self, fid: str, source_path: str, filename: str,
                          via_chat: bool = True):
        """从磁盘流式读取文件并逐片发送（聊天中继标记）。

        chunk 0 附带 base64 文件名；片间短暂休眠避免灌爆服务端。
        """
        try:
            total = os.path.getsize(source_path)
        except OSError as e:
            _dlog(f"[FILE_TX] stat failed {fid}: {e!r}")
            return
        if total <= 0 or total > FILE_MAX_SIZE_BYTES:
            _dlog(f"[FILE_TX] skip {fid}: size {total} out of range")
            return
        chunk_size = pick_chat_chunk_size(filename) if via_chat else FILE_WIRE_CHUNK_SIZE
        chunk_count = (total + chunk_size - 1) // chunk_size
        try:
            with open(source_path, "rb") as fh:
                index = 0
                while self.connected:
                    piece = fh.read(chunk_size)
                    if not piece:
                        break
                    text = encode_file_chunk_marker(
                        int(fid), chunk_count, index, piece,
                        filename if index == 0 else "",
                    )
                    if via_chat:
                        self.send_chat(text)
                    else:
                        msg = FileUploadChunkRequest(
                            file_id=int(fid), chunk_index=index,
                            chunk_count=chunk_count, data=piece,
                        )
                        self._send_message(WireMessageType.FILE_UPLOAD_CHUNK_REQUEST, msg)
                    index += 1
                    if index % 40 == 0:
                        _dlog(f"[FILE_TX] {fid}: sent {index}/{chunk_count} chunks")
                    time.sleep(FILE_CHUNK_SEND_INTERVAL)
        except (OSError, ConnectionError, ValueError) as e:
            _dlog(f"[FILE_TX] send chunks failed {fid}: {e!r}")
        _dlog(f"[FILE_TX] {fid}: chunk broadcast finished ({chunk_count} chunks)")

    def _serve_file_if_owner(self, fid: str):
        """收到取回标记后，若本人是上传者则重发分片（带冷却）。"""
        with self._file_lock:
            entry = self._owned_files.get(fid)
            if not entry:
                return
            now = time.time()
            if now - self._last_serve_time.get(fid, 0.0) < FILE_SERVE_COOLDOWN:
                return
            self._last_serve_time[fid] = now
        source_path, filename = entry
        if not os.path.exists(source_path):
            with self._file_lock:
                self._owned_files.pop(fid, None)
            return
        _dlog(f"[FILE_TX] serving fetch for {fid} from {source_path}")
        threading.Thread(
            target=self._send_file_chunks,
            args=(fid, source_path, filename, True),
            daemon=True,
        ).start()

    # ---- 接收侧 ----

    def _handle_file_transfer_marker(self, sender_id: int, text: str):
        """聊天广播中的文件传输标记拦截处理（不进入聊天 UI）。"""
        fetch_id = decode_file_fetch_marker(text)
        if fetch_id is not None:
            if sender_id != self._user_id:
                self._serve_file_if_owner(str(fetch_id))
            return

        chunk = decode_file_chunk_marker(text)
        if chunk is None:
            return
        # 服务端会把聊天广播回显给发送者本人，跳过自己的分片
        if sender_id == self._user_id:
            return
        self._on_rx_chunk(
            str(chunk["file_id"]), chunk["chunk_count"],
            chunk["chunk_index"], chunk["data"], chunk["filename"],
        )

    def _on_rx_chunk(self, fid: str, chunk_count: int, chunk_index: int,
                     data: bytes, filename: str):
        if chunk_count <= 0 or chunk_count > FILE_MAX_CHUNK_COUNT:
            return
        if get_cached_file_path(fid):
            return  # 文件已完整缓存
        with self._file_lock:
            assembler = self._rx_assemblers.get(fid)
            # wire(服务端)分片优先：一旦走服务端通道，忽略聊天分片
            if assembler is not None and getattr(assembler, "wire_source", False):
                return
            if assembler is None or assembler.chunk_count != chunk_count:
                if len(self._rx_assemblers) >= FILE_MAX_PENDING_ASSEMBLERS:
                    self._rx_assemblers.pop(next(iter(self._rx_assemblers)), None)
                assembler = FileChunkAssembler(fid, chunk_count)
                assembler.wire_source = False
                self._rx_assemblers[fid] = assembler
            done = assembler.add_chunk(chunk_index, data, filename)
        if done:
            self._finalize_rx(fid)

    def _finalize_rx(self, fid: str):
        with self._file_lock:
            assembler = self._rx_assemblers.pop(fid, None)
            timer = self._fetch_timers.pop(fid, None)
            self._fetch_attempts.pop(fid, None)
        if timer:
            timer.cancel()
        if not assembler or not assembler.complete:
            return
        data = assembler.data
        if len(data) > FILE_MAX_SIZE_BYTES:
            if self.on_file_error:
                self.on_file_error(fid, "File too large")
            return
        ext = os.path.splitext(assembler.filename or "")[1]
        if not ext or len(ext) > 16 or not all(c.isalnum() or c in "._-" for c in ext):
            ext = ".bin"
        cache_dir = get_file_cache_dir()
        path = os.path.join(cache_dir, f"{fid}{ext}")
        try:
            # 路径校验：规范化后必须仍位于缓存目录内（防路径穿越），校验通过才写入
            cache_dir_resolved = os.path.realpath(cache_dir)
            path_resolved = os.path.realpath(path)
            if path_resolved.startswith(cache_dir_resolved + os.sep):
                Path(path_resolved).write_bytes(data)
        except OSError as e:
            _dlog(f"[FILE_TX] write {fid} failed: {e!r}")
            if self.on_file_error:
                self.on_file_error(fid, f"Failed to write file: {e}")
            return
        _dlog(f"[FILE_TX] file {fid} assembled and cached at {path}")
        self._notify_file_received(fid, path, assembler.filename)

    def _notify_file_received(self, fid: str, path: str, filename: str):
        if self.on_file_received:
            try:
                self.on_file_received(fid, path, filename or os.path.basename(path))
            except Exception as e:
                _dlog(f"[FILE_TX] on_file_received callback error: {e!r}")

    def send_screen_share_start(self, channel_id: int, source_type: int,
                                 source_name: str, width: int, height: int,
                                 fps: int = 15):
        try:
            msg = ScreenShareStartRequest(
                channel_id=channel_id,
                source_type=source_type,
                source_name=source_name,
                width=width, height=height,
                fps=fps,
            )
            self._send_message(WireMessageType.SCREEN_SHARE_START, msg)
        except Exception:
            pass

    def send_screen_share_stop(self, channel_id: int):
        try:
            msg = ScreenShareStopRequest(channel_id=channel_id)
            self._send_message(WireMessageType.SCREEN_SHARE_STOP, msg)
        except Exception:
            pass

    # ============================================================
    # 一对一视频通话控制
    # ============================================================

    def _reset_video_call_state(self, reason: int = 0):
        """重置视频通话状态，并在必要时通知 UI。"""
        self._cancel_video_call_timeout()
        call_id = 0
        with self._video_call_lock:
            call_id = self._current_call_id
            self._video_call_state = VideoCallState.Idle
            self._current_call_id = 0
            self._call_peer_id = 0
            self._negotiated_profile = None
        if call_id and self.on_video_call_ended:
            try:
                self.on_video_call_ended(call_id, reason)
            except Exception:
                pass

    def _cancel_video_call_timeout(self):
        """取消未触发的呼叫超时定时器。"""
        timer = self._video_call_timeout_timer
        self._video_call_timeout_timer = None
        if timer is not None:
            timer.cancel()

    def _on_video_call_timeout(self, call_id: int):
        """呼叫发起 30 秒无响应：自动取消呼叫并恢复状态、通知 UI。"""
        with self._video_call_lock:
            if self._video_call_state != VideoCallState.Calling or self._current_call_id != call_id:
                return
            self._video_call_state = VideoCallState.Idle
            self._current_call_id = 0
            self._call_peer_id = 0
            self._negotiated_profile = None
        try:
            msg = VideoCallHangup(call_id=call_id, reason=3)
            self._send_message(WireMessageType.VIDEO_CALL_HANGUP, msg)
        except Exception:
            pass
        if self.on_video_call_error:
            try:
                self.on_video_call_error(call_id, "Call timeout")
            except Exception:
                pass

    def send_video_call_request(self, callee_id: int, profile: VideoProfile = None) -> bool:
        """向指定用户发起视频通话请求。"""
        if not self.connected:
            return False
        with self._video_call_lock:
            if self._video_call_state != VideoCallState.Idle:
                return False
            call_id = int(secrets.token_hex(4), 16)
            self._current_call_id = call_id
            self._call_peer_id = callee_id
            self._negotiated_profile = profile
            self._video_call_state = VideoCallState.Calling

        try:
            msg = VideoCallRequest(
                caller_id=self._user_id,
                callee_id=callee_id,
                call_id=call_id,
                requested_profile=profile or VideoProfile(),
            )
            self._send_message(WireMessageType.VIDEO_CALL_REQUEST, msg)
            # 呼叫超时保护：30 秒无响应自动取消
            self._video_call_timeout_timer = threading.Timer(
                30.0, self._on_video_call_timeout, args=(call_id,))
            self._video_call_timeout_timer.daemon = True
            self._video_call_timeout_timer.start()
            return True
        except Exception as e:
            logger.debug("[VIDEO_CALL] send_video_call_request failed: %s", e)
            self._reset_video_call_state(reason=1)
            return False

    def send_video_call_response(self, call_id: int, accepted: bool,
                                  profile: VideoProfile = None, reason: str = "") -> bool:
        """响应视频通话请求（接听或拒绝）。"""
        if not self.connected:
            return False
        with self._video_call_lock:
            if self._video_call_state != VideoCallState.Ringing:
                return False
            if self._current_call_id != call_id:
                return False
            self._negotiated_profile = profile
            self._video_call_state = VideoCallState.Connecting if accepted else VideoCallState.Ended

        try:
            msg = VideoCallResponse(
                call_id=call_id,
                accepted=accepted,
                negotiated_profile=profile or VideoProfile(),
                reason=reason,
            )
            self._send_message(WireMessageType.VIDEO_CALL_RESPONSE, msg)
            if not accepted:
                self._reset_video_call_state(reason=2)
            return True
        except Exception as e:
            logger.debug("[VIDEO_CALL] send_video_call_response failed: %s", e)
            self._reset_video_call_state(reason=1)
            return False

    def send_video_call_hangup(self, call_id: int, reason: int = 0) -> bool:
        """主动挂断视频通话。"""
        if not self.connected:
            return False
        with self._video_call_lock:
            if self._current_call_id != call_id:
                return False
            if self._video_call_state in (VideoCallState.Idle, VideoCallState.Ended):
                return False

        try:
            msg = VideoCallHangup(call_id=call_id, reason=reason)
            self._send_message(WireMessageType.VIDEO_CALL_HANGUP, msg)
        except Exception as e:
            logger.debug("[VIDEO_CALL] send_video_call_hangup failed: %s", e)
        finally:
            self._reset_video_call_state(reason=reason)
        return True

    def send_video_call_profile_update(self, call_id: int, profile: VideoProfile) -> bool:
        """在通话中更新视频 profile（如分辨率/码率切换）。"""
        if not self.connected:
            return False
        with self._video_call_lock:
            if self._current_call_id != call_id:
                return False
            if self._video_call_state not in (VideoCallState.Connecting, VideoCallState.Connected):
                return False
            self._negotiated_profile = profile

        try:
            msg = VideoCallProfileUpdate(call_id=call_id, new_profile=profile)
            self._send_message(WireMessageType.VIDEO_CALL_PROFILE_UPDATE, msg)
            return True
        except Exception as e:
            logger.debug("[VIDEO_CALL] send_video_call_profile_update failed: %s", e)
            return False

    def _handle_video_call_request(self, msg: VideoCallRequest):
        """收到来电请求。"""
        with self._video_call_lock:
            # 如果当前正忙，自动拒绝
            if self._video_call_state != VideoCallState.Idle:
                if self.on_video_call_error:
                    try:
                        self.on_video_call_error(msg.call_id, "Busy")
                    except Exception:
                        pass
                try:
                    resp = VideoCallResponse(
                        call_id=msg.call_id,
                        accepted=False,
                        negotiated_profile=VideoProfile(),
                        reason="busy",
                    )
                    self._send_message(WireMessageType.VIDEO_CALL_RESPONSE, resp)
                except Exception:
                    pass
                return
            self._current_call_id = msg.call_id
            self._call_peer_id = msg.caller_id
            self._negotiated_profile = msg.requested_profile
            self._video_call_state = VideoCallState.Ringing

        # 查找来电者用户名
        caller_name = f"User {msg.caller_id}"
        for u in self._channel_users:
            if u.get("id") == msg.caller_id:
                caller_name = u.get("username", caller_name)
                break

        if self.on_video_call_incoming:
            try:
                self.on_video_call_incoming(
                    msg.call_id, msg.caller_id, caller_name,
                    msg.requested_profile or VideoProfile(),
                )
            except Exception:
                traceback.print_exc()

    def _handle_video_call_response(self, msg: VideoCallResponse):
        """收到视频通话响应。"""
        with self._video_call_lock:
            if self._video_call_state != VideoCallState.Calling:
                return
            if self._current_call_id != msg.call_id:
                return
            self._cancel_video_call_timeout()
            self._negotiated_profile = msg.negotiated_profile
            if not msg.accepted:
                self._set_video_call_state(VideoCallState.Ended)
                if self.on_video_call_ended:
                    try:
                        self.on_video_call_ended(msg.call_id, 2)
                    except Exception:
                        pass
                # 延迟重置，让 UI 有机会展示拒绝原因
                threading.Timer(2.0, self._reset_video_call_state, kwargs={"reason": 2}).start()
                return
            self._video_call_state = VideoCallState.Connecting

        if self.on_video_call_established:
            try:
                peer_id = self._call_peer_id
                profile = self._negotiated_profile or VideoProfile()
                self.on_video_call_established(msg.call_id, peer_id, profile)
            except Exception:
                traceback.print_exc()

    def _handle_video_call_hangup(self, msg: VideoCallHangup):
        """收到对方挂断。"""
        with self._video_call_lock:
            if self._current_call_id != msg.call_id:
                return
            if self._video_call_state in (VideoCallState.Idle, VideoCallState.Ended):
                return
        self._reset_video_call_state(reason=msg.reason)

    def _handle_video_call_profile_update(self, msg: VideoCallProfileUpdate):
        """收到对方更新 profile（当前仅保存，UI 层可扩展处理）。"""
        with self._video_call_lock:
            if self._current_call_id != msg.call_id:
                return
            self._negotiated_profile = msg.new_profile

    def set_session_key(self, key: bytes):
        self._session_key = key

    def _send_message(self, msg_type: WireMessageType, msg):
        if not self._sock:
            raise ConnectionError("Not connected")
        payload = serialize_control_message(msg_type, msg)
        if len(payload) > TCP_MAX_PAYLOAD_SIZE:
            raise ValueError("Payload too large")
        case_value, _, _ = MESSAGE_TYPE_MAP[msg_type]
        header = struct.pack(">III", len(payload), case_value, 0)
        # 文件分片在后台线程发送，加锁避免与其他线程的 sendall 交错
        with self._send_lock:
            self._sock.sendall(header + payload)

    def _read_frame(self) -> tuple[int, bytes]:
        if not self._sock:
            raise ConnectionError("Not connected")
        header = self._recv_exact(TCP_HEADER_SIZE)
        payload_length, msg_type, request_id = struct.unpack(">III", header)
        if payload_length > TCP_MAX_PAYLOAD_SIZE:
            raise ValueError(f"Payload too large: {payload_length}")
        payload = self._recv_exact(payload_length)
        return msg_type, payload

    def _recv_exact(self, n: int) -> bytes:
        data = b""
        while len(data) < n:
            if not self._sock:
                raise ConnectionError("Connection closed")
            chunk = self._sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("Connection closed")
            data += chunk
        return data

    def _recv_loop(self):
        try:
            while self._connected and self._sock:
                try:
                    msg_type, payload = self._read_frame()
                    # TCP 语音帧（0xFF）：媒体走 TCP 控制连接（外网/NAT 场景）
                    if msg_type == TCP_VOICE_FRAME_TYPE:
                        if self.on_tcp_voice_frame:
                            try:
                                self.on_tcp_voice_frame(payload)
                            except Exception as e:
                                logger.debug("on_tcp_voice_frame error: %s", e)
                        continue
                    self._handle_message(msg_type, payload)
                except ConnectionError:
                    break
                except OSError:
                    break
                except Exception as e:
                    logger.debug("Error processing message (type=%s): %s",
                                 msg_type, e, exc_info=True)
                    continue
        finally:
            if self._connected:
                self._connected = False
                self._set_state(ClientState.Disconnected)
                if self.on_error:
                    self.on_error(7, "Connection lost")

    def _handle_message(self, msg_type: int, payload: bytes):
        logger.debug("[MSG] _handle_message: msg_type=%s, payload_size=%s",
                     msg_type, len(payload))
        try:
            wire_type, msg = deserialize_control_message(payload)
        except Exception as e:
            logger.debug("[MSG] Failed to deserialize message (type=%s, size=%s): %s",
                         msg_type, len(payload), e)
            return
        if wire_type is None or msg is None:
            logger.debug("[MSG] _handle_message: wire_type is None, skipping")
            return
        logger.debug("[MSG] _handle_message: wire_type=%s", wire_type)

        if wire_type == WireMessageType.CHANNEL_LIST_UPDATE:
            self._handle_channel_list(msg)
        elif wire_type == WireMessageType.USER_JOINED_CHANNEL:
            self._handle_user_joined(msg)
        elif wire_type == WireMessageType.USER_LEFT_CHANNEL:
            self._handle_user_left(msg)
        elif wire_type == WireMessageType.USER_SPEAKING:
            self._handle_user_speaking(msg)
        elif wire_type == WireMessageType.SERVER_MESSAGE:
            self._handle_server_message(msg)
        elif wire_type == WireMessageType.CHAT_BROADCAST:
            self._handle_chat_broadcast(msg)
        elif wire_type == WireMessageType.UDP_PING_RESPONSE:
            self._handle_udp_ping_response(msg)
        elif wire_type == WireMessageType.KEY_ROTATION_REQUEST:
            self._handle_key_rotation_request(msg)
        elif wire_type == WireMessageType.ADMIN_AUTH_RESPONSE:
            self._handle_admin_auth_response(msg)
        elif wire_type == WireMessageType.SET_SERVER_NAME_RESPONSE:
            self._handle_admin_response(msg, "set_server_name")
        elif wire_type == WireMessageType.RENAME_CHANNEL_RESPONSE:
            self._handle_admin_response(msg, "rename_channel")
            # 重命名成功后刷新本地频道树 UI 状态
            if self.on_channel_list:
                try:
                    self.on_channel_list(self._channels)
                except Exception:
                    pass
        elif wire_type == WireMessageType.SET_ADMIN_RESPONSE:
            self._handle_admin_response(msg, "set_admin")
        elif wire_type == WireMessageType.KICK_USER_RESPONSE:
            self._handle_admin_response(msg, "kick")
        elif wire_type == WireMessageType.BAN_USER_RESPONSE:
            self._handle_admin_response(msg, "ban")
        elif wire_type == WireMessageType.MOVE_USER_RESPONSE:
            self._handle_admin_response(msg, "move")
        elif wire_type == WireMessageType.LOGIN_RESPONSE:
            pass
        elif wire_type == WireMessageType.USER_MUTE_TOGGLE:
            pass
        elif wire_type == WireMessageType.FILE_UPLOAD_RESPONSE:
            self._handle_file_upload_response(msg)
        elif wire_type == WireMessageType.FILE_DOWNLOAD_RESPONSE:
            self._handle_file_download_response(msg)
        elif wire_type == WireMessageType.FILE_LIST_RESPONSE:
            self._handle_file_list_response(msg)
        elif wire_type == WireMessageType.SCREEN_SHARE_STATE:
            self._handle_screen_share_state(msg)
        elif wire_type == WireMessageType.VIDEO_CALL_REQUEST:
            self._handle_video_call_request(msg)
        elif wire_type == WireMessageType.VIDEO_CALL_RESPONSE:
            self._handle_video_call_response(msg)
        elif wire_type == WireMessageType.VIDEO_CALL_HANGUP:
            self._handle_video_call_hangup(msg)
        elif wire_type == WireMessageType.VIDEO_CALL_PROFILE_UPDATE:
            self._handle_video_call_profile_update(msg)

    def _handle_channel_list(self, msg):
        logger.debug("[CHANNEL_LIST] Received %s channels", len(msg.channels))
        self._channels = self._flatten_channels(msg.channels)
        _dlog(f"[CHANNEL_LIST] Received {len(self._channels)} flat channels, user_id={self._user_id}, in_channel={self.in_channel}, current_ch={self._current_channel_id}")
        for ch in self._channels:
            user_ids = [u.get("id") for u in ch.get("users", [])]
            _dlog(f"  channel '{ch['name']}'(id={ch['id']}) users={user_ids}")
        # Sync channel_users from current channel in the list
        if self.in_channel and self._current_channel_id:
            for ch in self._channels:
                if ch["id"] == self._current_channel_id:
                    logger.debug("[CHANNEL_LIST] Found current channel %s, users: %s",
                                 ch['name'], len(ch.get('users', [])))
                    self._channel_users = ch.get("users", [])
                    self._current_channel_name = ch.get("name", "")
                    _dlog(f"[CHANNEL_LIST] Synced from explicit channel: users={len(self._channel_users)}")
                    break
        else:
            # 服务器在登录/离开频道时会将本用户放入默认频道，
            # 但客户端本地状态仍为 Connected。通过遍历频道列表
            # 找到包含本用户 ID 的频道，恢复正确的频道状态。
            if self._user_id:
                found = False
                for ch in self._channels:
                    ch_users = ch.get("users", [])
                    if any(u.get("id") == self._user_id for u in ch_users):
                        self._current_channel_id = ch["id"]
                        self._current_channel_name = ch.get("name", "")
                        self._channel_users = ch_users
                        logger.debug("[CHANNEL_LIST] Detected current channel '%s' (id=%s), users: %s",
                                     self._current_channel_name, self._current_channel_id,
                                     len(self._channel_users))
                        _dlog(f"[CHANNEL_LIST] Auto-detected channel '{self._current_channel_name}'(id={self._current_channel_id}), users={len(self._channel_users)}")
                        if self._state == ClientState.Connected:
                            self._set_state(ClientState.InChannel)
                            _dlog(f"[CHANNEL_LIST] State changed Connected -> InChannel")
                        found = True
                        break
                if not found:
                    _dlog(f"[CHANNEL_LIST] WARNING: user_id={self._user_id} not found in any channel!")
        if self.on_channel_list:
            self.on_channel_list(self._channels)

    def _flatten_channels(self, channels, result=None, parent_id: int = 0) -> list:
        if result is None:
            result = []
        for ch in channels:
            effective_parent_id = ch.parent_id or parent_id
            result.append({
                "id": ch.id,
                "name": ch.name,
                "parent_id": effective_parent_id,
                "users": [{"id": u.id, "username": u.username, "muted": u.muted,
                           "deafened": u.deafened, "group_id": u.group_id}
                          for u in ch.users],
            })
            if ch.children:
                self._flatten_channels(ch.children, result, ch.id)
        return result

    def _handle_user_joined(self, msg):
        user_info = msg.user
        if user_info is None:
            return
        user = {
            "id": user_info.id,
            "username": user_info.username,
            "muted": user_info.muted,
            "deafened": user_info.deafened,
            "group_id": user_info.group_id,
        }
        logger.debug("[USER_JOINED] %s (id=%s) channel=%s, current=%s, in_channel=%s",
                     user['username'], user['id'], msg.channel_id,
                     self._current_channel_id, self.in_channel)
        _dlog(f"[USER_JOINED] user={user['username']}(id={user['id']}) msg_ch={msg.channel_id} current_ch={self._current_channel_id} in_channel={self.in_channel} cur_users={len(self._channel_users)}")
        # 如果当前频道未设置（channel_list 尚未到达），尝试用 msg.channel_id 自动检测
        if not self._current_channel_id and self._user_id and msg.channel_id:
            # 检查本用户是否也在该频道（通过现有 _channels 数据）
            for ch in self._channels:
                if ch["id"] == msg.channel_id:
                    if any(u.get("id") == self._user_id for u in ch.get("users", [])):
                        self._current_channel_id = msg.channel_id
                        self._current_channel_name = ch.get("name", "")
                        if self._state == ClientState.Connected:
                            self._set_state(ClientState.InChannel)
                        _dlog(f"[USER_JOINED] Auto-detected current channel via user_joined: ch={self._current_channel_name}(id={self._current_channel_id})")
                        # 用频道列表中的完整用户列表初始化 _channel_users
                        self._channel_users = list(ch.get("users", []))
                        break
        # 加入到 channel_users（如果属于当前频道且未重复）
        if msg.channel_id == self._current_channel_id and self._current_channel_id:
            existing = [u for u in self._channel_users if u["id"] == user["id"]]
            if not existing:
                self._channel_users.append(user)
                logger.debug("[USER_JOINED] Added to channel_users, total: %s",
                             len(self._channel_users))
                _dlog(f"[USER_JOINED] Added {user['username']}(id={user['id']}) to channel_users, total={len(self._channel_users)}")
        else:
            _dlog(f"[USER_JOINED] Not added: msg_ch={msg.channel_id} != current_ch={self._current_channel_id}")
        if self.on_user_joined:
            self.on_user_joined(user)

    def _handle_user_left(self, msg):
        uid = msg.user_id
        before = len(self._channel_users)
        self._channel_users = [u for u in self._channel_users if u["id"] != uid]
        after = len(self._channel_users)
        _dlog(f"[USER_LEFT] user_id={uid} channel_id={msg.channel_id} current_ch={self._current_channel_id} users_before={before} users_after={after}")
        if self.on_user_left:
            self.on_user_left(uid)

    def _handle_user_speaking(self, msg):
        if self.on_user_speaking:
            self.on_user_speaking(msg.user_id, msg.speaking)

    def _handle_server_message(self, msg):
        if self.on_server_message:
            self.on_server_message(msg.text)

    def _handle_chat_broadcast(self, msg):
        text = msg.text or ""
        # 文件传输数据通道：分片标记 / 取回标记不进聊天 UI
        if is_file_transfer_marker(text):
            self._handle_file_transfer_marker(msg.sender_id, text)
            return
        if self.on_chat_message:
            self.on_chat_message(
                msg.sender_id, msg.sender_name,
                msg.channel_id, msg.text, msg.timestamp
            )

    def _handle_admin_auth_response(self, msg):
        logger.debug("[ADMIN] _handle_admin_auth_response called: result=%s, message=%s",
                     msg.result, msg.message)
        success = msg.result == ResultCode.OK
        if success:
            self._is_admin = True
        logger.debug("[ADMIN] _handle_admin_auth_result: emitting signal, success=%s", success)
        if self.on_admin_auth_result:
            self.on_admin_auth_result(success, msg.message)

    def _handle_admin_response(self, msg, action: str):
        success = msg.result == ResultCode.OK
        if self.on_admin_action_result:
            self.on_admin_action_result(success, msg.message)

    def _handle_file_upload_response(self, msg):
        file_id = msg.file_id
        success = msg.result == ResultCode.OK
        if self.on_file_upload_response:
            self.on_file_upload_response(file_id, success, msg.message)

    def _handle_file_download_response(self, msg):
        """服务端 FileDownloadResponse(47) 分片重组。

        前瞻实现：当前服务端未实现下载（46 请求被丢弃，不会有 47 到达）。
        服务端实现后按 msg.result 分片重组并写盘。
        """
        if msg.result != ResultCode.OK:
            if self.on_file_error:
                self.on_file_error(str(msg.file_id), msg.message or "Download failed")
            return
        if msg.chunk_count <= 0 or msg.chunk_count > FILE_MAX_CHUNK_COUNT:
            return
        fid = str(msg.file_id)
        if get_cached_file_path(fid):
            return
        with self._file_lock:
            assembler = self._rx_assemblers.get(fid)
            if assembler is None or assembler.chunk_count != msg.chunk_count:
                assembler = FileChunkAssembler(fid, msg.chunk_count)
                assembler.wire_source = True  # 服务端通道优先于聊天中继
                self._rx_assemblers[fid] = assembler
            assembler.wire_source = True
            done = assembler.add_chunk(msg.chunk_index, msg.data, msg.filename)
        if done:
            self._finalize_rx(fid)

    def _handle_file_list_response(self, msg):
        entries = []
        for e in getattr(msg, 'entries', []):
            entries.append({
                "id": e.id,
                "channel_id": e.channel_id,
                "uploader_id": e.uploader_id,
                "filename": e.filename,
                "file_size": e.file_size,
                "upload_time": e.upload_time,
            })
        _dlog(f"[FILE_LIST] received {len(entries)} entries")
        if self.on_file_list:
            self.on_file_list(entries)

    def _handle_screen_share_state(self, msg: ScreenShareState):
        if self.on_screen_share_state:
            self.on_screen_share_state(
                msg.user_id, msg.sharing, msg.source_type,
                msg.source_name, msg.width, msg.height
            )

    # ============================================================
    # Ping / Latency Measurement
    # ============================================================

    def _start_ping_timer(self):
        self._send_ping()

    def _stop_ping_timer(self):
        if self._ping_timer:
            self._ping_timer.cancel()
            self._ping_timer = None
        self._last_latency_ms = -1

    def _send_ping(self):
        if not self.connected:
            return
        try:
            self._ping_send_time = time.time()
            msg = UdpPingRequest(sequence=self._ping_seq, client_udp_key=b"")
            self._send_message(WireMessageType.UDP_PING_REQUEST, msg)
            self._ping_seq += 1
            # Schedule next ping in 5 seconds
            self._ping_timer = threading.Timer(5.0, self._send_ping)
            self._ping_timer.daemon = True
            self._ping_timer.start()
        except Exception:
            pass

    def _handle_udp_ping_response(self, msg):
        rtt = (time.time() - self._ping_send_time) * 1000
        latency_ms = int(rtt)
        self._last_latency_ms = latency_ms
        if self.on_latency_update:
            self.on_latency_update(latency_ms)

    def _handle_key_rotation_request(self, msg):
        key = None
        if msg.encrypted_session_key:
            key = self._decrypt_session_key(msg.encrypted_session_key)
        if key is None:
            self._fail_connection("密钥协商失败，连接已终止")
            return
        self._session_key = key
        self._rotate_session_key_in_media()
        try:
            resp = KeyRotationResponse(
                new_client_public_key=self._public_key,
                key_epoch=msg.key_epoch,
            )
            self._send_message(WireMessageType.KEY_ROTATION_RESPONSE, resp)
        except Exception:
            pass
