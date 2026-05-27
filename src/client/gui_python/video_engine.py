import socket
import struct
import threading
import time
import traceback

from voice_crypto import VoiceCrypto, XCHACHA_NONCE_SIZE, POLY1305_TAG_SIZE
from screen_capture import ScreenCapture, SOURCE_SCREEN, SOURCE_WINDOW
from video_encoder import VideoEncoder, VideoDecoder, SCREEN_SHARE_FPS
from proto import video_pb2

VIDEO_UDP_MAX_PACKET_SIZE = 1400
VIDEO_NAL_MAX_FRAGMENT = 1200
VIDEO_REASSEMBLY_MAX_AGE = 5.0
_VIDEO_LOG_FILE = None

def _vlog(msg):
    """Log to both console and a file for diagnostics."""
    global _VIDEO_LOG_FILE
    line = f"[VIDEO] {msg}\n"
    print(line, end="")
    try:
        if _VIDEO_LOG_FILE is None:
            import os
            log_dir = os.path.expanduser("~")
            _VIDEO_LOG_FILE = open(os.path.join(log_dir, "nevo_video_debug.log"), "w")
        _VIDEO_LOG_FILE.write(line)
        _VIDEO_LOG_FILE.flush()
    except Exception:
        pass

def _vlog_exc(exc):
    tb = traceback.format_exc()
    _vlog(f"EXCEPTION: {exc}\n{tb}")


class FragmentReassembler:
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


