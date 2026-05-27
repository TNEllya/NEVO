import os
import socket
import struct
import threading
import time
import numpy as np
import sounddevice as sd

from voice_crypto import VoiceCrypto, XCHACHA_NONCE_SIZE, POLY1305_TAG_SIZE
from proto import voice_pb2

SAMPLE_RATE = 48000
CHANNELS = 1
FRAME_DURATION_MS = 20
FRAME_SIZE = int(SAMPLE_RATE * FRAME_DURATION_MS / 1000)
UDP_MAX_PACKET_SIZE = 1400

import sys as _sys
if getattr(_sys, 'frozen', False):
    _WF_LOG = os.path.join(os.path.dirname(_sys.executable), "voice_waveform_debug.log")
else:
    _WF_LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "voice_waveform_debug.log")


def _log_wf(msg: str):
    from datetime import datetime
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    line = f"[{ts}] {msg}"
    try:
        with open(_WF_LOG, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass


class VoiceEngine:
    def __init__(self):
        self._crypto = VoiceCrypto()
        self._udp_sock = None
        self._udp_recv_thread = None
        self._running = False
        self._lock = threading.Lock()

        self._user_id = 0
        self._channel_id = 0
        self._server_udp_addr = None

        self._sequence = 0
        self._muted = False
        self._deafened = False

        self._input_stream = None
        self._output_stream = None
        self._opus_encoder = None
        self._opus_decoders = {}
        self._decoders_lock = threading.Lock()

        self._playback_buffer = {}
        self._playback_lock = threading.Lock()

        self._audio_levels = {}
        self._audio_levels_lock = threading.Lock()
        self._audio_level_staleness_ms = 200

        self._user_volumes = {}
        self._user_volumes_lock = threading.Lock()

        self._user_local_mutes = set()
        self._user_mutes_lock = threading.Lock()

        self.on_voice_received = None

    @staticmethod
    def _apply_opus_option(obj, attr_name, value, method_name=None):
        try:
            if hasattr(obj, attr_name):
                setattr(obj, attr_name, value)
                return True
        except Exception:
            pass
        if method_name:
            method = getattr(obj, method_name, None)
            if callable(method):
                try:
                    method(value)
                    return True
                except Exception:
                    pass
        return False

    def _configure_decoder(self, decoder):
        self._apply_opus_option(decoder, "gain", 0, "set_gain")
        return decoder

    def set_server_udp(self, host, port):
        self._server_udp_addr = (host, port)
        _log_wf(f"[VOICE_ENGINE] set_server_udp: {host}:{port}")

    def get_user_audio_levels(self):
        now = time.time()
        threshold = self._audio_level_staleness_ms / 1000.0
        result = {}
        with self._audio_levels_lock:
            for uid, (rms, ts) in self._audio_levels.items():
                stale_ms = (now - ts) * 1000
                if uid == self._user_id or (now - ts) < threshold:
                    result[uid] = rms
        if result:
            _log_wf(f"[VOICE_ENGINE] get_user_audio_levels: {list(result.keys())}")
        return result

    def set_user_info(self, user_id, channel_id):
        self._user_id = user_id
        self._channel_id = channel_id
        # 如果已连接并且有 channel_id，立即发送注册包
        if self._running and self._udp_sock and self._server_udp_addr and channel_id > 0:
            self._send_registration_packet()

    def set_session_key(self, key):
        if isinstance(key, (bytes, bytearray)):
            self._crypto.set_session_key(key)

    def _send_registration_packet(self):
        """向服务器发送 UDP 注册包，让服务器知道我们的 UDP 端点"""
        if not self._udp_sock or not self._server_udp_addr:
            return
        try:
            # 发送一个最小的语音包作为注册
            header = voice_pb2.VoicePacketHeader()
            header.sequence_number = self._sequence
            header.sender_id = self._user_id
            header.channel_id = self._channel_id
            header.timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            header.last_frame = True
            header.tcp_tunnel = False
            self._sequence += 1

            header_bytes = header.SerializeToString()

            # 加密空负载
            encrypted = self._crypto.encrypt(b"", header_bytes)
            if not encrypted:
                return

            # 添加 2 字节长度前缀
            header_len_prefix = struct.pack('<H', len(header_bytes))
            packet = header_len_prefix + header_bytes + encrypted

            self._udp_sock.sendto(packet, self._server_udp_addr)
            _log_wf(f"[VOICE_ENGINE] SENT registration packet to {self._server_udp_addr} uid={self._user_id} channel={self._channel_id}")
        except Exception as e:
            _log_wf(f"[VOICE_ENGINE] _send_registration_packet EXCEPTION: {e}")

    @staticmethod
    def _create_dualstack_udp(rcvbuf=256 * 1024):
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
            return
        self._udp_sock = self._create_dualstack_udp(256 * 1024)

    @property
    def local_udp_port(self):
        if self._udp_sock:
            try:
                return self._udp_sock.getsockname()[1]
            except Exception:
                pass
        return 0

    def start(self):
        with self._lock:
            if self._running:
                return
            self._running = True

        try:
            self._init_opus()
            self._create_udp_socket()
            _log_wf(f"[VOICE_ENGINE] start: udp_port={self.local_udp_port} server={self._server_udp_addr} user_id={self._user_id} channel_id={self._channel_id}")
            self._start_receive_loop()
            self._start_capture()
            self._start_playback()
            if self._channel_id > 0:
                self._send_registration_packet()
        except Exception:
            self.stop()
            raise

    def stop(self):
        with self._lock:
            if not self._running:
                return
            self._running = False

        self._stop_capture()
        self._stop_playback()
        self._stop_receive_loop()
        self._close_udp_socket()
        self._cleanup_opus()

    def set_muted(self, muted):
        self._muted = muted

    def set_deafened(self, deafened):
        self._deafened = deafened

    def set_user_volume(self, user_id, volume):
        with self._user_volumes_lock:
            self._user_volumes[user_id] = max(0.0, min(1.0, volume))

    def get_user_volume(self, user_id):
        with self._user_volumes_lock:
            return self._user_volumes.get(user_id, 1.0)

    def set_user_local_mute(self, user_id, muted):
        with self._user_mutes_lock:
            if muted:
                self._user_local_mutes.add(user_id)
            else:
                self._user_local_mutes.discard(user_id)

    def is_user_local_muted(self, user_id):
        with self._user_mutes_lock:
            return user_id in self._user_local_mutes

    def get_all_user_settings(self):
        with self._user_volumes_lock:
            volumes = dict(self._user_volumes)
        with self._user_mutes_lock:
            mutes = list(self._user_local_mutes)
        return {"volumes": volumes, "mutes": mutes}

    def restore_user_settings(self, volumes: dict, mutes: list):
        with self._user_volumes_lock:
            for uid, vol in (volumes or {}).items():
                self._user_volumes[int(uid)] = max(0.0, min(1.0, float(vol)))
        with self._user_mutes_lock:
            self._user_local_mutes.clear()
            for uid in (mutes or []):
                self._user_local_mutes.add(int(uid))

    def add_remote_user(self, user_id):
        with self._decoders_lock:
            if user_id not in self._opus_decoders:
                try:
                    import opuslib
                    decoder = opuslib.Decoder(
                        fs=SAMPLE_RATE,
                        channels=CHANNELS,
                    )
                    self._opus_decoders[user_id] = self._configure_decoder(decoder)
                except Exception:
                    pass
        with self._playback_lock:
            if user_id not in self._playback_buffer:
                self._playback_buffer[user_id] = []

    def remove_remote_user(self, user_id):
        with self._decoders_lock:
            self._opus_decoders.pop(user_id, None)
        with self._playback_lock:
            self._playback_buffer.pop(user_id, None)
        with self._audio_levels_lock:
            self._audio_levels.pop(user_id, None)

    def send_voice_data(self, pcm_data):
        if not self._running or self._muted:
            return
        if not self._udp_sock or not self._server_udp_addr:
            _log_wf(f"[VOICE_ENGINE] send_voice_data SKIP: no socket={self._udp_sock} server={self._server_udp_addr}")
            return
        if not self._opus_encoder:
            _log_wf(f"[VOICE_ENGINE] send_voice_data SKIP: no opus encoder")
            return

        try:
            pcm_bytes = (pcm_data * 32767).astype(np.int16).tobytes()
            encoded = self._opus_encoder.encode(pcm_bytes, FRAME_SIZE)
            if not encoded:
                _log_wf(f"[VOICE_ENGINE] send_voice_data: opus encode returned empty")
                return

            header = voice_pb2.VoicePacketHeader()
            header.sequence_number = self._sequence
            header.sender_id = self._user_id
            header.channel_id = self._channel_id
            header.timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            header.last_frame = False
            header.tcp_tunnel = False
            self._sequence += 1

            header_bytes = header.SerializeToString()

            encrypted = self._crypto.encrypt(encoded, header_bytes)
            if not encrypted or len(encrypted) == 0:
                _log_wf(f"[VOICE_ENGINE] send_voice_data: encrypt failed/empty")
                return

            header_len_prefix = struct.pack('<H', len(header_bytes))
            packet = header_len_prefix + header_bytes + encrypted
            if len(packet) > UDP_MAX_PACKET_SIZE:
                _log_wf(f"[VOICE_ENGINE] send_voice_data: packet too large {len(packet)}")
                return

            self._udp_sock.sendto(packet, self._server_udp_addr)

            if self._sequence <= 6 or self._sequence % 100 == 0:
                _log_wf(f"[VOICE_ENGINE] SEND voice seq={self._sequence} size={len(packet)} to={self._server_udp_addr} channel={self._channel_id} uid={self._user_id}")
        except Exception as e:
            _log_wf(f"[VOICE_ENGINE] SEND voice EXCEPTION: {e}")

    def _handle_received_packet(self, data, addr):
        try:
            if len(data) < 2:
                _log_wf(f"[VOICE_ENGINE] _handle_received_packet: DROP len<2 from={addr}")
                return

            header_size = struct.unpack_from('<H', data, 0)[0]
            if header_size == 0 or 2 + header_size > len(data):
                _log_wf(f"[VOICE_ENGINE] _handle_received_packet: DROP bad header_size={header_size} data_len={len(data)}")
                return

            header = voice_pb2.VoicePacketHeader()
            header.ParseFromString(data[2:2 + header_size])

            sender_id = header.sender_id
            if sender_id == 0 or sender_id == self._user_id:
                _log_wf(f"[VOICE_ENGINE] _handle_received_packet: DROP sender_id={sender_id} my_uid={self._user_id}")
                return

            payload = data[2 + header_size:]
            if len(payload) < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE:
                _log_wf(f"[VOICE_ENGINE] _handle_received_packet: DROP payload too short len={len(payload)}")
                return

            header_aad = data[2:2 + header_size]
            plaintext = self._crypto.decrypt(payload, header_aad=header_aad)

            if plaintext is None:
                _log_wf(f"[VOICE_ENGINE] decrypt FAILED for sender={sender_id} my_uid={self._user_id} pkt_size={len(data)}")
                return

            _log_wf(f"[VOICE_ENGINE] decrypt OK sender={sender_id} my_uid={self._user_id} pt_len={len(plaintext)}")
            self._decode_and_play(sender_id, plaintext)
        except Exception as e:
            _log_wf(f"[VOICE_ENGINE] _handle_received_packet EXCEPTION: {e}")

    def _try_decrypt_raw(self, data):
        if len(data) < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE:
            return
        plaintext = self._crypto.decrypt(data)
        if plaintext is not None and len(plaintext) > 0:
            self._decode_and_play(0, plaintext)

    def _decode_and_play(self, sender_id, opus_data):
        if self._deafened:
            return

        with self._user_mutes_lock:
            if sender_id in self._user_local_mutes:
                return

        with self._decoders_lock:
            decoder = self._opus_decoders.get(sender_id)
            if decoder is None:
                try:
                    import opuslib
                    decoder = opuslib.Decoder(fs=SAMPLE_RATE, channels=CHANNELS)
                    self._opus_decoders[sender_id] = self._configure_decoder(decoder)
                except Exception:
                    _log_wf(f"[VOICE_ENGINE] _decode_and_play: opus decoder create FAILED for sender={sender_id}")
                    return

        try:
            pcm_bytes = decoder.decode(opus_data, FRAME_SIZE)
            if not pcm_bytes:
                return
            pcm_data = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32) / 32767.0

            with self._audio_levels_lock:
                try:
                    rms = float(np.sqrt(np.mean(np.square(pcm_data))))
                    self._audio_levels[sender_id] = (rms, time.time())
                    _log_wf(f"[VOICE_ENGINE] audio_level SET sender={sender_id} rms={rms:.6f}")
                except Exception as e:
                    self._audio_levels[sender_id] = (0.0, time.time())
                    _log_wf(f"[VOICE_ENGINE] audio_level SET ERROR sender={sender_id}: {e}")

            with self._playback_lock:
                if sender_id not in self._playback_buffer:
                    self._playback_buffer[sender_id] = []
                self._playback_buffer[sender_id].append(pcm_data)

            if self.on_voice_received:
                try:
                    self.on_voice_received(sender_id)
                except Exception:
                    pass
        except Exception:
            pass

    def _init_opus(self):
        try:
            import opuslib
            _log_wf(f"[VOICE_ENGINE] _init_opus: opuslib imported OK")
            self._opus_encoder = opuslib.Encoder(
                fs=SAMPLE_RATE,
                channels=CHANNELS,
                application=opuslib.APPLICATION_VOIP,
            )
            self._apply_opus_option(self._opus_encoder, "bitrate", 32000, "set_bitrate")
            self._apply_opus_option(self._opus_encoder, "complexity", 5, "set_complexity")
            self._apply_opus_option(self._opus_encoder, "packet_loss_perc", 10, "set_packet_loss_perc")
            self._apply_opus_option(self._opus_encoder, "inband_fec", True, "set_inband_fec")
            self._apply_opus_option(self._opus_encoder, "dtx", True, "enable_dtx")
            _log_wf(f"[VOICE_ENGINE] _init_opus: encoder created OK")
        except ImportError as e:
            _log_wf(f"[VOICE_ENGINE] _init_opus: ImportError: {e}")
            self._opus_encoder = None
        except Exception as e:
            _log_wf(f"[VOICE_ENGINE] _init_opus: FAILED: {type(e).__name__}: {e}")
            self._opus_encoder = None

    def _cleanup_opus(self):
        self._opus_encoder = None
        with self._decoders_lock:
            self._opus_decoders.clear()
        with self._playback_lock:
            self._playback_buffer.clear()
        with self._audio_levels_lock:
            self._audio_levels.clear()
        with self._user_volumes_lock:
            self._user_volumes.clear()
        with self._user_mutes_lock:
            self._user_local_mutes.clear()

    def _create_udp_socket(self):
        if self._udp_sock is not None:
            return
        self._udp_sock = self._create_dualstack_udp(256 * 1024)

    def _close_udp_socket(self):
        if self._udp_sock:
            try:
                self._udp_sock.close()
            except Exception:
                pass
            self._udp_sock = None

    def _start_receive_loop(self):
        if not self._udp_sock:
            return
        self._udp_recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._udp_recv_thread.start()

    def _stop_receive_loop(self):
        if self._udp_recv_thread and self._udp_recv_thread.is_alive():
            self._udp_recv_thread.join(timeout=3.0)
        self._udp_recv_thread = None

    def _recv_loop(self):
        _pkt_count = 0
        _log_interval = 50
        _last_timeout_log = 0.0
        while self._running:
            try:
                data, addr = self._udp_sock.recvfrom(UDP_MAX_PACKET_SIZE)
                if data:
                    _pkt_count += 1
                    if _pkt_count <= 5 or _pkt_count % _log_interval == 0:
                        _log_wf(f"[VOICE_ENGINE] recv_loop: received pkt #{_pkt_count} from {addr} size={len(data)}")
                    _last_timeout_log = 0.0
                    self._handle_received_packet(data, addr)
            except socket.timeout:
                now = time.time()
                if now - _last_timeout_log > 5.0:
                    _log_wf(f"[VOICE_ENGINE] recv_loop: NO UDP data for 5+ seconds (socket={self._udp_sock}, server={self._server_udp_addr})")
                    _last_timeout_log = now
                continue
            except OSError:
                _log_wf(f"[VOICE_ENGINE] recv_loop: OSError, breaking")
                break
            except Exception as e:
                _log_wf(f"[VOICE_ENGINE] recv_loop: unexpected error: {e}")
                time.sleep(0.01)

    def _start_capture(self):
        if self._input_stream is not None:
            return
        try:
            device = sd.default.device[0]
            if device is None:
                return

            def callback(indata, frames, time_info, status):
                if self._running:
                    with self._audio_levels_lock:
                        try:
                            chunk = indata[:, 0]
                            self._audio_levels[self._user_id] = (
                                float(np.sqrt(np.mean(np.square(chunk)))),
                                time.time(),
                            )
                        except Exception:
                            pass
                    if not self._muted:
                        self.send_voice_data(indata[:, 0].copy())

            self._input_stream = sd.InputStream(
                device=device,
                channels=CHANNELS,
                samplerate=SAMPLE_RATE,
                blocksize=FRAME_SIZE,
                dtype="float32",
                latency="low",
                callback=callback,
            )
            self._input_stream.start()
        except Exception:
            self._input_stream = None

    def _stop_capture(self):
        if self._input_stream is not None:
            try:
                self._input_stream.stop()
                self._input_stream.close()
            except Exception:
                pass
            self._input_stream = None

    def _start_playback(self):
        if self._output_stream is not None:
            return
        try:
            device = sd.default.device[1]

            def callback(outdata, frames, time_info, status):
                outdata.fill(0)
                mixed = np.zeros(frames, dtype=np.float32)
                count = 0
                with self._playback_lock:
                    items = list(self._playback_buffer.items())
                for uid, buf in items:
                    if len(buf) > 0:
                        chunk = buf.pop(0)
                        with self._user_volumes_lock:
                            gain = self._user_volumes.get(uid, 1.0)
                        chunk = chunk * gain
                        min_len = min(len(chunk), frames)
                        mixed[:min_len] += chunk[:min_len]
                        count += 1
                        if len(chunk) > frames:
                            with self._playback_lock:
                                self._playback_buffer[uid].insert(0, chunk[frames:])
                if count > 0:
                    gain = 1.0 / max(count, 1)
                    outdata[:, 0] = mixed * gain

            self._output_stream = sd.OutputStream(
                device=device,
                channels=CHANNELS,
                samplerate=SAMPLE_RATE,
                blocksize=FRAME_SIZE,
                dtype="float32",
                latency="low",
                callback=callback,
            )
            self._output_stream.start()
        except Exception:
            self._output_stream = None

    def _stop_playback(self):
        if self._output_stream is not None:
            try:
                self._output_stream.stop()
                self._output_stream.close()
            except Exception:
                pass
            self._output_stream = None
