import struct
import socket
import threading
import time
import traceback
import logging
from enum import IntEnum
from typing import Optional, Callable

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# 配置日志记录器
logger = logging.getLogger("nevo_client")

# ★ 启动标记 — 如果这行没出现在日志里，说明运行的代码不是这个文件
print(f"[BOOTSTRAP] nevo_client.py loaded from: {__file__}")

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
    FileDeleteRequest, FileDeleteResponse,
    ChannelListUpdate, UserJoinedChannel, UserLeftChannel,
    UserSpeaking, ServerMessage,
    KeyRotationRequest, KeyRotationResponse,
    StunBindRequest, StunBindResponse,
    UdpPingRequest, UdpPingResponse,
    serialize_control_message, deserialize_control_message,
    MESSAGE_TYPE_MAP, CASE_TO_DESERIALIZER,
)
import socket as _socket

try:
    from nacl.public import PrivateKey as _PrivateKey, SealedBox as _SealedBox
    _HAS_SEALED_BOX = True
except Exception:
    _PrivateKey = None
    _SealedBox = None
    _HAS_SEALED_BOX = False


class ClientState(IntEnum):
    Disconnected = 0
    Connecting = 1
    Connected = 2
    InChannel = 3


TCP_HEADER_SIZE = 12
TCP_MAX_PAYLOAD_SIZE = 1024 * 1024


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

        self.on_state_changed: Optional[Callable[[ClientState, ClientState], None]] = None
        self.on_channel_list: Optional[Callable[[list], None]] = None
        self.on_user_joined: Optional[Callable[[dict], None]] = None
        self.on_user_left: Optional[Callable[[int], None]] = None
        self.on_user_speaking: Optional[Callable[[int, bool], None]] = None
        self.on_chat_message: Optional[Callable[[int, str, int, str, int], None]] = None
        self.on_server_message: Optional[Callable[[str], None]] = None
        self.on_error: Optional[Callable[[int, str], None]] = None
        self.on_admin_auth_result: Optional[Callable[[bool, str], None]] = None
        self.on_admin_action_result: Optional[Callable[[bool, str], None]] = None
        self.on_file_upload_response: Optional[Callable[[int, bool, str], None]] = None
        self.on_latency_update: Optional[Callable[[int], None]] = None

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
    def channels(self) -> list:
        return self._channels

    @property
    def channel_users(self) -> list:
        return self._channel_users

    def _set_state(self, new_state: ClientState):
        old = self._state
        self._state = new_state
        print(f"[STATE] {old.name} -> {new_state.name}")
        if self.on_state_changed:
            self.on_state_changed(new_state, old)

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
        print(f"[SESSION_KEY_DEBUG] encrypted_session_key present: {bool(login_resp.encrypted_session_key)}")
        print(f"[SESSION_KEY_DEBUG] server_public_key present: {bool(login_resp.server_public_key)}")
        pk_len = len(login_resp.server_public_key) if login_resp.server_public_key else 0
        print(f"[SESSION_KEY_DEBUG] server_public_key len: {pk_len}")
        if login_resp.encrypted_session_key:
            print(f"[SESSION_KEY_DEBUG] encrypted_session_key len: {len(login_resp.encrypted_session_key)}")
            key = self._decrypt_session_key(login_resp.encrypted_session_key)
            print(f"[SESSION_KEY_DEBUG] decrypt result: {key is not None}")
        if key is None and login_resp.server_public_key and len(login_resp.server_public_key) >= 32:
            key = login_resp.server_public_key[:32]
            print(f"[SESSION_KEY_DEBUG] fallback to server_public_key[:32]")
        if key is None:
            print(f"[SESSION_KEY_DEBUG] FAILED: no key available - encrypted_session_key={bool(login_resp.encrypted_session_key)}, pk_len={pk_len}")
            return False
        self._session_key = key
        if isinstance(key, (bytes, bytearray)):
            print(f"[SESSION_KEY] Applied session key (first 8 bytes hex): {key[:8].hex()}")
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

    def connect(self, host: str, port: int, username: str, password: str = "",
                voice_engine=None, video_engine=None) -> bool:
        with self._lock:
            if self._state != ClientState.Disconnected:
                return False
            self._username = username
            self._voice_engine = voice_engine
            self._video_engine = video_engine

        self._set_state(ClientState.Connecting)

        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.settimeout(10)
            self._sock.connect((host, port))
            self._sock.settimeout(None)

            client_udp_port = 0
            if voice_engine is not None:
                voice_engine.pre_create_udp_socket()
                client_udp_port = voice_engine.local_udp_port

            client_video_port = 0
            if video_engine is not None:
                video_engine.pre_create_udp_socket()
                client_video_port = video_engine.local_udp_port

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
                except Exception:
                    self._private_key = None
                    self._public_key = b""

            login_msg = LoginRequest(
                username=username,
                auth_credential=password.encode("utf-8"),
                key_exchange_methods=key_exchange_methods,
                client_public_key=client_public_key,
                client_udp_port=client_udp_port,
                client_video_udp_port=client_video_port,
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

            if login_resp.encrypted_session_key:
                self._encrypted_session_key = login_resp.encrypted_session_key
            self._apply_session_key_from_response(login_resp)
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
            msg = SpeakingState(speaking=speaking)
            self._send_message(WireMessageType.SPEAKING_STATE, msg)
        except Exception:
            pass

    def send_admin_auth(self, password: str):
        print(f"[DEBUG] send_admin_auth called, pwd_len={len(password)}")
        try:
            msg = AdminAuthRequest(password=password)
            self._send_message(WireMessageType.ADMIN_AUTH_REQUEST, msg)
            print(f"[DEBUG] send_admin_auth: message sent successfully")
        except Exception as e:
            print(f"[ERROR] send_admin_auth exception: {e}")
            traceback.print_exc()

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
                    self._handle_message(msg_type, payload)
                except ConnectionError:
                    break
                except OSError:
                    break
                except Exception as e:
                    traceback.print_exc()
                    print(f"[Warning] Error processing message (type={msg_type}): {e}")
                    continue
        finally:
            if self._connected:
                self._connected = False
                self._set_state(ClientState.Disconnected)
                if self.on_error:
                    self.on_error(7, "Connection lost")

    def _handle_message(self, msg_type: int, payload: bytes):
        print(f"[DEBUG] _handle_message: msg_type={msg_type}, payload_size={len(payload)}")
        try:
            wire_type, msg = deserialize_control_message(payload)
        except Exception as e:
            print(f"[WARNING] Failed to deserialize message (type={msg_type}, size={len(payload)}): {e}")
            return
        if wire_type is None or msg is None:
            print(f"[DEBUG] _handle_message: wire_type is None, skipping")
            return
        print(f"[DEBUG] _handle_message: wire_type={wire_type}")

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

    def _handle_channel_list(self, msg):
        print(f"[CHANNEL_LIST] Received {len(msg.channels)} channels")
        self._channels = self._flatten_channels(msg.channels)
        # Sync channel_users from current channel in the list
        if self.in_channel and self._current_channel_id:
            for ch in self._channels:
                if ch["id"] == self._current_channel_id:
                    print(f"[CHANNEL_LIST] Found current channel {ch['name']}, users: {len(ch.get('users', []))}")
                    self._channel_users = ch.get("users", [])
                    self._current_channel_name = ch.get("name", "")
                    break
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
        print(f"[USER_JOINED] {user['username']} (id={user['id']}) channel={msg.channel_id}, current={self._current_channel_id}, in_channel={self.in_channel}")
        # Always add to channel_users if it's our current channel or if we are joining
        if msg.channel_id == self._current_channel_id:
            # Check if user already exists to avoid duplicates
            existing = [u for u in self._channel_users if u["id"] == user["id"]]
            if not existing:
                self._channel_users.append(user)
                print(f"[USER_JOINED] Added to channel_users, total: {len(self._channel_users)}")
        if self.on_user_joined:
            self.on_user_joined(user)

    def _handle_user_left(self, msg):
        uid = msg.user_id
        self._channel_users = [u for u in self._channel_users if u["id"] != uid]
        if self.on_user_left:
            self.on_user_left(uid)

    def _handle_user_speaking(self, msg):
        if self.on_user_speaking:
            self.on_user_speaking(msg.user_id, msg.speaking)

    def _handle_server_message(self, msg):
        if self.on_server_message:
            self.on_server_message(msg.text)

    def _handle_chat_broadcast(self, msg):
        if self.on_chat_message:
            self.on_chat_message(
                msg.sender_id, msg.sender_name,
                msg.channel_id, msg.text, msg.timestamp
            )

    def _handle_admin_auth_response(self, msg):
        print(f"[DEBUG] _handle_admin_auth_response called: result={msg.result}, message={msg.message}")
        success = msg.result == ResultCode.OK
        if success:
            self._is_admin = True
        print(f"[DEBUG] _handle_admin_auth_result: emitting signal, success={success}")
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
        if key is None and msg.new_server_public_key and len(msg.new_server_public_key) >= 32:
            key = msg.new_server_public_key[:32]
        if key is None:
            return
        self._session_key = key
        self._apply_session_key_to_media()
        try:
            resp = KeyRotationResponse(
                new_client_public_key=self._public_key,
                key_epoch=msg.key_epoch,
            )
            self._send_message(WireMessageType.KEY_ROTATION_RESPONSE, resp)
        except Exception:
            pass
