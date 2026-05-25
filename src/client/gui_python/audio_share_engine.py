import logging
import struct
import threading
import time

import numpy as np

log = logging.getLogger("audio_share")
if not log.handlers:
    _h = logging.StreamHandler()
    _h.setFormatter(logging.Formatter("[AUDIO_SHARE] %(message)s"))
    log.addHandler(_h)
log.setLevel(logging.DEBUG)

SAMPLE_RATE = 48000
CHANNELS = 2
FRAME_DURATION_MS = 20
FRAME_SIZE = int(SAMPLE_RATE * FRAME_DURATION_MS / 1000)
AUDIO_SHARE_CHANNEL_ID = 9001

SOURCE_APP = "app"
SOURCE_SYSTEM = "system"


class AudioShareEngine:
    def __init__(self):
        self._running = False
        self._sharing = False
        self._source_type = None
        self._capture_thread = None
        self._udp_sock = None
        self._server_addr = None
        self._crypto = None
        self._user_id = 0
        self._sequence = 0
        self._opus_encoder = None
        self._loopback_device = None
        self._share_start_time = 0
        self._level = 0.0

        self.on_share_state_changed = None
        self.on_audio_level = None

    def set_udp_socket(self, sock):
        self._udp_sock = sock

    def set_server_addr(self, addr):
        self._server_addr = addr

    def set_crypto(self, crypto):
        self._crypto = crypto

    def set_user_info(self, user_id, channel_id=0):
        self._user_id = user_id

    def _init_opus(self):
        try:
            import opuslib
            self._opus_encoder = opuslib.Encoder(
                fs=SAMPLE_RATE,
                channels=CHANNELS,
                application=opuslib.APPLICATION_AUDIO,
            )
            self._opus_encoder.set_bitrate(128000)
            self._opus_encoder.set_complexity(8)
        except Exception as e:
            log.error(f"opus init failed: {e}")

    def _cleanup_opus(self):
        self._opus_encoder = None

    def _find_loopback_device(self, source_type):
        import sounddevice as sd
        devices = sd.query_devices()

        if source_type == SOURCE_SYSTEM:
            for i, d in enumerate(devices):
                name = d.get("name", "").lower()
                if d.get("max_input_channels", 0) > 0:
                    if any(k in name for k in ["stereo mix", "what u hear", "wave out", "loopback", "mix", "stereomix"]):
                        log.info(f"System loopback device: [{i}] {d['name']}")
                        return i
            log.warning("No system loopback device found, trying VB-Audio")
            for i, d in enumerate(devices):
                name = d.get("name", "").lower()
                if d.get("max_input_channels", 0) > 0 and "cable" in name:
                    log.info(f"VB-Audio cable device: [{i}] {d['name']}")
                    return i
            return None

        elif source_type == SOURCE_APP:
            try:
                import subprocess
                result = subprocess.run(
                    ["python", "-c",
                     "try:\n"
                     " from screen_audio_capture import get_audio_devices\n"
                     " devices = get_audio_devices()\n"
                     " for d in devices:\n"
                     "  print(f'{d[\"id\"]}|{d[\"name\"]}')\n"
                     "except ImportError:\n"
                     " pass"],
                    capture_output=True, text=True, timeout=5
                )
                if result.stdout.strip():
                    first_line = result.stdout.strip().split("\n")[0]
                    parts = first_line.split("|", 1)
                    if len(parts) == 2:
                        log.info(f"App audio capture: {parts[1]}")
                        return int(parts[0])
            except Exception:
                pass

            for i, d in enumerate(devices):
                name = d.get("name", "").lower()
                if d.get("max_input_channels", 0) > 0 and "cable" in name:
                    log.info(f"App audio via cable: [{i}] {d['name']}")
                    return i
            return None

        return None

    def start_share(self, source_type):
        if self._sharing:
            return False, "Already sharing"

        self._source_type = source_type
        device_idx = self._find_loopback_device(source_type)

        if device_idx is None:
            return False, self.tr_no("No audio capture device found. Please enable Stereo Mix or install VB-Audio Virtual Cable.")

        self._init_opus()
        if not self._opus_encoder:
            return False, "Opus encoder init failed"

        self._loopback_device = device_idx
        self._sharing = True
        self._running = True
        self._sequence = 0
        self._share_start_time = time.time()

        self._capture_thread = threading.Thread(target=self._capture_loop, daemon=True)
        self._capture_thread.start()

        log.info(f"Audio share started: source={source_type} device={device_idx}")
        if self.on_share_state_changed:
            self.on_share_state_changed(True)
        return True, None

    def stop_share(self):
        if not self._sharing:
            return

        self._running = False
        self._sharing = False

        if self._capture_thread and self._capture_thread.is_alive():
            self._capture_thread.join(timeout=3)

        self._cleanup_opus()
        self._loopback_device = None
        self._level = 0.0

        log.info("Audio share stopped")
        if self.on_share_state_changed:
            self.on_share_state_changed(False)

    def is_sharing(self):
        return self._sharing

    def get_share_duration(self):
        if not self._sharing:
            return 0
        return time.time() - self._share_start_time

    def get_audio_level(self):
        return self._level

    def tr_no(self, text):
        return text

    def _capture_loop(self):
        import sounddevice as sd

        try:
            stream = sd.InputStream(
                device=self._loopback_device,
                channels=CHANNELS,
                samplerate=SAMPLE_RATE,
                blocksize=FRAME_SIZE,
                dtype="float32",
                latency="low",
            )
            stream.start()
            log.info("Capture stream started")
        except Exception as e:
            log.error(f"Capture stream failed: {e}")
            self._sharing = False
            self._running = False
            if self.on_share_state_changed:
                self.on_share_state_changed(False)
            return

        while self._running:
            try:
                data, overflowed = stream.read(FRAME_SIZE)
                if data is not None and len(data) > 0:
                    self._level = float(np.sqrt(np.mean(np.square(data))))
                    self._encode_and_send(data)
                    if self.on_audio_level:
                        self.on_audio_level(self._level)
            except Exception as e:
                log.error(f"Capture read error: {e}")
                break

        try:
            stream.stop()
            stream.close()
        except Exception:
            pass

    def _encode_and_send(self, pcm_data):
        if not self._opus_encoder or not self._udp_sock or not self._server_addr:
            return

        try:
            pcm_bytes = (pcm_data * 32767).astype(np.int16).tobytes()
            encoded = self._opus_encoder.encode(pcm_bytes, FRAME_SIZE)
            if not encoded:
                return

            from proto import voice_pb2
            header = voice_pb2.VoicePacketHeader()
            header.sequence_number = self._sequence
            header.sender_id = self._user_id
            header.channel_id = AUDIO_SHARE_CHANNEL_ID
            header.timestamp = int(time.time() * 1000) & 0xFFFFFFFF
            header.last_frame = False
            header.tcp_tunnel = False
            self._sequence += 1

            header_bytes = header.SerializeToString()

            if self._crypto:
                encrypted = self._crypto.encrypt(encoded, header_bytes)
                if not encrypted:
                    return
            else:
                encrypted = encoded

            header_len_prefix = struct.pack('<H', len(header_bytes))
            packet = header_len_prefix + header_bytes + encrypted

            if len(packet) <= 1400:
                self._udp_sock.sendto(packet, self._server_addr)

        except Exception as e:
            log.error(f"Encode/send error: {e}")