class VideoEngine:
    def __init__(self):
        _vlog("=" * 60)
        _vlog("[VIDEO ENGINE] INITIALIZED - v2025.05 (cross-device fix)")
        _vlog("=" * 60)
        self._capture = ScreenCapture()
        self._encoder = VideoEncoder()
        self._crypto = VoiceCrypto()
        self._udp_sock = None
        self._server_udp_addr = None
        self._running = False
        self._sharing = False
        self._user_id = 0
        self._channel_id = 0
        self._sequence = 0
        self._capture_thread = None
        self._recv_thread = None
        self._reassembler = FragmentReassembler()
        self._decoders = {}
        self._decoders_lock = threading.Lock()

        self.on_video_frame = None
        self.on_share_state_changed = None

    def set_server_udp(self, host, port):
        old = self._server_udp_addr
        self._server_udp_addr = (host, port)
        _vlog(f"[INIT] set_server_udp: {old} -> {self._server_udp_addr}")

    def set_user_info(self, user_id, channel_id):
        _vlog(f"[INIT] set_user_info: user_id={user_id}, channel_id={channel_id} (was {self._user_id}/{self._channel_id})")
        self._user_id = user_id
        self._channel_id = channel_id
        if self._running and self._udp_sock and self._server_udp_addr:
            self._send_registration_packet()

    def set_session_key(self, key):
        if isinstance(key, (bytes, bytearray)):
            self._crypto.set_session_key(key)

    def _send_registration_packet(self):
        if not self._udp_sock or not self._server_udp_addr:
            return
        try:
            header = video_pb2.VideoPacketHeader()
            header.sequence_number = self._sequence
            header.sender_id = self._user_id
            header.channel_id = self._channel_id
            header.timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            header.width = 0
            header.height = 0
            header.fps = 0
            header.frame_type = 0
            self._sequence += 1

            header_bytes = header.SerializeToString()
            encrypted = self._crypto.encrypt(b"", header_bytes)
            if not encrypted:
                return

            header_len_prefix = struct.pack('<H', len(header_bytes))
            packet = header_len_prefix + header_bytes + encrypted
            self._udp_sock.sendto(packet, self._server_udp_addr)
            _vlog(f"Registration packet sent to {self._server_udp_addr}")
        except Exception as e:
            _vlog_exc(e)

    @staticmethod
    def _create_dualstack_udp(rcvbuf=512 * 1024):
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

    def pre_create_udp_socket(self):
        if self._udp_sock is not None:
            return True
        self._udp_sock = self._create_dualstack_udp(512 * 1024)
        if self._udp_sock:
            _vlog(f"[RECV] pre_create_udp_socket SUCCESS: port={self.local_udp_port}")
            return True
        else:
            _vlog("[RECV] pre_create_udp_socket FAILED")
            return False

    @property
    def local_udp_port(self):
        if self._udp_sock:
            try:
                return self._udp_sock.getsockname()[1]
            except Exception:
                pass
        return 0

    def start_receive(self):
        if self._recv_thread is not None and self._recv_thread.is_alive():
            _vlog("[RECV] already running")
            return
        if self._udp_sock is None:
            _vlog("[RECV] start_receive: UDP socket not ready, attempting to create...")
            if not self.pre_create_udp_socket():
                _vlog("[RECV] start_receive FAILED: could not create UDP socket")
                return
        if self._udp_sock is None:
            _vlog("[RECV] start_receive failed: no UDP socket")
            return
        self._running = True
        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()
        _vlog(f"[RECV] receive thread started, port={self.local_udp_port}")
        if self._channel_id > 0:
            self._send_registration_packet()

    def stop_receive(self):
        self._running = False
        if self._recv_thread and self._recv_thread.is_alive():
            self._recv_thread.join(timeout=3.0)
        self._recv_thread = None

    def start_share(self, source_type=SOURCE_SCREEN, source_index=0, fps=SCREEN_SHARE_FPS):
        _vlog(f"=== start_share called: type={source_type}, idx={source_index}, fps={fps} ===")
        _vlog(f"[DIAG] user_id={self._user_id}, channel_id={self._channel_id}")
        _vlog(f"[DIAG] server_udp_addr={self._server_udp_addr}")
        _vlog(f"[DIAG] has_crypto_key={self._crypto._key is not None and self._crypto._key != bytes(32)}")
        _vlog(f"[DIAG] udp_socket={'OK port=' + str(self.local_udp_port) if self._udp_sock else 'NONE'}")
        try:
            if self._sharing:
                _vlog("FAIL: already sharing")
                return False, "Already sharing"
            if not ScreenCapture.is_available():
                err_detail = getattr(ScreenCapture, '_MSS_ERROR', 'unknown')
                _vlog(f"FAIL: ScreenCapture not available (mss) — {err_detail}")
                return False, f"Screen capture library (mss) not available: {err_detail}"
            if not VideoEncoder.is_available():
                _vlog("FAIL: VideoEncoder not available (av)")
                return False, "Video encoding library (PyAV) not available"
            if not self._server_udp_addr:
                _vlog(f"FAIL: _server_udp_addr is None or empty! Cannot send video without server address")
                return False, "Video server address not configured. Are you connected to a server?"
            if not self._udp_sock:
                _vlog("FAIL: UDP socket not created")
                return False, "UDP socket not initialized"

            key_valid = self._crypto._key is not None and self._crypto._key != bytes(32)
            if not key_valid:
                _vlog("FAIL: encryption key not set")
                return False, "Session key not available. Are you logged in?"

            # Ensure UDP socket is created for sending AND receiving
            self.pre_create_udp_socket()
            if self._udp_sock is None:
                _vlog("FAIL: failed to create UDP socket")
                return False, "Failed to create UDP socket"
            _vlog(f"UDP socket ready, port={self.local_udp_port}, server={self._server_udp_addr}")

            self._capture.start(source_type, source_index, fps)
            _vlog("capture.start() done")

            frame = self._capture.capture_frame()
            _vlog(f"capture_frame returned: {type(frame).__name__}, shape={getattr(frame,'shape','N/A')}")
            if frame is None:
                _vlog("FAIL: capture_frame returned None")
                self._capture.stop()
                return False, "Failed to capture screen frame"

            h, w = frame.shape[:2]
            _vlog(f"frame OK: {w}x{h}, calling encoder.init...")

            if not self._encoder.init(w, h, fps):
                _vlog("FAIL: encoder.init() returned False")
                self._capture.stop()
                return False, "Failed to initialize video encoder"
            _vlog("encoder.init() OK")

            # Also ensure receive thread is running
            if self._recv_thread is None or not self._recv_thread.is_alive():
                _vlog("Starting receive thread from start_share")
                self._running = True
                self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
                self._recv_thread.start()

            self._sharing = True
            self._capture_thread = threading.Thread(target=self._capture_loop, daemon=True)
            self._capture_thread.start()
            _vlog("=== start_share SUCCESS ===")
            
            self._send_registration_packet()

            if self.on_share_state_changed:
                try:
                    self.on_share_state_changed(True)
                except Exception as e:
                    _vlog_exc(e)
            return True, ""

        except Exception as e:
            _vlog_exc(e)
            self._sharing = False
            return False, str(e)

    def stop_share(self):
        if not self._sharing:
            return
        self._sharing = False
        self._capture.stop()
        if self._capture_thread and self._capture_thread.is_alive():
            self._capture_thread.join(timeout=5.0)
        self._capture_thread = None
        nals = self._encoder.flush()
        for nal in nals:
            self._send_video_nal(nal, frame_type=0)
        self._encoder.close()

        if self.on_share_state_changed:
            try:
                self.on_share_state_changed(False)
            except Exception:
                pass

    @property
    def is_sharing(self):
        return self._sharing

    def close(self):
        self.stop_share()
        self.stop_receive()
        if self._udp_sock:
            try:
                self._udp_sock.close()
            except Exception:
                pass
            self._udp_sock = None
        with self._decoders_lock:
            for dec in self._decoders.values():
                dec.close()
            self._decoders.clear()

    def _capture_loop(self):
        last_time = time.time()
        frame_count = 0
        _vlog(f"[CAPTURE] loop STARTED, on_video_frame={self.on_video_frame is not None}")
        while self._sharing and self._capture.running:
            frame = self._capture.capture_frame()
            if frame is not None:
                # Local self-preview: MUST copy the frame to prevent data race
                if self.on_video_frame:
                    h, w = frame.shape[:2]
                    # Create a copy to ensure the frame data persists after this iteration
                    frame_copy = frame.copy()
                    try:
                        _vlog(f"[SELFVIEW] calling on_video_frame for user_id={self._user_id}, frame_shape={frame_copy.shape}")
                        self.on_video_frame(self._user_id, frame_copy, w, h)
                        _vlog(f"[SELFVIEW] on_video_frame returned OK")
                    except Exception as e:
                        _vlog_exc(e)
                        _vlog(f"[SELFVIEW] on_video_frame RAISED EXCEPTION")

                nals = self._encoder.encode(frame)
                _vlog(f"[CAPTURE] frame#{frame_count}: size={frame.shape[1]}x{frame.shape[0]}, nals={len(nals)}")
                frame_count += 1
                for i, nal in enumerate(nals):
                    is_keyframe = (i == 0 and len(nals) > 0)
                    _vlog(f"[CAPTURE]   nal[{i}]: size={len(nal)}, keyframe={is_keyframe}")
                    self._send_video_nal(nal, frame_type=0 if is_keyframe else 1)

            elapsed = time.time() - last_time
            sleep_time = (1.0 / self._capture.fps) - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)
            last_time = time.time()

    def _send_video_nal(self, nal_data, frame_type=1):
        if not self._udp_sock or not self._server_udp_addr:
            _vlog("[SEND] SKIP: no socket or no server addr")
            return
        if not self._crypto._key or self._crypto._key == bytes(32):
            _vlog("[SEND] SKIP: no encryption key")
            return

        header = video_pb2.VideoPacketHeader()
        header.sequence_number = self._sequence
        header.sender_id = self._user_id
        header.channel_id = self._channel_id
        header.timestamp = int(time.time() * 1000) & 0xFFFFFFFF
        header.frame_type = frame_type
        header.width = self._encoder.width
        header.height = self._encoder.height
        header.fps = self._capture.fps

        if len(nal_data) <= VIDEO_NAL_MAX_FRAGMENT:
            header.fragment_index = 0
            header.fragment_total = 1
            _vlog(f"[SEND] seq={self._sequence}, nal_size={len(nal_data)}, addr={self._server_udp_addr}")
            self._send_packet(header, nal_data)
        else:
            total = (len(nal_data) + VIDEO_NAL_MAX_FRAGMENT - 1) // VIDEO_NAL_MAX_FRAGMENT
            header.fragment_total = total
            _vlog(f"[SEND] seq={self._sequence}, nal_size={len(nal_data)}, frags={total}, addr={self._server_udp_addr}")
            for i in range(total):
                start = i * VIDEO_NAL_MAX_FRAGMENT
                end = min(start + VIDEO_NAL_MAX_FRAGMENT, len(nal_data))
                header.fragment_index = i
                self._send_packet(header, nal_data[start:end])

        self._sequence += 1

    def _send_packet(self, header, payload):
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
        _vlog(f"[RECV] _recv_loop STARTED, socket={self._udp_sock}, running={self._running}")
        _vlog(f"[RECV] server_addr={self._server_udp_addr}, user_id={self._user_id}, channel_id={self._channel_id}")
        _vlog(f"[RECV] crypto_key_set={self._crypto._key is not None and self._crypto._key != bytes(32)}")
        pkt_count = 0
        error_count = 0
        self_seen_count = 0
        zero_sender_count = 0
        decrypt_fail_count = 0
        short_payload_count = 0
        sender_stats = {}
        last_stats_time = time.time()
        
        while self._running:
            if not self._udp_sock:
                time.sleep(0.1)
                continue
            try:
                data, addr = self._udp_sock.recvfrom(VIDEO_UDP_MAX_PACKET_SIZE)
                pkt_count += 1
                
                # 每50个包输出一次统计摘要
                if pkt_count % 50 == 0:
                    elapsed = time.time() - last_stats_time
                    rate = pkt_count / max(elapsed, 0.001)
                    _vlog(f"[RECV] === STATS: total={pkt_count}, rate={rate:.1f}/s, errors={error_count}, "
                          f"self_skipped={self_seen_count}, zero_sender={zero_sender_count}, "
                          f"decrypt_fail={decrypt_fail_count}, short_payload={short_payload_count} ===")
                    _vlog(f"[RECV] === SENDERS: {sender_stats} ===")
                    last_stats_time = time.time()
                
                if data:
                    self._handle_received_packet(data, addr)
            except socket.timeout:
                continue
            except OSError as e:
                _vlog(f"[RECV] OSError: {e}, exiting loop")
                break
            except Exception as e:
                error_count += 1
                if error_count <= 5:
                    _vlog_exc(e)
                elif error_count == 10:
                    _vlog("[RECV] Too many errors, suppressing further exception logs")
                time.sleep(0.01)

        _vlog(f"[RECV] _recv_loop EXITED: total_packets={pkt_count}, errors={error_count}")
        _vlog(f"[RECV] Final stats: self_skipped={self_seen_count}, zero_sender={zero_sender_count}, "
              f"decrypt_fail={decrypt_fail_count}, short_payload={short_payload_count}")
        self._reassembler.cleanup_stale()

    def _handle_received_packet(self, data, addr=None):
        try:
            if len(data) < 2:
                return

            header_size = struct.unpack_from("<H", data, 0)[0]
            if header_size == 0 or 2 + header_size > len(data):
                _vlog(f"[RECV] BAD PACKET: total_size={len(data)}, header_size_field={header_size}")
                return

            header = video_pb2.VideoPacketHeader()
            header.ParseFromString(data[2 : 2 + header_size])

            sender_id = header.sender_id
            if sender_id == 0:
                _vlog(f"[RECV] WARNING: packet from {addr} has sender_id=0, seq={header.sequence_number}, "
                      f"channel={header.channel_id}, dropping (server should have filled this)")
                return

            if sender_id == self._user_id:
                return

            payload = data[2 + header_size :]
            if len(payload) < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE:
                _vlog(f"[RECV] SKIP: payload too short ({len(payload)} bytes), expected >= {XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE}")
                return

            header_aad = data[2 : 2 + header_size]
            plaintext = self._crypto.decrypt_simple(payload, header_aad)
            if plaintext is None:
                _vlog(f"[RECV] DECRYPT FAILED: sender={sender_id}, seq={header.sequence_number}, "
                      f"payload_len={len(payload)}, aad_len={len(header_aad)}")
                return

            _vlog(f"[RECV] OK: sender={sender_id}, seq={header.sequence_number}, "
                  f"frag={header.fragment_index}/{header.fragment_total}, nal_size={len(plaintext)}, "
                  f"from_addr={addr}, frame_type={header.frame_type}, "
                  f"resolution={header.width}x{header.height}")

            seq = header.sequence_number
            frag_idx = header.fragment_index
            frag_total = header.fragment_total

            if frag_total <= 1:
                self._process_nal(sender_id, plaintext, header)
            else:
                nal = self._reassembler.add_fragment(
                    sender_id, seq, frag_idx, frag_total, plaintext
                )
                if nal is not None:
                    _vlog(f"[RECV] reassembled NAL for sender={sender_id}, size={len(nal)}")
                    self._process_nal(sender_id, nal, header)
        except Exception as e:
            _vlog_exc(e)

    def _process_nal(self, sender_id, nal_data, header):
        if header.width <= 0 or header.height <= 0:
            return
        with self._decoders_lock:
            if sender_id not in self._decoders:
                dec = VideoDecoder()
                if dec.init(header.width, header.height):
                    self._decoders[sender_id] = dec
                    _vlog(f"[DECODE] created decoder for sender={sender_id}, {header.width}x{header.height}")
                else:
                    _vlog(f"[DECODE] FAILED to create decoder for sender={sender_id}")
                    return
            else:
                decoder = self._decoders[sender_id]
                if decoder and (decoder._width != header.width or decoder._height != header.height):
                    _vlog(f"[DECODE] resolution changed for sender={sender_id}: {decoder._width}x{decoder._height} -> {header.width}x{header.height}, recreating decoder")
                    if decoder.init(header.width, header.height):
                        _vlog(f"[DECODE] decoder recreated for sender={sender_id}")
                    else:
                        _vlog(f"[DECODE] FAILED to recreate decoder for sender={sender_id}")
                        del self._decoders[sender_id]
                        return
            decoder = self._decoders.get(sender_id)
            if not decoder:
                return

        frame = decoder.decode(nal_data)
        if frame is not None and self.on_video_frame:
            _vlog(f"[DECODE] decoded frame from sender={sender_id}: {frame.shape}")
            try:
                self.on_video_frame(sender_id, frame, header.width, header.height)
            except Exception as e:
                _vlog_exc(e)
        elif frame is None:
            _vlog(f"[DECODE] decode returned None for sender={sender_id}")
