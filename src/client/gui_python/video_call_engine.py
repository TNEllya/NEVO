"""一对一视频通话媒体引擎。

参考 video_engine.py 的 UDP 包格式（VideoPacketHeader + XChaCha20-Poly1305），
但专为一对一视频通话设计：
- 管理本地 CameraCapture、VideoEncoder、VideoDecoder
- 仅处理 header.call_id == self._call_id 的数据包
- 使用独立的 UDP socket 端口
"""
import socket
import struct
import threading
import time
import traceback
from typing import Optional, Callable, Tuple

import numpy as np

from camera_capture import CameraCapture
from video_encoder import VideoEncoder, VideoDecoder
from voice_crypto import VoiceCrypto, XCHACHA_NONCE_SIZE, POLY1305_TAG_SIZE
from proto import video_pb2

VIDEO_UDP_MAX_PACKET_SIZE = 1400
VIDEO_NAL_MAX_FRAGMENT = 1200
VIDEO_REASSEMBLY_MAX_AGE = 5.0
VIDEO_CALL_FPS = 30


def _vlog(msg: str):
    """视频通话引擎日志。"""
    line = f"[VIDEO_CALL] {msg}"
    print(line)


def _vlog_exc(exc):
    tb = traceback.format_exc()
    _vlog(f"EXCEPTION: {exc}\n{tb}")


class _FragmentReassembler:
    """NAL 分片重组器。"""

    def __init__(self):
        self._buffers = {}
        self._lock = threading.Lock()

    def add_fragment(self, seq: int, frag_idx: int, frag_total: int, data: bytes) -> Optional[bytes]:
        with self._lock:
            if seq not in self._buffers:
                self._buffers[seq] = {
                    "total": frag_total,
                    "received": {},
                    "timestamp": time.time(),
                }
            self._buffers[seq]["received"][frag_idx] = data
            if len(self._buffers[seq]["received"]) == frag_total:
                nal = b"".join(
                    self._buffers[seq]["received"][i]
                    for i in range(frag_total)
                )
                del self._buffers[seq]
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


