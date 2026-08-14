#!/usr/bin/env python3
"""
NEVO Web Client Gateway
Bridges browser WebSocket connections to the NEVO TCP backend (port 24430).
Uses NevoClient from the Python GUI layer for wire protocol handling.
Zero external dependencies beyond the existing NEVO Python runtime.
"""

import base64
import hashlib
import json
import os
import socket
import struct
import sys
import threading
import time
import traceback
from http.server import HTTPServer, SimpleHTTPRequestHandler
from socketserver import ThreadingMixIn

# ---- Add NEVO client library to path ----
_GUI_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "src", "client", "gui_python")
sys.path.insert(0, os.path.abspath(_GUI_DIR))

HOST = os.environ.get("NEVO_WEB_HOST", "127.0.0.1")
WEB_PORT = int(os.environ.get("NEVO_WEB_PORT", "8088"))

# When frozen with PyInstaller, web assets may sit next to the exe (one-file)
# or inside the _internal directory (one-dir). Prefer _internal if it contains index.html.
if getattr(sys, "frozen", False):
    exe_dir = os.path.dirname(sys.executable)
    internal_dir = os.path.join(exe_dir, "_internal")
    if os.path.isfile(os.path.join(internal_dir, "index.html")):
        WEB_ROOT = internal_dir
    else:
        WEB_ROOT = exe_dir
else:
    WEB_ROOT = os.path.dirname(os.path.abspath(__file__))

# ---- NevoClient import (graceful fallback) ----
try:
    from nevo_client import NevoClient, ClientState, VideoCallState, VideoProfile
    from nevo_wire import ResultCode
    import nevo_client as _nc_probe
    CLIENT_AVAILABLE = True
    print(f"[GATEWAY] NevoClient library loaded successfully "
          f"(sealed_box_available={getattr(_nc_probe, '_HAS_SEALED_BOX', '?')})")
except Exception as e:
    CLIENT_AVAILABLE = False
    print(f"[GATEWAY] WARNING: NevoClient not available: {e}")
    print("[GATEWAY] Running in mock mode (no real backend connection)")

# ---- 媒体库条件导入（VoiceCrypto + protobuf）----
try:
    from voice_crypto import VoiceCrypto
    from proto import voice_pb2, video_pb2
    HAS_MEDIA = True
    print("[GATEWAY] Media libraries (VoiceCrypto, protobuf) loaded successfully")
except Exception as e:
    HAS_MEDIA = False
    print(f"[GATEWAY] Media libs not available: {e}")


# ============================================================
#  WebSocket frame helpers (RFC 6455, text only)
# ============================================================

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def ws_accept_key(client_key: str) -> str:
    digest = hashlib.sha1((client_key + WS_GUID).encode()).digest()
    return base64.b64encode(digest).decode()


def ws_read_frame(rfile) -> str | None:
    """Read one WebSocket text frame from rfile. Returns None on close."""
    hdr = rfile.read(2)
    if len(hdr) < 2:
        return None
    b0, b1 = hdr[0], hdr[1]
    opcode = b0 & 0x0F
    masked = (b1 & 0x80) != 0
    length = b1 & 0x7F

    if opcode == 0x08:  # Close frame
        return None

    if length == 126:
        raw = rfile.read(2)
        length = struct.unpack(">H", raw)[0]
    elif length == 127:
        raw = rfile.read(8)
        length = struct.unpack(">Q", raw)[0]

    mask_key = b""
    if masked:
        mask_key = rfile.read(4)

    payload = rfile.read(length) if length > 0 else b""
    if masked and payload:
        unmasked = bytearray(payload)
        for i in range(len(unmasked)):
            unmasked[i] ^= mask_key[i % 4]
        payload = bytes(unmasked)

    return payload.decode("utf-8", errors="replace")


def ws_send_text(wfile, message: str, lock: threading.Lock):
    """Send a WebSocket text frame."""
    data = message.encode("utf-8")
    frame = bytearray()
    frame.append(0x81)  # FIN + text opcode
    length = len(data)
    if length < 126:
        frame.append(length)
    elif length < 65536:
        frame.append(126)
        frame.extend(struct.pack(">H", length))
    else:
        frame.append(127)
        frame.extend(struct.pack(">Q", length))
    frame.extend(data)
    with lock:
        wfile.write(bytes(frame))
        wfile.flush()


def ws_send_close(wfile, lock: threading.Lock):
    frame = bytearray([0x88, 0x00])
    try:
        with lock:
            wfile.write(bytes(frame))
            wfile.flush()
    except Exception:
        pass


# ============================================================
#  NevoClient bridge — wraps NevoClient with JSON event forwarding
# ============================================================

class ClientBridge:
    """Wraps a NevoClient instance, forwarding callbacks as JSON events."""

    def __init__(self, wfile, ws_lock: threading.Lock):
        self._wfile = wfile
        self._ws_lock = ws_lock
        self._client: NevoClient | None = None
        self._alive = True
        self._voice_engine = None
        self._video_engine = None
        self._media_bridge = None  # 媒体桥接（语音/视频 UDP）

        if CLIENT_AVAILABLE:
            self._client = NevoClient()
            self._wire_callbacks()

    def _send_event(self, event: str, data: dict | None = None):
        if not self._alive:
            return
        msg = json.dumps({"event": event, "data": data or {}}, ensure_ascii=False)
        try:
            ws_send_text(self._wfile, msg, self._ws_lock)
        except Exception:
            self._alive = False

    def _wire_callbacks(self):
        c = self._client
        c.on_state_changed = self._on_state_changed
        c.on_channel_list = self._on_channel_list
        c.on_user_joined = self._on_user_joined
        c.on_user_left = self._on_user_left
        c.on_user_speaking = self._on_user_speaking
        c.on_chat_message = self._on_chat_message
        c.on_server_message = self._on_server_message
        c.on_error = self._on_error
        c.on_latency_update = self._on_latency_update
        c.on_video_call_incoming = self._on_video_call_incoming
        c.on_video_call_established = self._on_video_call_established
        c.on_video_call_ended = self._on_video_call_ended
        c.on_video_call_error = self._on_video_call_error
        # 管理 / 文件 / 屏幕共享回调接线
        c.on_admin_auth_result = self._on_admin_auth_result
        c.on_admin_action_result = self._on_admin_action_result
        c.on_file_upload_response = self._on_file_upload_response
        c.on_file_list = self._on_file_list
        c.on_screen_share_state = self._on_screen_share_state

    # ---- Callback handlers ----

    def _on_state_changed(self, new_state, old_state):
        state_names = {0: "disconnected", 1: "connecting", 2: "connected", 3: "in_channel"}
        self._send_event("state_changed", {
            "state": state_names.get(int(new_state), "unknown"),
            "old_state": state_names.get(int(old_state), "unknown"),
        })

    def _on_channel_list(self, channels):
        self._send_event("channel_list", {"channels": channels})
        # 登录后服务器会自动放入默认频道；此处同步 MediaBridge 的频道 ID，
        # 否则 UDP 注册包和语音帧会带 channel_id=0，导致服务器不转发。
        if self._media_bridge and self._client:
            cid = self._client.current_channel_id
            if cid > 0:
                self._media_bridge.set_channel(cid)
            # 对端重启软件后重新登录时，服务器只广播 channel_list（无 user_joined）。
            # 若不在此时重置其语音去重基线，新会话序号从 0 重新计数会被当作
            # 旧会话的重复帧全部丢弃（"A 重启后 B 听不到 A"）。重置所有在列
            # 用户：至多造成每个活跃发送者一帧重复（20ms），可听影响可忽略。
            for ch in (channels or []):
                for u in (ch.get("users") or []):
                    uid = u.get("id")
                    if uid:
                        self._media_bridge.reset_sender(uid)

    def _on_user_joined(self, user):
        self._send_event("user_joined", {"user": user})
        # 对端重新入频道（含重启后显式入频道）同样需要重置其去重基线与
        # 视频分片缓冲（原因同 _on_channel_list）。
        uid = (user or {}).get("id")
        if uid and self._media_bridge:
            self._media_bridge.reset_sender(uid)

    def _on_user_left(self, user_id):
        self._send_event("user_left", {"user_id": user_id})

    def _on_user_speaking(self, user_id, speaking):
        self._send_event("user_speaking", {"user_id": user_id, "speaking": speaking})

    def _on_chat_message(self, user_id, username, channel_id, text, timestamp):
        self._send_event("chat_message", {
            "user_id": user_id, "username": username,
            "channel_id": channel_id, "text": text, "timestamp": timestamp,
        })

    def _on_server_message(self, text):
        self._send_event("server_message", {"text": text})

    def _on_error(self, code, message):
        self._send_event("error", {"code": code, "message": message})

    def _on_latency_update(self, latency_ms):
        self._send_event("latency_update", {"latency_ms": latency_ms})

    def _on_video_call_incoming(self, caller_id, call_id, caller_name, profile):
        self._send_event("video_call_incoming", {
            "caller_id": caller_id, "call_id": call_id,
            "caller_name": caller_name,
            "profile": self._profile_to_dict(profile),
        })

    def _on_video_call_established(self, call_id, peer_id, profile):
        self._send_event("video_call_established", {
            "call_id": call_id, "peer_id": peer_id,
            "profile": self._profile_to_dict(profile),
        })

    def _on_video_call_ended(self, call_id, reason):
        self._send_event("video_call_ended", {"call_id": call_id, "reason": reason})

    def _on_video_call_error(self, call_id, message):
        self._send_event("video_call_error", {"call_id": call_id, "message": message})

    def _on_admin_auth_result(self, success, message):
        self._send_event("admin_auth_result", {"success": success, "message": message})

    def _on_admin_action_result(self, success, message):
        self._send_event("admin_action_result", {"success": success, "message": message})

    def _on_file_upload_response(self, file_id, success, message):
        self._send_event("file_upload_response", {"file_id": file_id, "success": success, "message": message})

    def _on_file_list(self, files):
        self._send_event("file_list", {"files": files})

    def _on_screen_share_state(self, user_id, is_sharing, source_type, source_title, channel_id, width):
        self._send_event("screen_share_state", {
            "user_id": user_id, "is_sharing": is_sharing,
            "source_type": source_type, "source_title": source_title,
            "channel_id": channel_id, "width": width
        })

    @staticmethod
    def _profile_to_dict(profile) -> dict:
        if profile is None:
            return {}
        return {
            "codec": getattr(profile, "codec", 0),
            "width": getattr(profile, "width", 640),
            "height": getattr(profile, "height", 480),
            "fps": getattr(profile, "fps", 30),
            "target_bitrate_kbps": getattr(profile, "target_bitrate_kbps", 1000),
        }

    # ---- Command handlers (called from WebSocket) ----

    def handle_command(self, action: str, params: dict) -> dict:
        """Process a JSON command from the browser. Returns a response dict."""
        if not CLIENT_AVAILABLE:
            return self._mock_command(action, params)

        c = self._client
        if c is None:
            return {"ok": False, "error": "Client not initialized"}

        try:
            if action == "login":
                host = params.get("host", "127.0.0.1")
                port = int(params.get("port", 24430))
                username = params.get("username", "")
                password = params.get("password", "")
                if not username:
                    return {"ok": False, "error": "用户名不能为空"}
                # 预创建 UDP 媒体套接字：把本地端口随 LoginRequest 上报服务器，
                # 服务端在登录时即建立 UDP 端点映射（fail-closed 中继要求，
                # 未映射端点的一切语音/视频包都会被丢弃）；MediaBridge 随后复用。
                voice_sock = None
                video_sock = None
                if CLIENT_AVAILABLE and HAS_MEDIA:
                    try:
                        voice_sock, video_sock = MediaBridge.pre_create_sockets()
                    except Exception as e:
                        print(f"[GATEWAY] MediaBridge pre-create failed: {e}")
                client_udp_port = 0
                client_video_port = 0
                if voice_sock:
                    try:
                        client_udp_port = voice_sock.getsockname()[1]
                    except Exception:
                        pass
                if video_sock:
                    try:
                        client_video_port = video_sock.getsockname()[1]
                    except Exception:
                        pass
                ok = c.connect(host, port, username, password,
                               client_udp_port=client_udp_port,
                               client_video_udp_port=client_video_port)
                # 登录成功后创建并启动媒体桥接（复用预创建套接字）
                if ok and c.session_key and c.server_udp_port:
                    if CLIENT_AVAILABLE and HAS_MEDIA:
                        try:
                            self._media_bridge = MediaBridge(
                                self._wfile, self._ws_lock, c,
                                voice_sock=voice_sock, video_sock=video_sock)
                            self._media_bridge.start(
                                host, c.server_udp_port, c.server_video_udp_port,
                                c.user_id, c.session_key)
                            # 注册为客户端媒体引擎：服务端每 600s 轮换会话
                            # 密钥时，NevoClient 会把新密钥传播到媒体桥的
                            # 加密层（否则轮换宽限期一过语音/视频全部失效）。
                            c.register_media_engines(self._media_bridge)
                        except Exception as e:
                            print(f"[GATEWAY] MediaBridge start failed: {e}")
                            self._media_bridge = None
                            for _s in (voice_sock, video_sock):
                                if _s:
                                    try:
                                        _s.close()
                                    except Exception:
                                        pass
                elif voice_sock:
                    # 登录失败：关闭预创建套接字，避免句柄泄漏
                    for _s in (voice_sock, video_sock):
                        if _s:
                            try:
                                _s.close()
                            except Exception:
                                pass
                return {
                    "ok": ok,
                    "user_id": c.user_id,
                    "username": c.username,
                    "is_admin": c.is_admin,
                    "server_udp_port": c.server_udp_port,
                    "server_video_udp_port": c.server_video_udp_port,
                    "error": "" if ok else "连接失败 — 请检查服务器地址和端口",
                }

            elif action == "disconnect":
                c.disconnect()
                return {"ok": True}

            elif action == "join_channel":
                channel_id = int(params.get("channel_id", 0))
                ok = c.join_channel(channel_id)
                # 通知媒体桥接当前频道
                if ok and self._media_bridge:
                    self._media_bridge.set_channel(channel_id)
                return {"ok": ok, "channel_id": channel_id,
                        "channel_name": c.current_channel_name}

            elif action == "leave_channel":
                c.leave_channel()
                # 离开频道时清零媒体桥接频道
                if self._media_bridge:
                    self._media_bridge.set_channel(0)
                return {"ok": True}

            elif action == "send_chat":
                text = params.get("text", "")
                channel_id = int(params.get("channel_id", 0))
                c.send_chat(text, channel_id)
                return {"ok": True}

            elif action == "toggle_mute":
                muted = bool(params.get("muted", False))
                c.set_muted(muted)
                # 同步媒体桥：静音后停止发送本端麦克风音频（否则他人仍能听到）
                if self._media_bridge:
                    try:
                        self._media_bridge.set_muted(muted)
                    except Exception:
                        pass
                return {"ok": True, "muted": muted}

            elif action == "toggle_deafen":
                deafened = bool(params.get("deafened", False))
                c.set_deafened(deafened)
                # 同步媒体桥：闭麦后停止播放远端音频（并隐含静音本端）
                if self._media_bridge:
                    try:
                        self._media_bridge.set_deafened(deafened)
                    except Exception:
                        pass
                return {"ok": True, "deafened": deafened, "muted": c.is_muted}

            elif action == "speaking_state":
                # 浏览器本地上报的说话状态（VAD），转发给服务器以便广播给其他客户端
                speaking = bool(params.get("speaking", False))
                try:
                    c.send_speaking_state(speaking)
                except Exception:
                    pass
                return {"ok": True}

            elif action == "start_video_call":
                callee_id = int(params.get("callee_id", 0))
                profile = VideoProfile(
                    width=int(params.get("width", 640)),
                    height=int(params.get("height", 480)),
                    fps=int(params.get("fps", 30)),
                    target_bitrate_kbps=int(params.get("bitrate", 1000)),
                )
                ok = c.send_video_call_request(callee_id, profile)
                return {"ok": ok, "call_id": c.current_call_id}

            elif action == "accept_video_call":
                call_id = int(params.get("call_id", 0))
                profile = VideoProfile(
                    width=int(params.get("width", 640)),
                    height=int(params.get("height", 480)),
                    fps=int(params.get("fps", 30)),
                )
                ok = c.send_video_call_response(call_id, True, profile)
                return {"ok": ok}

            elif action == "reject_video_call":
                call_id = int(params.get("call_id", 0))
                ok = c.send_video_call_response(call_id, False, reason="用户拒绝")
                return {"ok": ok}

            elif action == "hangup_video_call":
                call_id = int(params.get("call_id", 0))
                ok = c.send_video_call_hangup(call_id)
                return {"ok": ok}

            elif action == "get_status":
                return {
                    "ok": True,
                    "state": int(c.state),
                    "connected": c.connected,
                    "in_channel": c.in_channel,
                    "username": c.username,
                    "user_id": c.user_id,
                    "current_channel_id": c.current_channel_id,
                    "current_channel_name": c.current_channel_name,
                    "is_muted": c.is_muted,
                    "is_deafened": c.is_deafened,
                    "is_admin": c.is_admin,
                    "channels": c.channels,
                    "channel_users": c.channel_users,
                    "video_call_state": int(c.video_call_state),
                    "current_call_id": c.current_call_id,
                    "call_peer_id": c.call_peer_id,
                }

            elif action == "admin_auth":
                password = params.get("password", "")
                c.send_admin_auth(password)
                return {"ok": True}

            elif action == "create_channel":
                name = params.get("name", "")
                parent_id = int(params.get("parent_id", 0))
                c.send_create_channel(name, parent_id)
                return {"ok": True}

            elif action == "delete_channel":
                channel_id = int(params.get("channel_id", 0))
                c.send_delete_channel(channel_id)
                return {"ok": True}

            # ---- 管理命令 ----
            elif action == "rename_channel":
                channel_id = int(params.get("channel_id", 0))
                new_name = params.get("new_name", "")
                c.send_rename_channel(channel_id, new_name)
                return {"ok": True}

            elif action == "set_server_name":
                server_name = params.get("server_name", "")
                c.send_set_server_name(server_name)
                return {"ok": True}

            elif action == "set_admin":
                user_id = int(params.get("user_id", 0))
                set_admin = bool(params.get("set_admin", False))
                c.send_set_admin(user_id, set_admin)
                return {"ok": True}

            elif action == "kick_user":
                user_id = int(params.get("user_id", 0))
                reason = params.get("reason", "")
                c.send_kick_user(user_id, reason)
                return {"ok": True}

            elif action == "ban_user":
                user_id = int(params.get("user_id", 0))
                reason = params.get("reason", "")
                expires_at = int(params.get("expires_at", 0))
                c.send_ban_user(user_id, reason, expires_at)
                return {"ok": True}

            elif action == "move_user":
                user_id = int(params.get("user_id", 0))
                channel_id = int(params.get("channel_id", 0))
                c.send_move_user(user_id, channel_id)
                return {"ok": True}

            # ---- 文件传输命令 ----
            elif action == "file_list":
                channel_id = int(params.get("channel_id", 0))
                c.send_file_list_request(channel_id)
                return {"ok": True}

            elif action == "file_upload":
                channel_id = int(params.get("channel_id", 0))
                filename = params.get("filename", "")
                file_size = int(params.get("file_size", 0))
                c.send_file_upload_request(channel_id, filename, file_size)
                return {"ok": True}

            elif action == "file_delete":
                file_id = int(params.get("file_id", 0))
                c.send_file_delete_request(file_id)
                return {"ok": True}

            # ---- 屏幕共享命令 ----
            elif action == "screen_share_start":
                channel_id = int(params.get("channel_id", 0))
                source_type = int(params.get("source_type", 0))
                source_title = params.get("source_title", "")
                width = int(params.get("width", 640))
                height = int(params.get("height", 480))
                fps = int(params.get("fps", 15))
                c.send_screen_share_start(channel_id, source_type, source_title, width, height, fps)
                return {"ok": True}

            elif action == "screen_share_stop":
                channel_id = int(params.get("channel_id", 0))
                c.send_screen_share_stop(channel_id)
                return {"ok": True}

            # ---- 媒体帧（浏览器 WebCodecs → UDP）----
            elif action == "media_frame":
                if self._media_bridge:
                    media_type = params.get("type", "")  # "voice" 或 "video"
                    data_b64 = params.get("data", "")
                    raw = base64.b64decode(data_b64)
                    if media_type == "voice":
                        self._media_bridge.send_voice_frame(raw)
                    elif media_type == "video":
                        width = int(params.get("width", 640))
                        height = int(params.get("height", 480))
                        fps = int(params.get("fps", 30))
                        is_keyframe = bool(params.get("keyframe", False))
                        self._media_bridge.send_video_frame(raw, width, height, fps, 0, is_keyframe)
                return {"ok": True}

            else:
                return {"ok": False, "error": f"Unknown action: {action}"}

        except Exception as e:
            traceback.print_exc()
            return {"ok": False, "error": str(e)}

    def _mock_command(self, action: str, params: dict) -> dict:
        """Mock mode for when NevoClient is unavailable."""
        import time, random
        if action == "login":
            self._send_event("state_changed", {"state": "connected", "old_state": "connecting"})
            self._send_event("channel_list", {"channels": [
                {"id": 1, "name": "日常", "parent_id": 0, "users": [
                    {"id": 2, "username": "Bob", "muted": False, "deafened": False, "group_id": 0},
                ]},
                {"id": 2, "name": "会议", "parent_id": 0, "users": []},
                {"id": 3, "name": "闲聊", "parent_id": 0, "users": [
                    {"id": 3, "username": "Charlie", "muted": True, "deafened": False, "group_id": 0},
                ]},
            ]})
            return {"ok": True, "user_id": 1, "username": params.get("username", ""),
                    "is_admin": False, "server_udp_port": 24432, "server_video_udp_port": 24433}
        if action == "disconnect":
            self._send_event("state_changed", {"state": "disconnected", "old_state": "connected"})
            return {"ok": True}
        if action == "join_channel":
            self._send_event("state_changed", {"state": "in_channel", "old_state": "connected"})
            return {"ok": True, "channel_id": params.get("channel_id"), "channel_name": "日常"}
        if action == "leave_channel":
            self._send_event("state_changed", {"state": "connected", "old_state": "in_channel"})
            return {"ok": True}
        if action == "send_chat":
            return {"ok": True}
        if action == "get_status":
            return {"ok": True, "connected": True, "in_channel": True,
                    "username": "MockUser", "channels": [], "channel_users": []}
        if action == "media_frame":
            # Mock 模式下直接丢弃媒体帧（无真实后端）
            return {"ok": True}
        return {"ok": True}

    def shutdown(self):
        self._alive = False
        # 停止媒体桥接
        if self._media_bridge:
            try:
                self._media_bridge.stop()
            except Exception:
                pass
            self._media_bridge = None
        if self._client:
            try:
                self._client.disconnect()
            except Exception:
                pass