class VideoCallEngine:
    """一对一视频通话引擎。"""

    def __init__(self):
        _vlog("Initialized")
        self._camera = CameraCapture()
        self._encoder = VideoEncoder()
        self._decoder = VideoDecoder()
        self._crypto = VoiceCrypto()

        self._udp_sock: Optional[socket.socket] = None
        self._server_udp_addr: Optional[Tuple[str, int]] = None
        self._running = False
        self._in_call = False
        self._muted_video = False

        self._call_id: int = 0
        self._user_id: int = 0
        self._peer_id: int = 0
        self._sequence: int = 0

        self._profile = None
        self._capture_thread: Optional[threading.Thread] = None
        self._recv_thread: Optional[threading.Thread] = None
        self._reassembler = _FragmentReassembler()

        # 回调
        self.on_video_frame: Optional[Callable[[int, np.ndarray, int, int], None]] = None
        self.on_error: Optional[Callable[[str], None]] = None

    def set_server_udp(self, host: str, port: int):
        """设置视频通话 UDP 服务器地址。"""
        old = self._server_udp_addr
        self._server_udp_addr = (host, port)
        _vlog(f"set_server_udp: {old} -> {self._server_udp_addr}")

    def set_session_key(self, key: bytes):
        """设置加密会话密钥。"""
        if isinstance(key, (bytes, bytearray)):
            self._crypto.set_session_key(key)

    def rotate_session_key(self, key: bytes):
        """轮换加密会话密钥。"""
        if isinstance(key, (bytes, bytearray)):
            self._crypto.rotate_key(key)

    @staticmethod
    def _create_dualstack_udp(rcvbuf=512 * 1024):
        """创建双栈 UDP socket。"""
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

    def pre_create_udp_socket(self) -> bool:
        """预创建 UDP socket（登录时上报端口）。"""
        if self._udp_sock is not None:
            return True
        self._udp_sock = self._create_dualstack_udp(512 * 1024)
        if self._udp_sock:
            _vlog(f"pre_create_udp_socket OK, port={self.local_udp_port}")
            return True
        _vlog("pre_create_udp_socket FAILED")
        return False

    @property
    def local_udp_port(self) -> int:
        if self._udp_sock:
            try:
                return self._udp_sock.getsockname()[1]
            except Exception:
                pass
        return 0

    def start_call(self, call_id: int, server_addr: Tuple[str, int],
                   user_id: int, session_key: bytes,
                   profile=None, camera_index: int = 0) -> Tuple[bool, str]:
        """启动一对一视频通话媒体通道。

        参数:
            call_id: 通话唯一标识
            server_addr: (host, port) 视频通话 UDP 服务器地址
            user_id: 当前用户 ID
            session_key: 会话密钥
            profile: VideoProfile，若为空则使用默认 640x480@30fps
            camera_index: 摄像头设备索引
        返回:
            (success, error_message)
        """
        _vlog(f"start_call: call_id={call_id}, server={server_addr}, user={user_id}")
        if self._in_call:
            return False, "Already in a video call"
        if not CameraCapture.is_available():
            return False, f"OpenCV not available: {self._camera_error()}"
        if not VideoEncoder.is_available():
            return False, "Video encoder (PyAV) not available"
        if not server_addr or server_addr[1] <= 0:
            return False, "Invalid server address"
        if not session_key or len(session_key) < 32:
            return False, "Session key not available"

        try:
            self._call_id = call_id
            self._server_udp_addr = server_addr
            self._user_id = user_id
            self._sequence = 0
            self._profile = profile

            if not self.pre_create_udp_socket():
                return False, "Failed to create UDP socket"

            width = profile.width if profile else 640
            height = profile.height if profile else 480
            fps = profile.fps if profile else VIDEO_CALL_FPS
            bitrate = (profile.target_bitrate_kbps * 1000) if profile else 1000000

            # 对齐分辨率到 16 的倍数（H.264 要求）
            width = (width // 16) * 16
            height = (height // 16) * 16
            if width < 16 or height < 16:
                return False, "Invalid video resolution"

            if not self._camera.start(camera_index, width, height, fps):
                return False, "Failed to start camera"

            # 等待摄像头首帧
            for _ in range(50):
                frame = self._camera.capture_frame()
                if frame is not None:
                    break
                time.sleep(0.02)
            if frame is None:
                self._camera.stop()
                return False, "Camera did not produce frames"

            if not self._encoder.init(width, height, fps, bitrate):
                self._camera.stop()
                return False, "Failed to initialize video encoder"

            self._crypto.set_session_key(session_key)

            self._running = True
            self._in_call = True
            self._muted_video = False

            self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
            self._recv_thread.start()

            self._capture_thread = threading.Thread(target=self._capture_loop, daemon=True)
            self._capture_thread.start()

            _vlog("start_call SUCCESS")
            return True, ""
        except Exception as e:
            _vlog_exc(e)
            self.stop_call()
            return False, str(e)

    def stop_call(self):
        """停止视频通话并释放资源。"""
        _vlog("stop_call")
        self._in_call = False
        self._running = False
        self._muted_video = False

        if self._capture_thread and self._capture_thread.is_alive():
            self._capture_thread.join(timeout=3.0)
        self._capture_thread = None

        self._camera.stop()

        if self._encoder:
            try:
                nals = self._encoder.flush()
                for nal in nals:
                    self._send_video_nal(nal, frame_type=0)
            except Exception:
                pass
            self._encoder.close()

        if self._decoder:
            self._decoder.close()

        if self._recv_thread and self._recv_thread.is_alive():
            self._recv_thread.join(timeout=3.0)
        self._recv_thread = None

        if self._udp_sock:
            try:
                self._udp_sock.close()
            except Exception:
                pass
            self._udp_sock = None

        self._server_udp_addr = None
        self._call_id = 0
        self._peer_id = 0
        self._profile = None
        self._reassembler.cleanup_stale()
        _vlog("stop_call done")

    def set_camera_device(self, index: int):
        """通话中切换摄像头。"""
        if not self._in_call:
            return False
        try:
            width = self._camera.width
            height = self._camera.height
            fps = self._camera.fps
            return self._camera.start(index, width, height, fps)
        except Exception as e:
            _vlog(f"set_camera_device failed: {e}")
            return False

    def set_muted_video(self, muted: bool):
        """暂停/恢复本地视频发送。"""
        self._muted_video = muted

    @property
    def is_in_call(self) -> bool:
        return self._in_call

    @property
    def call_id(self) -> int:
        return self._call_id

    def close(self):
        """彻底关闭引擎。"""
        self.stop_call()

    def _capture_loop(self):
        """本地摄像头采集编码发送循环。"""
        _vlog("capture_loop started")
        last_time = time.time()
        frame_count = 0
        while self._in_call and self._camera.is_running():
            if self._muted_video:
                time.sleep(0.05)
                continue

            frame = self._camera.capture_frame()
            if frame is not None:
                # 本地预览回调
                if self.on_video_frame:
                    try:
                        h, w = frame.shape[:2]
                        self.on_video_frame(self._user_id, frame.copy(), w, h)
                    except Exception as e:
                        _vlog_exc(e)

                nals = self._encoder.encode(frame)
                frame_count += 1
                for i, nal in enumerate(nals):
                    is_keyframe = (i == 0)
                    self._send_video_nal(nal, frame_type=0 if is_keyframe else 1)

            elapsed = time.time() - last_time
            sleep_time = (1.0 / self._camera.fps) - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)
            last_time = time.time()

        _vlog(f"capture_loop exited, frames={frame_count}")

    def _send_video_nal(self, nal_data: bytes, frame_type: int = 1):
        if not self._udp_sock or not self._server_udp_addr:
            return
        if not self._crypto._key or self._crypto._key == bytes(32):
            return

        header = video_pb2.VideoPacketHeader()
        header.sequence_number = self._sequence
        header.sender_id = self._user_id
        header.channel_id = 0
        header.call_id = self._call_id
        header.timestamp = int(time.time() * 1000) & 0xFFFFFFFF
        header.frame_type = frame_type
        header.width = self._encoder.width
        header.height = self._encoder.height
        header.fps = self._camera.fps

        if len(nal_data) <= VIDEO_NAL_MAX_FRAGMENT:
            header.fragment_index = 0
            header.fragment_total = 1
            self._send_packet(header, nal_data)
        else:
            total = (len(nal_data) + VIDEO_NAL_MAX_FRAGMENT - 1) // VIDEO_NAL_MAX_FRAGMENT
            header.fragment_total = total
            for i in range(total):
                start = i * VIDEO_NAL_MAX_FRAGMENT
                end = min(start + VIDEO_NAL_MAX_FRAGMENT, len(nal_data))
                header.fragment_index = i
                self._send_packet(header, nal_data[start:end])

        self._sequence += 1

    def _send_packet(self, header, payload: bytes):
        header_bytes = header.SerializeToString()
        encrypted = self._crypto.encrypt(payload, header_bytes)
        if not encrypted or len(encrypted) == 0:
            return

        header_len_prefix = struct.pack("<H", len(header_bytes))
        packet = header_len_prefix + header_bytes + encrypted
        if len(packet) > VIDEO_UDP_MAX_PACKET_SIZE:
            return

        try:
            self._udp_sock.sendto(packet, self._server_udp_addr)
        except Exception:
            pass

    def _recv_loop(self):
        """接收远端视频包循环。"""
        _vlog("recv_loop started")
        pkt_count = 0
        error_count = 0
        while self._running:
            if not self._udp_sock:
                time.sleep(0.05)
                continue
            try:
                data, addr = self._udp_sock.recvfrom(VIDEO_UDP_MAX_PACKET_SIZE)
                pkt_count += 1
                if data:
                    self._handle_received_packet(data, addr)
            except socket.timeout:
                continue
            except OSError:
                break
            except Exception as e:
                error_count += 1
                if error_count <= 5:
                    _vlog_exc(e)
                time.sleep(0.01)

        _vlog(f"recv_loop exited, packets={pkt_count}, errors={error_count}")
        self._reassembler.cleanup_stale()

    def _handle_received_packet(self, data: bytes, addr=None):
        try:
            if len(data) < 2:
                return

            header_size = struct.unpack_from("<H", data, 0)[0]
            if header_size == 0 or 2 + header_size > len(data):
                return

            header = video_pb2.VideoPacketHeader()
            header.ParseFromString(data[2: 2 + header_size])

            # 一对一视频通话：只处理相同 call_id 的包
            if header.call_id != self._call_id:
                return

            sender_id = header.sender_id
            if sender_id == 0 or sender_id == self._user_id:
                return

            self._peer_id = sender_id

            payload = data[2 + header_size:]
            if len(payload) < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE:
                return

            header_aad = data[2: 2 + header_size]
            plaintext = self._crypto.decrypt_simple(payload, header_aad)
            if plaintext is None:
                _vlog(f"decrypt failed: sender={sender_id}, seq={header.sequence_number}")
                return

            seq = header.sequence_number
            frag_idx = header.fragment_index
            frag_total = header.fragment_total

            if frag_total <= 1:
                self._process_nal(sender_id, plaintext, header)
            else:
                nal = self._reassembler.add_fragment(seq, frag_idx, frag_total, plaintext)
                if nal is not None:
                    self._process_nal(sender_id, nal, header)
        except Exception as e:
            _vlog_exc(e)

    def _process_nal(self, sender_id: int, nal_data: bytes, header):
        if header.width <= 0 or header.height <= 0:
            return
        try:
            if self._decoder._codec_context is None or \
                    self._decoder._width != header.width or \
                    self._decoder._height != header.height:
                if not self._decoder.init(header.width, header.height):
                    return

            frame = self._decoder.decode(nal_data)
            if frame is not None and self.on_video_frame:
                self.on_video_frame(sender_id, frame, header.width, header.height)
        except Exception as e:
            _vlog_exc(e)

    @staticmethod
    def _camera_error() -> str:
        from camera_capture import _CV2_ERROR
        return _CV2_ERROR