# ============================================================
#  MediaBridge — 浏览器 WebCodecs 帧 ↔ NEVO UDP 语音/视频协议
# ============================================================

VIDEO_NAL_MAX_FRAGMENT = 1200
VIDEO_UDP_MAX_PACKET_SIZE = 1400
VIDEO_REASSEMBLY_MAX_AGE = 5.0
SPEAKING_TIMEOUT = 0.8  # 远端用户停止说话判定超时（秒）


class _FragmentReassembler:
    """视频 NAL 分片重组器，按 (sender_id, seq) 累积分片。"""

    def __init__(self):
        self._buffers = {}
        self._lock = threading.Lock()

    def add_fragment(self, sender_id, seq, frag_idx, frag_total, data):
        key = (sender_id, seq)
        with self._lock:
            if key not in self._buffers:
                self._buffers[key] = {
                    "total": frag_total,
                    "received": {},
                    "timestamp": time.time(),
                }
            self._buffers[key]["received"][frag_idx] = data
            if len(self._buffers[key]["received"]) == frag_total:
                nal = b"".join(
                    self._buffers[key]["received"][i]
                    for i in range(frag_total)
                )
                del self._buffers[key]
                return nal
        return None

    def cleanup_stale(self):
        now = time.time()
        with self._lock:
            stale = [
                k for k, v in self._buffers.items()
                if now - v["timestamp"] > VIDEO_REASSEMBLY_MAX_AGE
            ]
            for k in stale:
                del self._buffers[k]

    def reset_sender(self, sender_id):
        """丢弃某发送者的所有未完成分片（发送端重启后旧分片必须作废，
        否则会与新会话的相同序号分片混拼成损坏的 NAL）。"""
        with self._lock:
            stale = [k for k in self._buffers if k[0] == sender_id]
            for k in stale:
                del self._buffers[k]


class MediaBridge:
    """桥接浏览器 WebCodecs 编码帧到 NEVO UDP 语音/视频协议。"""

    def __init__(self, wfile, ws_lock, client, voice_sock=None, video_sock=None):
        self._wfile = wfile
        self._ws_lock = ws_lock
        self._client = client
        # 复用登录前预创建的 UDP 套接字（端口已随 LoginRequest 上报服务器）
        self._voice_sock = voice_sock
        self._video_sock = video_sock
        self._crypto = VoiceCrypto() if HAS_MEDIA else None
        self._voice_seq = 0
        self._video_seq = 0
        self._user_id = 0
        self._channel_id = 0
        self._call_id = 0
        self._server_voice_addr = None
        self._server_video_addr = None
        self._voice_recv_thread = None
        self._video_recv_thread = None
        self._keepalive_thread = None
        self._alive = True
        # 静音（不发送本端麦克风）/ 闭麦（同时不播放远端音频）
        # 由 handle_command 的 toggle_mute / toggle_deafen 驱动，
        # 媒体收发线程读取（Python 简单属性读写原子，GIL 保证可见性）
        self._muted = False
        self._deafened = False
        self._reassembler = _FragmentReassembler()
        # 远端用户说话状态检测（基于收到的音频包，与 PyQt 客户端 on_voice_received 行为一致）
        self._speaking_last_audio = {}   # user_id -> 最近收到音频的时间戳
        self._speaking_active = {}       # user_id -> 是否正在说话
        self._speaking_lock = threading.Lock()
        # TCP/UDP 双路去重：网关发送端对同一帧同时走 TCP 隧道与 UDP，
        # 服务端会把两条路径都中继过来（sequence_number 相同）。
        # 若不去重，浏览器收到 2 倍帧、抖动缓冲被灌满只能丢帧，语音卡顿。
        self._voice_seen = {}            # sender_id -> 最近已转发的序号
        self._seen_lock = threading.Lock()

    # ---- 静音 / 闭麦 ----

    def set_muted(self, muted: bool):
        """静音：停止发送本端麦克风音频（他人听不到本端）。"""
        self._muted = bool(muted)

    def set_deafened(self, deafened: bool):
        """闭麦：停止播放远端音频（听不到他人），并隐含静音本端。"""
        self._deafened = bool(deafened)
        if deafened:
            self._muted = True

    # ---- 生命周期 ----

    @staticmethod
    def pre_create_sockets():
        """预创建 UDP 媒体套接字，返回 (voice_sock, video_sock)。

        登录前调用：本地端口随 LoginRequest 上报服务器，服务端在登录时
        建立 UDP 端点映射（fail-closed 中继要求，未知端点一律丢弃）。
        """
        voice_sock = MediaBridge._create_dualstack_udp(256 * 1024)
        video_sock = None
        if voice_sock:
            video_sock = MediaBridge._create_dualstack_udp(512 * 1024)
        return voice_sock, video_sock

    def start(self, host, voice_port, video_port, user_id, session_key):
        """初始化 UDP 套接字并启动接收循环。"""
        self._user_id = user_id
        self._channel_id = 0
        # 设置加密会话密钥
        if self._crypto and session_key:
            self._crypto.set_session_key(bytes(session_key))
        # 注册 TCP 语音帧回调（外网/NAT 场景媒体走 TCP 控制连接）
        if self._client is not None:
            try:
                self._client.on_tcp_voice_frame = self._on_tcp_voice_frame
            except Exception:
                pass
        # 复用登录前预创建的语音套接字；未预创建时（旧调用路径）再创建
        if not self._voice_sock:
            self._voice_sock = self._create_dualstack_udp(256 * 1024)
        self._server_voice_addr = (host, voice_port)
        # 视频套接字（IPv6 双栈）
        if video_port and video_port > 0:
            if not self._video_sock:
                self._video_sock = self._create_dualstack_udp(512 * 1024)
            self._server_video_addr = (host, video_port)
        elif self._video_sock:
            # 服务器未提供视频端口时关闭预建视频套接字
            try:
                self._video_sock.close()
            except Exception:
                pass
            self._video_sock = None
        # 启动接收线程
        self._voice_recv_thread = threading.Thread(
            target=self._voice_recv_loop, daemon=True)
        self._voice_recv_thread.start()
        if self._video_sock:
            self._video_recv_thread = threading.Thread(
                target=self._video_recv_loop, daemon=True)
            self._video_recv_thread.start()
        # 启动 UDP 保活线程（每 15 秒发送注册包，防止 NAT 映射过期并提前注册端点）
        self._keepalive_thread = threading.Thread(
            target=self._keepalive_loop, daemon=True)
        self._keepalive_thread.start()
        print(f"[GATEWAY] MediaBridge started: voice={self._local_port(self._voice_sock)} "
              f"video={self._local_port(self._video_sock)} uid={user_id}")

    @staticmethod
    def _create_dualstack_udp(rcvbuf=256 * 1024):
        """创建 IPv6 双栈 UDP 套接字（V6ONLY=0，兼容 IPv4/IPv6）。"""
        try:
            sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
            sock.settimeout(1.0)
            sock.bind(("::", 0))
            return sock
        except Exception:
            pass
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
            sock.settimeout(1.0)
            sock.bind(("0.0.0.0", 0))
            return sock
        except Exception:
            return None

    @staticmethod
    def _local_port(sock):
        if sock:
            try:
                return sock.getsockname()[1]
            except Exception:
                pass
        return 0

    def _resolve_sendto(self, sock, addr):
        """IPv6 套接字发送到 IPv4 地址时需要映射为 IPv4-mapped IPv6。"""
        if not addr or not sock:
            return addr
        host, port = addr[0], addr[1]
        try:
            if sock.family == socket.AF_INET6 and '.' in host:
                return ('::ffff:' + host, port, 0, 0)
        except Exception:
            pass
        return addr

    def set_channel(self, channel_id):
        self._channel_id = channel_id
        if channel_id > 0:
            self._send_registration_packet()

    def set_call_id(self, call_id):
        self._call_id = call_id

    # ---- 会话密钥同步（注册为 NevoClient 媒体引擎后由客户端驱动） ----

    def set_session_key(self, key):
        """下发/重设会话密钥（登录或重协商）。"""
        if self._crypto:
            self._crypto.set_session_key(bytes(key))

    def rotate_session_key(self, key):
        """服务端密钥轮换：切换新密钥，旧密钥保留 KEY_OVERLAP_SECONDS 兜底。

        服务端每 600s 轮换一次每客户端会话密钥，并通过 KeyRotationRequest
        通知客户端；客户端（NevoClient）会把新密钥传播到注册的媒体引擎。
        此前 MediaBridge 未注册为引擎，轮换后仍用旧密钥加密/解密，
        服务端宽限期（20s）一过，本网关的所有语音/视频帧被中继丢弃。
        """
        if self._crypto:
            self._crypto.rotate_key(bytes(key))

    # ---- 发送（浏览器 → 后端 UDP）----

    def send_voice_frame(self, encoded_data):
        """将编码后的 Opus 数据包装为 VoicePacketHeader + 加密，经 TCP 发送。

        外网/NAT 场景（frp 内网穿透）UDP 回程不可靠，媒体帧优先走 TCP
        控制连接（与登录同链路，回程可靠）；同时保留 UDP 发送作为
        内网直连兜底（服务端中继会按接收者情况选择 TCP 或 UDP 下发）。
        """
        if not self._crypto:
            return
        # 静音：不发送本端麦克风音频（保活/注册包由独立线程发送，不受影响）
        if self._muted:
            return
        try:
            # 频道 ID 兜底：channel_list 回调与 MediaBridge 创建存在时序竞态，
            # 若本桥频道未同步则取客户端当前频道（登录后自动进入默认频道）
            channel_id = self._channel_id
            if channel_id == 0 and self._client is not None:
                try:
                    channel_id = self._client.current_channel_id
                except Exception:
                    channel_id = 0
            header = voice_pb2.VoicePacketHeader()
            header.sequence_number = self._voice_seq
            header.sender_id = self._user_id
            header.channel_id = channel_id
            header.timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            header.last_frame = False
            header.tcp_tunnel = True
            self._voice_seq += 1
            header_bytes = header.SerializeToString()
            # encrypt 返回 nonce + ciphertext + tag
            encrypted = self._crypto.encrypt(encoded_data, header_bytes)
            if not encrypted:
                return
            header_len_prefix = struct.pack('<H', len(header_bytes))
            packet = header_len_prefix + header_bytes + encrypted
            # TCP 发送（外网/NAT 可靠路径）
            if self._client is not None:
                self._client.send_voice_frame_tcp(packet)
            # UDP 兜底（内网直连场景）
            if self._voice_sock and self._server_voice_addr:
                self._voice_sock.sendto(
                    packet, self._resolve_sendto(self._voice_sock, self._server_voice_addr))
        except Exception:
            pass

    def send_video_frame(self, encoded_data, width, height, fps, frame_type, is_keyframe):
        """将编码后的 H.264 NAL 包装为 VideoPacketHeader + 加密 + UDP 发送。
        NAL 分片：超过 1200 字节时拆分。"""
        if not self._video_sock or not self._server_video_addr or not self._crypto:
            return
        try:
            # NAL 分片
            if len(encoded_data) <= VIDEO_NAL_MAX_FRAGMENT:
                fragments = [encoded_data]
            else:
                fragments = [
                    encoded_data[i:i + VIDEO_NAL_MAX_FRAGMENT]
                    for i in range(0, len(encoded_data), VIDEO_NAL_MAX_FRAGMENT)
                ]
            fragment_total = len(fragments)
            for idx, frag in enumerate(fragments):
                header = video_pb2.VideoPacketHeader()
                header.sequence_number = self._video_seq
                header.sender_id = self._user_id
                header.channel_id = self._channel_id
                header.timestamp = int(time.time() * 1000) & 0xFFFFFFFF
                header.frame_type = 0 if is_keyframe else 1
                header.fragment_index = idx
                header.fragment_total = fragment_total
                header.width = width
                header.height = height
                header.fps = fps
                header.call_id = self._call_id
                header.tcp_tunnel = False
                self._video_seq += 1
                header_bytes = header.SerializeToString()
                # 视频使用相同的 encrypt（与 decrypt_simple 配对）
                encrypted = self._crypto.encrypt(frag, header_bytes)
                if not encrypted:
                    continue
                header_len_prefix = struct.pack('<H', len(header_bytes))
                packet = header_len_prefix + header_bytes + encrypted
                if len(packet) > VIDEO_UDP_MAX_PACKET_SIZE:
                    continue
                try:
                    self._video_sock.sendto(
                        packet,
                        self._resolve_sendto(self._video_sock, self._server_video_addr))
                except Exception:
                    pass
        except Exception:
            pass

    # ---- UDP 注册 / 保活 ----

    def _send_registration_packet(self):
        """发送最小语音包作为 UDP 端点注册，让服务器提前建立映射。"""
        if not self._voice_sock or not self._server_voice_addr or not self._crypto:
            return
        try:
            # 频道 ID 兜底（与 send_voice_frame 一致，避免时序竞态导致 channel=0）
            channel_id = self._channel_id
            if channel_id == 0 and self._client is not None:
                try:
                    channel_id = self._client.current_channel_id
                except Exception:
                    channel_id = 0
            header = voice_pb2.VoicePacketHeader()
            header.sequence_number = self._voice_seq
            header.sender_id = self._user_id
            header.channel_id = channel_id
            header.timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            header.last_frame = True
            header.tcp_tunnel = False
            self._voice_seq += 1
            header_bytes = header.SerializeToString()
            # payload 为空，只加密空明文（得到 nonce + tag）
            encrypted = self._crypto.encrypt(b"", header_bytes)
            if not encrypted:
                return
            header_len_prefix = struct.pack('<H', len(header_bytes))
            packet = header_len_prefix + header_bytes + encrypted
            self._voice_sock.sendto(
                packet, self._resolve_sendto(self._voice_sock, self._server_voice_addr))
        except Exception:
            pass

    def _keepalive_loop(self):
        """每 15 秒发送一次 UDP 注册包，防止 NAT 重绑定或映射过期。"""
        next_send = time.time() + 15.0
        while self._alive:
            now = time.time()
            if now >= next_send:
                if self._user_id > 0 and self._channel_id > 0:
                    self._send_registration_packet()
                next_send = now + 15.0
            # _check_speaking_timeouts 已停用：说话状态由服务端 USER_SPEAKING 广播驱动
            time.sleep(0.5)

    # ---- 接收（后端 UDP → 浏览器 WS）----

    # 同流内乱序/重复的序号回退幅度很小（双路中继最多相差几帧）；
    # 回退超过该阈值视为发送端已重启（新会话序号从 0 重新计数）。
    _SEEN_RESET_GAP = 256

    def _is_duplicate(self, sender_id, seq):
        """TCP/UDP 双路中继去重：同一 (sender_id, seq) 只转发一次。

        发送端（对端用户）重启软件后，其新会话的序号从 0 重新计数。
        若仍按旧基线判断，新会话的全部帧会被误判为重复帧丢弃
        （"A 重启后 B 听不到 A，B 重启才恢复"的根因）。因此：
        小幅回退 = 同流内的乱序/重复（丢弃）；大幅回退 = 新会话
        （重置基线并放行）。
        """
        with self._seen_lock:
            last = self._voice_seen.get(sender_id, -1)
            if seq <= last:
                if last - seq >= self._SEEN_RESET_GAP:
                    self._voice_seen[sender_id] = seq
                    return False
                return True
            self._voice_seen[sender_id] = seq
            return False

    def reset_sender(self, sender_id):
        """发送端会话重建（对端重启软件/重新入频道）后重置其去重基线，
        并清空其视频分片重组缓冲，避免新旧会话的相同序号分片被混拼。"""
        with self._seen_lock:
            self._voice_seen.pop(sender_id, None)
        self._reassembler.reset_sender(sender_id)

    def _on_tcp_voice_frame(self, payload: bytes):
        """接收 TCP 语音帧（服务端中继经 TCP 控制连接下发），解密并转发浏览器。

        外网/NAT 场景（frp 内网穿透）UDP 回程不可靠，媒体帧随 TCP 控制
        连接到达，解析逻辑与 UDP 语音包一致（2B 头长 + protobuf 头 + 密文）。
        """
        if not self._alive or not self._crypto:
            return
        try:
            if len(payload) < 2:
                return
            header_size = struct.unpack_from('<H', payload, 0)[0]
            if header_size == 0 or 2 + header_size > len(payload):
                return
            header = voice_pb2.VoicePacketHeader()
            header.ParseFromString(payload[2:2 + header_size])
            if header.sender_id == 0:
                return
            payload_body = payload[2 + header_size:]
            header_aad = payload[2:2 + header_size]
            plaintext = self._crypto.decrypt(payload_body, header_aad=header_aad)
            if plaintext is None:
                return
            # 空载荷 = 对端的 UDP 注册/保活包（仅用于端点注册，无音频内容）：
            # 不能转发给浏览器，否则 AudioDecoder 解空 chunk 报错并永久关闭
            # （表现为"一段时间后无声音"）。
            if len(plaintext) == 0:
                return
            # 闭麦：不播放远端音频（不转发给浏览器）
            if self._deafened:
                return
            # TCP/UDP 双路去重（同一帧经两条路径各中继一次）
            if not self._is_duplicate(header.sender_id, header.sequence_number):
                # 通过 WS 事件发送给浏览器
                self._send_media_event("voice_frame", {
                    "sender_id": header.sender_id,
                    "data": base64.b64encode(plaintext).decode('ascii'),
                })
        except Exception:
            pass

    def _voice_recv_loop(self):
        """接收 UDP 语音包，解密，通过 WS 发回浏览器。"""
        while self._alive:
            if not self._voice_sock:
                time.sleep(0.1)
                continue
            try:
                data, addr = self._voice_sock.recvfrom(2048)
                if len(data) < 2:
                    continue
                header_size = struct.unpack_from('<H', data, 0)[0]
                if header_size == 0 or 2 + header_size > len(data):
                    continue
                header = voice_pb2.VoicePacketHeader()
                header.ParseFromString(data[2:2 + header_size])
                # 跳过无效发送者（服务端中继已排除发送者自身端点，不会回传本网关的包；
                # 同账号多设备场景下 sender_id 可能等于本网关 user_id，必须放行）
                if header.sender_id == 0:
                    continue
                payload = data[2 + header_size:]
                header_aad = data[2:2 + header_size]
                plaintext = self._crypto.decrypt(payload, header_aad=header_aad)
                if plaintext is None:
                    continue
                # 空载荷（注册/保活包）不转发给浏览器（原因同 TCP 语音路径）
                if len(plaintext) == 0:
                    continue
                # TCP/UDP 双路去重（与 TCP 语音路径共享去重状态）
                if self._is_duplicate(header.sender_id, header.sequence_number):
                    continue
                # 闭麦：不播放远端音频（不转发给浏览器）
                if self._deafened:
                    continue
                # 检测到远端有效音频（非空载荷，排除保活空包）
                # 注意：说话状态由服务端 USER_SPEAKING 广播统一驱动，
                # MediaBridge 不再发 user_speaking 事件，避免两条路径冲突导致波形闪烁。
                if len(plaintext) > 0:
                    pass  # _note_remote_audio 已停用
                # 通过 WS 事件发送给浏览器
                self._send_media_event("voice_frame", {
                    "sender_id": header.sender_id,
                    "data": base64.b64encode(plaintext).decode('ascii'),
                })
            except socket.timeout:
                continue
            except OSError:
                break
            except Exception:
                continue

    # ---- 远端说话状态检测 ----

    def _note_remote_audio(self, user_id):
        """检测到远端用户音频，首次触发时上报说话开始事件。"""
        now = time.time()
        emit = False
        with self._speaking_lock:
            self._speaking_last_audio[user_id] = now
            if not self._speaking_active.get(user_id):
                self._speaking_active[user_id] = True
                emit = True
        if emit:
            self._send_media_event("user_speaking",
                                   {"user_id": user_id, "speaking": True})

    def _check_speaking_timeouts(self):
        """检查远端用户说话超时，超时则上报说话停止事件。"""
        now = time.time()
        to_clear = []
        with self._speaking_lock:
            for uid, active in list(self._speaking_active.items()):
                if active and now - self._speaking_last_audio.get(uid, 0) > SPEAKING_TIMEOUT:
                    self._speaking_active[uid] = False
                    to_clear.append(uid)
        for uid in to_clear:
            self._send_media_event("user_speaking",
                                   {"user_id": uid, "speaking": False})

    def _video_recv_loop(self):
        """接收 UDP 视频包，解密，分片重组，通过 WS 发回浏览器。"""
        while self._alive:
            if not self._video_sock:
                time.sleep(0.1)
                continue
            try:
                data, addr = self._video_sock.recvfrom(VIDEO_UDP_MAX_PACKET_SIZE)
                if len(data) < 2:
                    continue
                header_size = struct.unpack_from('<H', data, 0)[0]
                if header_size == 0 or 2 + header_size > len(data):
                    continue
                header = video_pb2.VideoPacketHeader()
                header.ParseFromString(data[2:2 + header_size])
                # 跳过无效发送者（服务端中继已排除发送者自身端点，不会回传本网关的包；
                # 同账号多设备场景下 sender_id 可能等于本网关 user_id，必须放行）
                if header.sender_id == 0:
                    continue
                payload = data[2 + header_size:]
                header_aad = data[2:2 + header_size]
                # 视频使用 decrypt_simple
                plaintext = self._crypto.decrypt_simple(payload, header_aad)
                if plaintext is None:
                    continue
                # 空载荷不转发（与语音路径一致，避免解码器进入错误状态）
                if len(plaintext) == 0:
                    continue
                seq = header.sequence_number
                frag_idx = header.fragment_index
                frag_total = header.fragment_total
                # 单分片直接处理；多分片重组
                if frag_total <= 1:
                    self._send_video_frame_event(header, plaintext)
                else:
                    nal = self._reassembler.add_fragment(
                        header.sender_id, seq, frag_idx, frag_total, plaintext)
                    if nal is not None:
                        self._send_video_frame_event(header, nal)
                # 定期清理过期分片缓冲
                if self._video_seq % 100 == 0:
                    self._reassembler.cleanup_stale()
            except socket.timeout:
                continue
            except OSError:
                break
            except Exception:
                continue

    def _send_video_frame_event(self, header, nal_data):
        """将重组后的视频 NAL 通过 WS 事件发送给浏览器。"""
        self._send_media_event("video_frame", {
            "sender_id": header.sender_id,
            "width": header.width,
            "height": header.height,
            "fps": header.fps,
            "frame_type": header.frame_type,
            "keyframe": header.frame_type == 0,  # 0=KeyFrame, 1=DeltaFrame
            "data": base64.b64encode(nal_data).decode('ascii'),
        })

    # ---- WS 事件辅助 ----

    def _send_media_event(self, event_type, data):
        """通过 WebSocket 发送媒体事件给浏览器。"""
        if not self._alive:
            return
        msg = json.dumps({"event": event_type, "data": data}, ensure_ascii=False)
        try:
            ws_send_text(self._wfile, msg, self._ws_lock)
        except Exception:
            self._alive = False

    # ---- 停止 ----

    def stop(self):
        """关闭套接字和接收线程。"""
        self._alive = False
        if self._voice_sock:
            try:
                self._voice_sock.close()
            except Exception:
                pass
            self._voice_sock = None
        if self._video_sock:
            try:
                self._video_sock.close()
            except Exception:
                pass
            self._video_sock = None
        if self._voice_recv_thread and self._voice_recv_thread.is_alive():
            self._voice_recv_thread.join(timeout=3.0)
        self._voice_recv_thread = None
        if self._video_recv_thread and self._video_recv_thread.is_alive():
            self._video_recv_thread.join(timeout=3.0)
        self._video_recv_thread = None
        if self._keepalive_thread and self._keepalive_thread.is_alive():
            self._keepalive_thread.join(timeout=3.0)
        self._keepalive_thread = None
        self._reassembler.cleanup_stale()
        with self._speaking_lock:
            self._speaking_active.clear()
            self._speaking_last_audio.clear()


# ============================================================
#  HTTP + WebSocket handler
# ============================================================

class GatewayHandler(SimpleHTTPRequestHandler):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_ROOT, **kwargs)

    def log_message(self, fmt, *args):
        import time
        ts = time.strftime("%H:%M:%S")
        sys.stdout.write(f"[{ts}] {args[0]}\n")
        sys.stdout.flush()

    def end_headers(self):
        # 禁止浏览器缓存静态资源，确保前端更新后能立即生效
        if not self.path.startswith("/ws"):
            self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
            self.send_header("Pragma", "no-cache")
            self.send_header("Expires", "0")
        super().end_headers()

    def do_GET(self):
        # WebSocket upgrade
        if self.path == "/ws" or self.path.startswith("/ws?"):
            self._handle_ws_upgrade()
        else:
            super().do_GET()

    def _handle_ws_upgrade(self):
        # Validate WebSocket headers
        upgrade = self.headers.get("Upgrade", "").lower()
        ws_key = self.headers.get("Sec-WebSocket-Key", "")
        if "websocket" not in upgrade or not ws_key:
            self.send_response(400)
            self.end_headers()
            return

        accept = ws_accept_key(ws_key)
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()

        # Create client bridge
        ws_lock = threading.Lock()
        bridge = ClientBridge(self.wfile, ws_lock)

        # Send connected event
        ws_send_text(self.wfile, json.dumps({"event": "ws_connected", "data": {
            "client_available": CLIENT_AVAILABLE,
        }}, ensure_ascii=False), ws_lock)

        # Main read loop
        try:
            while True:
                raw = ws_read_frame(self.rfile)
                if raw is None:
                    break
                try:
                    msg = json.loads(raw)
                    action = msg.get("action", "")
                    params = msg.get("params", {})
                    req_id = msg.get("id")

                    # Run command in a thread to avoid blocking the read loop
                    response = bridge.handle_command(action, params)
                    response["id"] = req_id
                    ws_send_text(self.wfile, json.dumps(response, ensure_ascii=False), ws_lock)

                except json.JSONDecodeError:
                    ws_send_text(self.wfile, json.dumps({
                        "error": "Invalid JSON", "raw": raw[:200],
                    }, ensure_ascii=False), ws_lock)
                except Exception as e:
                    traceback.print_exc()
                    ws_send_text(self.wfile, json.dumps({
                        "error": str(e),
                    }, ensure_ascii=False), ws_lock)

        except Exception:
            pass
        finally:
            bridge.shutdown()
            ws_send_close(self.wfile, ws_lock)


class ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


def main():
    print("=" * 56)
    print("  NEVO Web Client Gateway")
    print("=" * 56)
    print(f"  Web UI:    http://{HOST}:{WEB_PORT}")
    print(f"  WebSocket: ws://{HOST}:{WEB_PORT}/ws")
    print(f"  Root:      {WEB_ROOT}")
    print(f"  Backend:   {'NevoClient (real)' if CLIENT_AVAILABLE else 'Mock mode'}")
    print("=" * 56)
    print("\n  Press Ctrl+C to stop.\n")

    server = ThreadingHTTPServer((HOST, WEB_PORT), GatewayHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.shutdown()


if __name__ == "__main__":
    main()
