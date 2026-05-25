# Screen Sharing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add screen sharing to NEVO VoIP, allowing users to share their screen/window with channel members via server-relayed UDP + H.264.

**Architecture:** Independent video UDP channel parallel to the existing voice UDP channel. Client captures screen → H.264 encode → encrypt → fragment → UDP send → server VideoRelay forwards → client receives → reassemble → decrypt → decode → render. Passthrough relay (no server decrypt/re-encrypt).

**Tech Stack:** Python (mss, PyAV/ffmpeg, PyQt5), C++ (Boost.Asio, protobuf), H.264 (ultrafast/zerolatency)

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `proto/video.proto` | VideoPacketHeader protobuf definition |
| `src/client/gui_python/screen_capture.py` | Screen/window capture via mss |
| `src/client/gui_python/video_encoder.py` | H.264 encode/decode via PyAV |
| `src/client/gui_python/video_engine.py` | Video send/receive/fragment/reassemble engine |
| `src/client/gui_python/screen_share_dialog.py` | Source selection dialog UI |
| `src/client/gui_python/screen_audio_capture.py` | System/app audio capture |
| `src/server/include/nevo/server/VideoRelay.h` | VideoRelay header |
| `src/server/src/VideoRelay.cpp` | VideoRelay implementation |

### Modified Files

| File | Change |
|------|--------|
| `proto/control.proto` | Add screen share message types (60-62) and messages |
| `src/client/gui_python/views/connection_bar.py` | Add "Share Screen" button |
| `src/client/gui_python/main_window.py` | Wire up VideoEngine and screen share UI |
| `src/client/gui_python/nevo_client.py` | Handle screen share control messages |
| `src/client/gui_python/nevo_wire.py` | Add video UDP port to login wire protocol |
| `src/server/include/nevo/server/ServerCore.h` | Add VideoRelay, video UDP socket, video port |
| `src/server/src/ServerCore.cpp` | Add video UDP receive loop, VideoRelay integration |
| `src/server/src/ClientSession.cpp` | Handle screen share start/stop control messages |
| `src/core/src/protocol/PacketCodec.cpp` | Add video packet encode/decode |
| `CMakeLists.txt` | Add VideoRelay to build |

---

### Task 1: Add video.proto

**Files:**
- Create: `proto/video.proto`

- [ ] **Step 1: Create video.proto**

```protobuf
syntax = "proto3";
package nevo.video;

message VideoPacketHeader {
    uint32 sequence_number = 1;
    uint64 sender_id = 2;
    uint64 channel_id = 3;
    uint32 timestamp = 4;
    uint32 frame_type = 5;       // 0=KeyFrame, 1=DeltaFrame
    uint32 fragment_index = 6;   // Fragment index within NAL
    uint32 fragment_total = 7;   // Total fragments for this NAL
    uint32 width = 8;
    uint32 height = 9;
    uint32 fps = 10;
    bool tcp_tunnel = 11;
}
```

- [ ] **Step 2: Generate Python protobuf bindings**

Run: `cd C:\Users\yzd20\Desktop\NEVO\proto && protoc --python_out=../src/client/gui_python/proto --proto_path=. video.proto`

- [ ] **Step 3: Commit**

```bash
git add proto/video.proto src/client/gui_python/proto/video_pb2.py
git commit -m "feat: add video.proto for screen sharing packet headers"
```

---

### Task 2: Add screen share control messages to control.proto

**Files:**
- Modify: `proto/control.proto`

- [ ] **Step 1: Add message types and messages to control.proto**

Add to `MessageType` enum after `MSG_FILE_DELETE_RESPONSE = 50`:

```protobuf
    // Screen sharing
    MSG_SCREEN_SHARE_START = 60;
    MSG_SCREEN_SHARE_STOP = 61;
    MSG_SCREEN_SHARE_STATE = 62;
```

Add new message definitions before `ControlMessage`:

```protobuf
// ============================================================
// Screen sharing
// ============================================================

message ScreenShareStartRequest {
    uint32 channel_id = 1;
    uint32 source_type = 2;     // 0=screen, 1=window, 2=tab
    string source_name = 3;
    uint32 width = 4;
    uint32 height = 5;
    uint32 fps = 6;
    bool share_audio = 7;
    uint32 audio_source = 8;    // 0=none, 1=system, 2=application
    uint32 audio_app_pid = 9;
}

message ScreenShareStopRequest {
    uint32 channel_id = 1;
}

message ScreenShareState {
    uint64 user_id = 1;
    bool sharing = 2;
    uint32 source_type = 3;
    string source_name = 4;
    uint32 width = 5;
    uint32 height = 6;
    bool has_audio = 7;
}
```

Add to `ControlMessage.oneof payload`:

```protobuf
        ScreenShareStartRequest screen_share_start = 60;
        ScreenShareStopRequest screen_share_stop = 61;
        ScreenShareState screen_share_state = 62;
```

- [ ] **Step 2: Regenerate Python protobuf bindings**

Run: `cd C:\Users\yzd20\Desktop\NEVO\proto && protoc --python_out=../src/client/gui_python/proto --proto_path=. control.proto common.proto`

- [ ] **Step 3: Commit**

```bash
git add proto/control.proto src/client/gui_python/proto/control_pb2.py
git commit -m "feat: add screen share control messages to control.proto"
```

---

### Task 3: Create screen_capture.py

**Files:**
- Create: `src/client/gui_python/screen_capture.py`

- [ ] **Step 1: Write screen_capture.py**

```python
import threading
import time
import numpy as np

try:
    import mss
    HAS_MSS = True
except ImportError:
    HAS_MSS = False

try:
    import win32gui
    import win32ui
    import win32con
    HAS_WIN32 = True
except ImportError:
    HAS_WIN32 = False


SOURCE_SCREEN = 0
SOURCE_WINDOW = 1
SOURCE_TAB = 2


class ScreenSource:
    def __init__(self, source_type, index, name, width, height):
        self.source_type = source_type
        self.index = index
        self.name = name
        self.width = width
        self.height = height


class ScreenCapture:
    def __init__(self):
        self._source_type = SOURCE_SCREEN
        self._source_index = 0
        self._fps = 15
        self._running = False
        self._capture_rect = None
        self._window_hwnd = None

    @staticmethod
    def is_available():
        return HAS_MSS

    def enumerate_sources(self):
        sources = []
        if HAS_MSS:
            with mss.mss() as sct:
                for i, mon in enumerate(sct.monitors[1:], 0):
                    sources.append(ScreenSource(
                        source_type=SOURCE_SCREEN,
                        index=i,
                        name=f"Monitor {i + 1} ({mon['width']}x{mon['height']})",
                        width=mon['width'],
                        height=mon['height'],
                    ))
        if HAS_WIN32:
            seen = []
            def _enum_cb(hwnd, _):
                if not win32gui.IsWindowVisible(hwnd):
                    return
                title = win32gui.GetWindowText(hwnd)
                if not title or len(title) < 2:
                    return
                if title in seen:
                    return
                seen.append(title)
                rect = win32gui.GetWindowRect(hwnd)
                w = rect[2] - rect[0]
                h = rect[3] - rect[1]
                if w < 64 or h < 64:
                    return
                sources.append(ScreenSource(
                    source_type=SOURCE_WINDOW,
                    index=len(seen) - 1,
                    name=title,
                    width=w,
                    height=h,
                ))
            try:
                win32gui.EnumWindows(_enum_cb, None)
            except Exception:
                pass
        return sources

    def start(self, source_type, source_index, fps=15):
        self._source_type = source_type
        self._source_index = source_index
        self._fps = fps
        self._running = True
        if source_type == SOURCE_WINDOW and HAS_WIN32:
            seen = []
            def _find_cb(hwnd, _):
                if not win32gui.IsWindowVisible(hwnd):
                    return
                title = win32gui.GetWindowText(hwnd)
                if not title or len(title) < 2:
                    return
                if title in seen:
                    return
                seen.append(title)
                if len(seen) - 1 == source_index:
                    self._window_hwnd = hwnd
            try:
                win32gui.EnumWindows(_find_cb, None)
            except Exception:
                pass

    def stop(self):
        self._running = False
        self._window_hwnd = None

    def capture_frame(self):
        if not HAS_MSS:
            return None
        try:
            with mss.mss() as sct:
                if self._source_type == SOURCE_SCREEN:
                    monitors = sct.monitors
                    idx = self._source_index + 1
                    if idx >= len(monitors):
                        idx = 1
                    monitor = monitors[idx]
                elif self._source_type == SOURCE_WINDOW and self._window_hwnd:
                    rect = win32gui.GetWindowRect(self._window_hwnd)
                    monitor = {
                        "left": rect[0],
                        "top": rect[1],
                        "width": rect[2] - rect[0],
                        "height": rect[3] - rect[1],
                    }
                else:
                    monitor = sct.monitors[1]

                shot = sct.grab(monitor)
                frame = np.array(shot, dtype=np.uint8)
                return np.ascontiguousarray(frame[:, :, :3])
        except Exception:
            return None
        return None

    @property
    def fps(self):
        return self._fps

    @property
    def running(self):
        return self._running
```

- [ ] **Step 2: Commit**

```bash
git add src/client/gui_python/screen_capture.py
git commit -m "feat: add screen capture module with mss and win32 support"
```

---

### Task 4: Create video_encoder.py

**Files:**
- Create: `src/client/gui_python/video_encoder.py`

- [ ] **Step 1: Write video_encoder.py**

```python
import numpy as np

try:
    import av
    HAS_AV = True
except ImportError:
    HAS_AV = False


SCREEN_SHARE_BITRATE_720P = 1500000
SCREEN_SHARE_BITRATE_1080P = 3000000
SCREEN_SHARE_FPS = 15
SCREEN_SHARE_MAX_WIDTH = 1920
SCREEN_SHARE_MAX_HEIGHT = 1080


def _align_dimension(val, alignment=16):
    return (val // alignment) * alignment


class VideoEncoder:
    def __init__(self):
        self._container = None
        self._stream = None
        self._width = 0
        self._height = 0

    @staticmethod
    def is_available():
        return HAS_AV

    def init(self, width, height, fps=SCREEN_SHARE_FPS, bitrate=None):
        if not HAS_AV:
            return False
        try:
            width = _align_dimension(min(width, SCREEN_SHARE_MAX_WIDTH))
            height = _align_dimension(min(height, SCREEN_SHARE_MAX_HEIGHT))
            if width < 16 or height < 16:
                return False
            self._width = width
            self._height = height

            if bitrate is None:
                if height >= 1080:
                    bitrate = SCREEN_SHARE_BITRATE_1080P
                else:
                    bitrate = SCREEN_SHARE_BITRATE_720P

            self._container = av.open("/dev/null", "w", format="null")
            self._stream = self._container.add_stream("h264", rate=fps)
            self._stream.width = width
            self._stream.height = height
            self._stream.pix_fmt = "yuv420p"
            self._stream.bit_rate = bitrate
            self._stream.options = {
                "preset": "ultrafast",
                "tune": "zerolatency",
                "profile": "high",
            }
            self._stream.open()
            return True
        except Exception:
            self._container = None
            self._stream = None
            return False

    def encode(self, frame_bgr):
        if not self._stream:
            return []
        try:
            av_frame = av.VideoFrame.from_ndarray(frame_bgr, format="bgr24")
            av_frame.width = self._width
            av_frame.height = self._height
            packets = self._stream.encode(av_frame)
            result = []
            for pkt in packets:
                result.append(bytes(pkt))
            return result
        except Exception:
            return []

    def flush(self):
        if not self._stream:
            return []
        try:
            packets = self._stream.encode()
            result = []
            for pkt in packets:
                result.append(bytes(pkt))
            return result
        except Exception:
            return []

    def close(self):
        if self._container:
            try:
                self._container.close()
            except Exception:
                pass
            self._container = None
            self._stream = None

    @property
    def width(self):
        return self._width

    @property
    def height(self):
        return self._height


class VideoDecoder:
    def __init__(self):
        self._container = None
        self._stream = None
        self._width = 0
        self._height = 0

    def init(self, width=0, height=0):
        if not HAS_AV:
            return False
        try:
            self._container = av.open("/dev/null", "r", format="null")
            self._stream = self._container.add_stream("h264", rate=SCREEN_SHARE_FPS)
            self._stream.width = width if width else 1920
            self._stream.height = height if height else 1080
            self._stream.pix_fmt = "yuv420p"
            self._stream.open()
            self._width = width
            self._height = height
            return True
        except Exception:
            self._container = None
            self._stream = None
            return False

    def decode(self, nal_data):
        if not self._stream:
            return None
        try:
            packet = av.Packet(nal_data)
            frames = self._stream.decode(packet)
            for frame in frames:
                return frame.to_ndarray(format="bgr24")
        except Exception:
            pass
        return None

    def close(self):
        if self._container:
            try:
                self._container.close()
            except Exception:
                pass
            self._container = None
            self._stream = None
```

- [ ] **Step 2: Commit**

```bash
git add src/client/gui_python/video_encoder.py
git commit -m "feat: add H.264 video encoder/decoder with screen share optimizations"
```

---

### Task 5: Create video_engine.py

**Files:**
- Create: `src/client/gui_python/video_engine.py`

- [ ] **Step 1: Write video_engine.py**

```python
import socket
import struct
import threading
import time

from voice_crypto import VoiceCrypto, XCHACHA_NONCE_SIZE, POLY1305_TAG_SIZE
from screen_capture import ScreenCapture, SOURCE_SCREEN, SOURCE_WINDOW
from video_encoder import VideoEncoder, VideoDecoder, SCREEN_SHARE_FPS
from proto import video_pb2

VIDEO_UDP_MAX_PACKET_SIZE = 1400
VIDEO_NAL_MAX_FRAGMENT = 1200
VIDEO_REASSEMBLY_MAX_AGE = 5.0


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
        self._server_udp_addr = (host, port)

    def set_user_info(self, user_id, channel_id):
        self._user_id = user_id
        self._channel_id = channel_id

    def set_session_key(self, key):
        if isinstance(key, (bytes, bytearray)):
            self._crypto.set_session_key(key)

    def pre_create_udp_socket(self):
        if self._udp_sock is not None:
            return
        try:
            self._udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512 * 1024)
            self._udp_sock.settimeout(1.0)
            self._udp_sock.bind(("0.0.0.0", 0))
        except Exception:
            self._udp_sock = None

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
            return
        self._running = True
        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()

    def stop_receive(self):
        self._running = False
        if self._recv_thread and self._recv_thread.is_alive():
            self._recv_thread.join(timeout=3.0)
        self._recv_thread = None

    def start_share(self, source_type=SOURCE_SCREEN, source_index=0, fps=SCREEN_SHARE_FPS):
        if self._sharing:
            return False
        if not ScreenCapture.is_available():
            return False
        if not VideoEncoder.is_available():
            return False

        self._capture.start(source_type, source_index, fps)
        frame = self._capture.capture_frame()
        if frame is None:
            self._capture.stop()
            return False

        h, w = frame.shape[:2]
        if not self._encoder.init(w, h, fps):
            self._capture.stop()
            return False

        self._sharing = True
        self._capture_thread = threading.Thread(target=self._capture_loop, daemon=True)
        self._capture_thread.start()

        if self.on_share_state_changed:
            try:
                self.on_share_state_changed(True)
            except Exception:
                pass
        return True

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
        while self._sharing and self._capture.running:
            frame = self._capture.capture_frame()
            if frame is not None:
                nals = self._encoder.encode(frame)
                for i, nal in enumerate(nals):
                    is_keyframe = (i == 0 and len(nals) > 0)
                    self._send_video_nal(nal, frame_type=0 if is_keyframe else 1)

            elapsed = time.time() - last_time
            sleep_time = (1.0 / self._capture.fps) - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)
            last_time = time.time()

    def _send_video_nal(self, nal_data, frame_type=1):
        if not self._udp_sock or not self._server_udp_addr:
            return
        if not self._crypto._key or self._crypto._key == bytes(32):
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
        while self._running:
            if not self._udp_sock:
                time.sleep(0.1)
                continue
            try:
                data, addr = self._udp_sock.recvfrom(VIDEO_UDP_MAX_PACKET_SIZE)
                if data:
                    self._handle_received_packet(data)
            except socket.timeout:
                continue
            except OSError:
                break
            except Exception:
                time.sleep(0.01)

        self._reassembler.cleanup_stale()

    def _handle_received_packet(self, data):
        try:
            if len(data) < 2:
                return

            header_size = struct.unpack_from("<H", data, 0)[0]
            if header_size == 0 or 2 + header_size > len(data):
                return

            header = video_pb2.VideoPacketHeader()
            header.ParseFromString(data[2 : 2 + header_size])

            sender_id = header.sender_id
            if sender_id == 0 or sender_id == self._user_id:
                return

            payload = data[2 + header_size :]
            if len(payload) < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE:
                return

            header_aad = data[2 : 2 + header_size]
            plaintext = self._crypto.decrypt(payload, header_aad=header_aad)
            if plaintext is None:
                return

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
                    self._process_nal(sender_id, nal, header)
        except Exception:
            pass

    def _process_nal(self, sender_id, nal_data, header):
        with self._decoders_lock:
            if sender_id not in self._decoders:
                dec = VideoDecoder()
                if dec.init(header.width, header.height):
                    self._decoders[sender_id] = dec
                else:
                    return
            decoder = self._decoders.get(sender_id)
            if not decoder:
                return

        frame = decoder.decode(nal_data)
        if frame is not None and self.on_video_frame:
            try:
                self.on_video_frame(sender_id, frame, header.width, header.height)
            except Exception:
                pass
```

- [ ] **Step 2: Commit**

```bash
git add src/client/gui_python/video_engine.py
git commit -m "feat: add video engine with capture, encode, fragment, reassemble, decrypt"
```

---

### Task 6: Create screen_share_dialog.py

**Files:**
- Create: `src/client/gui_python/screen_share_dialog.py`

- [ ] **Step 1: Write screen_share_dialog.py**

```python
from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import QVBoxLayout, QHBoxLayout, QGridLayout, QLabel
from qfluentwidgets import (
    Dialog, PushButton, ComboBox, SpinBox, StrongBodyLabel,
    TabBar, TabItem, CardWidget, FluentIcon,
)

from screen_capture import ScreenCapture, SOURCE_SCREEN, SOURCE_WINDOW


AUDIO_NONE = 0
AUDIO_SYSTEM = 1
AUDIO_APPLICATION = 2


class ScreenShareDialog(Dialog):
    def __init__(self, parent=None):
        super().__init__(parent.tr("Share Screen"), "", parent)
        self.yesButton.setText(self.tr("Start Sharing"))
        self.cancelButton.setText(self.tr("Cancel"))

        self._sources = []
        self._selected_source = None
        self._audio_source = AUDIO_NONE

        layout = QVBoxLayout()
        layout.setSpacing(12)

        source_label = StrongBodyLabel(self.tr("Select Source"))
        layout.addWidget(source_label)

        self.source_combo = ComboBox()
        self.source_combo.setMinimumWidth(320)
        self.source_combo.currentIndexChanged.connect(self._on_source_changed)
        layout.addWidget(self.source_combo)

        audio_label = StrongBodyLabel(self.tr("Audio Source"))
        layout.addWidget(audio_label)

        self.audio_combo = ComboBox()
        self.audio_combo.addItems([
            self.tr("No Audio"),
            self.tr("System Audio"),
            self.tr("Application Audio (Windows only)"),
        ])
        self.audio_combo.currentIndexChanged.connect(self._on_audio_changed)
        layout.addWidget(self.audio_combo)

        fps_label = StrongBodyLabel(self.tr("Frame Rate"))
        layout.addWidget(fps_label)

        self.fps_spin = SpinBox()
        self.fps_spin.setRange(5, 30)
        self.fps_spin.setValue(15)
        layout.addWidget(self.fps_spin)

        self.contentLabel.hide()
        self.textLayout.insertLayout(self.textLayout.count(), layout)
        self.setFixedSize(420, 360)

        self._refresh_sources()

    def _refresh_sources(self):
        self._sources = ScreenCapture().enumerate_sources()
        self.source_combo.clear()
        for src in self._sources:
            type_prefix = "🖥" if src.source_type == SOURCE_SCREEN else "🪟"
            self.source_combo.addItem(f"{type_prefix} {src.name}")
        if self._sources:
            self._selected_source = self._sources[0]

    def _on_source_changed(self, index):
        if 0 <= index < len(self._sources):
            self._selected_source = self._sources[index]

    def _on_audio_changed(self, index):
        self._audio_source = index

    def get_config(self):
        if not self._selected_source:
            return None
        return {
            "source_type": self._selected_source.source_type,
            "source_index": self._selected_source.index,
            "source_name": self._selected_source.name,
            "width": self._selected_source.width,
            "height": self._selected_source.height,
            "fps": self.fps_spin.value(),
            "audio_source": self._audio_source,
        }
```

- [ ] **Step 2: Commit**

```bash
git add src/client/gui_python/screen_share_dialog.py
git commit -m "feat: add screen share source selection dialog"
```

---

### Task 7: Create screen_audio_capture.py

**Files:**
- Create: `src/client/gui_python/screen_audio_capture.py`

- [ ] **Step 1: Write screen_audio_capture.py**

```python
import threading
import numpy as np

try:
    import sounddevice as sd
    HAS_SD = True
except ImportError:
    HAS_SD = False

AUDIO_NONE = 0
AUDIO_SYSTEM = 1
AUDIO_APPLICATION = 2

SAMPLE_RATE = 48000
CHANNELS = 2
FRAME_SIZE = 960


class ScreenAudioCapture:
    def __init__(self):
        self._stream = None
        self._running = False
        self._audio_source = AUDIO_NONE
        self._on_audio_data = None

    def start(self, audio_source, on_audio_data=None):
        if not HAS_SD or audio_source == AUDIO_NONE:
            return False
        self._audio_source = audio_source
        self._on_audio_data = on_audio_data
        self._running = True

        try:
            device = self._find_loopback_device()
            if device is None:
                self._running = False
                return False

            def callback(indata, frames, time_info, status):
                if self._running and self._on_audio_data:
                    self._on_audio_data(indata.copy())

            self._stream = sd.InputStream(
                device=device,
                channels=CHANNELS,
                samplerate=SAMPLE_RATE,
                blocksize=FRAME_SIZE,
                dtype="float32",
                latency="low",
                callback=callback,
            )
            self._stream.start()
            return True
        except Exception:
            self._running = False
            return False

    def stop(self):
        self._running = False
        if self._stream is not None:
            try:
                self._stream.stop()
                self._stream.close()
            except Exception:
                pass
            self._stream = None

    def _find_loopback_device(self):
        if not HAS_SD:
            return None
        devices = sd.query_devices()
        for i, dev in enumerate(devices):
            name = dev.get("name", "").lower()
            if "loopback" in name and dev.get("max_input_channels", 0) > 0:
                return i
        for i, dev in enumerate(devices):
            name = dev.get("name", "").lower()
            if "stereo mix" in name and dev.get("max_input_channels", 0) > 0:
                return i
        return None

    @property
    def running(self):
        return self._running
```

- [ ] **Step 2: Commit**

```bash
git add src/client/gui_python/screen_audio_capture.py
git commit -m "feat: add screen audio capture with WASAPI loopback support"
```

---

### Task 8: Add VideoRelay to server (C++ header)

**Files:**
- Create: `src/server/include/nevo/server/VideoRelay.h`

- [ ] **Step 1: Write VideoRelay.h**

```cpp
#pragma once

#include "nevo/core/common/Types.h"
#include "nevo/network/UdpSocket.h"

#include <boost/asio.hpp>

#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>

namespace nevo {

class ChannelManager;

struct VideoClientMapping {
    UserId user_id;
    ChannelId channel_id;
    boost::asio::ip::udp::endpoint endpoint;
    std::shared_ptr<class VoiceCrypto> crypto;
};

class VideoRelay {
public:
    VideoRelay();
    ~VideoRelay();

    void setChannelManager(std::shared_ptr<ChannelManager> mgr);
    void setUdpSocket(std::shared_ptr<UdpSocket> socket);

    void handleVideoPacket(const uint8_t* data, uint32_t size,
                           const boost::asio::ip::udp::endpoint& sender);

    void addClientMapping(UserId user_id,
                          const boost::asio::ip::udp::endpoint& ep,
                          ChannelId channel_id);
    void removeClientMapping(UserId user_id);
    void updateClientChannel(UserId user_id, ChannelId channel_id);

    uint64_t packetsRelayed() const { return packets_relayed_.load(); }
    uint64_t packetsDropped() const { return packets_dropped_.load(); }

private:
    std::optional<UserId> findUserByEndpoint(
        const boost::asio::ip::udp::endpoint& ep) const;

    std::mutex mutex_;
    std::shared_ptr<ChannelManager> channel_mgr_;
    std::shared_ptr<UdpSocket> udp_socket_;

    std::unordered_map<UserId, VideoClientMapping> client_map_;
    std::unordered_map<std::string, UserId> endpoint_to_user_;

    std::atomic<uint64_t> packets_relayed_{0};
    std::atomic<uint64_t> packets_dropped_{0};
};

} // namespace nevo
```

- [ ] **Step 2: Commit**

```bash
git add src/server/include/nevo/server/VideoRelay.h
git commit -m "feat: add VideoRelay header for server-side video forwarding"
```

---

### Task 9: Add VideoRelay implementation (C++)

**Files:**
- Create: `src/server/src/VideoRelay.cpp`

- [ ] **Step 1: Write VideoRelay.cpp**

```cpp
#include "nevo/server/VideoRelay.h"
#include "nevo/core/model/ChannelManager.h"
#include "nevo/core/protocol/PacketCodec.h"

#include "video.pb.h"

#include <NEVO_LOG.h>

namespace nevo {

VideoRelay::VideoRelay() = default;

VideoRelay::~VideoRelay() = default;

void VideoRelay::setChannelManager(std::shared_ptr<ChannelManager> mgr) {
    channel_mgr_ = std::move(mgr);
}

void VideoRelay::setUdpSocket(std::shared_ptr<UdpSocket> socket) {
    udp_socket_ = std::move(socket);
}

void VideoRelay::handleVideoPacket(const uint8_t* data, uint32_t size,
                                    const boost::asio::ip::udp::endpoint& sender) {
    if (!data || size < 2) {
        ++packets_dropped_;
        return;
    }

    auto sender_id_opt = findUserByEndpoint(sender);
    if (!sender_id_opt) {
        ++packets_dropped_;
        return;
    }

    UserId sender_id = *sender_id_opt;

    uint32_t header_size = 0;
    auto header_opt = decodeVoicePacketHeader(data, size, header_size);
    if (!header_opt) {
        ++packets_dropped_;
        return;
    }

    ChannelId sender_channel = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = client_map_.find(sender_id);
        if (it != client_map_.end()) {
            sender_channel = it->second.channel_id;
        }
    }

    if (sender_channel == 0) {
        ++packets_dropped_;
        return;
    }

    std::vector<std::pair<UserId, boost::asio::ip::udp::endpoint>> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [uid, mapping] : client_map_) {
            if (uid != sender_id && mapping.channel_id == sender_channel) {
                targets.emplace_back(uid, mapping.endpoint);
            }
        }
    }

    if (targets.empty()) {
        return;
    }

    if (udp_socket_) {
        for (const auto& [uid, ep] : targets) {
            udp_socket_->asyncSendTo(data, size, ep);
        }
    }

    packets_relayed_.fetch_add(targets.size());
}

void VideoRelay::addClientMapping(UserId user_id,
                                   const boost::asio::ip::udp::endpoint& ep,
                                   ChannelId channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    VideoClientMapping mapping;
    mapping.user_id = user_id;
    mapping.channel_id = channel_id;
    mapping.endpoint = ep;
    client_map_[user_id] = mapping;

    std::string ep_key = ep.address().to_string() + ":" + std::to_string(ep.port());
    endpoint_to_user_[ep_key] = user_id;
}

void VideoRelay::removeClientMapping(UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        std::string ep_key = it->second.endpoint.address().to_string() +
                             ":" + std::to_string(it->second.endpoint.port());
        endpoint_to_user_.erase(ep_key);
    }
    client_map_.erase(user_id);
}

void VideoRelay::updateClientChannel(UserId user_id, ChannelId channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        it->second.channel_id = channel_id;
    }
}

std::optional<UserId> VideoRelay::findUserByEndpoint(
    const boost::asio::ip::udp::endpoint& ep) const {
    std::string ep_key = ep.address().to_string() + ":" + std::to_string(ep.port());
    auto it = endpoint_to_user_.find(ep_key);
    if (it != endpoint_to_user_.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace nevo
```

- [ ] **Step 2: Commit**

```bash
git add src/server/src/VideoRelay.cpp
git commit -m "feat: add VideoRelay passthrough implementation"
```

---

### Task 10: Integrate VideoRelay into ServerCore

**Files:**
- Modify: `src/server/include/nevo/server/ServerCore.h`
- Modify: `src/server/src/ServerCore.cpp`

- [ ] **Step 1: Add VideoRelay to ServerCore.h**

Add forward declaration after `class AudioRelay;`:

```cpp
class VideoRelay;
```

Add member variables in the private section after `std::shared_ptr<AudioRelay> audio_relay_;`:

```cpp
    uint16_t video_udp_port_;
    std::shared_ptr<UdpSocket> video_udp_socket_;
    std::shared_ptr<VideoRelay> video_relay_;
```

Add public method declarations:

```cpp
    uint16_t videoUdpPort() const { return video_udp_port_; }

    void updateVideoRelayChannel(UserId user_id, ChannelId channel_id);

    void addVideoRelayMapping(UserId user_id,
                              const boost::asio::ip::udp::endpoint& ep,
                              ChannelId channel_id);
    void removeVideoRelayMapping(UserId user_id);
```

Add private coroutine declaration:

```cpp
    boost::asio::awaitable<void> receiveVideoUdpLoop();
```

- [ ] **Step 2: Update ServerCore constructor to accept video UDP port**

In `ServerCore.cpp`, update the constructor to initialize `video_udp_port_` (default: udp_port + 1):

```cpp
ServerCore::ServerCore(boost::asio::io_context& io_ctx,
                       uint16_t tcp_port,
                       uint16_t udp_port)
    : io_ctx_(io_ctx),
      tcp_port_(tcp_port),
      udp_port_(udp_port),
      video_udp_port_(udp_port + 1),
      tcp_acceptor_(io_ctx) {
    // ... existing code
}
```

- [ ] **Step 3: Initialize VideoRelay in ServerCore::initialize()**

After AudioRelay initialization, add:

```cpp
    video_relay_ = std::make_shared<VideoRelay>();
    video_relay_->setChannelManager(channel_mgr_);
```

- [ ] **Step 4: Start video UDP loop in ServerCore::start()**

After the voice UDP loop spawn, add:

```cpp
    video_udp_socket_ = std::make_shared<UdpSocket>(io_ctx_, video_udp_port_);
    video_relay_->setUdpSocket(video_udp_socket_);

    boost::asio::co_spawn(io_ctx_,
        [this]() -> boost::asio::awaitable<void> {
            try {
                co_await receiveVideoUdpLoop();
            } catch (const std::exception& e) {
                NEVO_LOG_ERROR("server", "Video UDP receive loop exception: {}", e.what());
            }
        },
        boost::asio::detached);
```

- [ ] **Step 5: Implement receiveVideoUdpLoop()**

```cpp
boost::asio::awaitable<void> ServerCore::receiveVideoUdpLoop() {
    NEVO_LOG_INFO("server", "Video UDP receive loop starting on port {}", video_udp_port_);

    video_udp_socket_->onPacket = [this](const uint8_t* data, uint32_t size,
                                          const boost::asio::ip::udp::endpoint& sender) {
        if (video_relay_) {
            video_relay_->handleVideoPacket(data, size, sender);
        }
    };

    co_await video_udp_socket_->asyncReceiveFrom();

    NEVO_LOG_INFO("server", "Video UDP receive loop exited");
}
```

- [ ] **Step 6: Add video relay mapping methods**

```cpp
void ServerCore::addVideoRelayMapping(UserId user_id,
                                       const boost::asio::ip::udp::endpoint& ep,
                                       ChannelId channel_id) {
    if (video_relay_) {
        video_relay_->addClientMapping(user_id, ep, channel_id);
    }
}

void ServerCore::removeVideoRelayMapping(UserId user_id) {
    if (video_relay_) {
        video_relay_->removeClientMapping(user_id);
    }
}

void ServerCore::updateVideoRelayChannel(UserId user_id, ChannelId channel_id) {
    if (video_relay_) {
        video_relay_->updateClientChannel(user_id, channel_id);
    }
}
```

- [ ] **Step 7: Wire up video relay in onClientConnected/onClientDisconnected**

In `onClientConnected()`, after `audio_relay_->addClientMapping(...)`, add:

```cpp
    if (session->isAuthenticated()) {
        // Video relay mapping will be added when we receive the first video UDP packet
        // or when the client sends its video UDP port
    }
```

In `onClientDisconnected()`, after `audio_relay_->removeClientMapping(...)`, add:

```cpp
    video_relay_->removeClientMapping(user_id);
```

- [ ] **Step 8: Add video UDP port to LoginResponse**

In `ClientSession.cpp` handleLogin, add to LoginResponse:

```cpp
login_resp->set_server_video_udp_port(server_core_->videoUdpPort());
```

And in `control.proto`, add to `LoginResponse`:

```protobuf
    uint32 server_video_udp_port = 9;    // 服务端视频 UDP 端口
```

- [ ] **Step 9: Commit**

```bash
git add src/server/include/nevo/server/ServerCore.h src/server/src/ServerCore.cpp src/server/src/ClientSession.cpp proto/control.proto
git commit -m "feat: integrate VideoRelay into ServerCore with separate video UDP port"
```

---

### Task 11: Update CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add VideoRelay.cpp to server sources**

Find the server source list and add:

```cmake
src/server/src/VideoRelay.cpp
```

- [ ] **Step 2: Add video.proto to protobuf generation**

Add `proto/video.proto` to the protobuf source list.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat: add VideoRelay and video.proto to CMake build"
```

---

### Task 12: Update client wire protocol for video UDP port

**Files:**
- Modify: `src/client/gui_python/nevo_wire.py`
- Modify: `src/client/gui_python/nevo_client.py`

- [ ] **Step 1: Add client_video_udp_port to login wire encoding in nevo_wire.py**

In the `encode_login_request` function, after `client_udp_port`, add encoding for `client_video_udp_port`:

```python
    if hasattr(msg, 'client_video_udp_port') and msg.client_video_udp_port:
        r.writeU16(msg.client_video_udp_port)
```

- [ ] **Step 2: Parse server_video_udp_port from LoginResponse in nevo_wire.py**

In the `decode_login_response` function, add:

```python
    video_port = 0
    if r.readU16(video_port_raw):
        video_port = video_port_raw
    resp.server_video_udp_port = video_port
```

- [ ] **Step 3: Update nevo_client.py to handle video UDP port**

In `connect()`, after creating voice_engine UDP socket, also pre-create video_engine UDP socket:

```python
    client_video_udp_port = 0
    client_video_port = 0
    if voice_engine is not None:
        voice_engine.pre_create_udp_socket()
        client_video_udp_port = voice_engine.local_udp_port
    if video_engine is not None:
        video_engine.pre_create_udp_socket()
        client_video_port = video_engine.local_udp_port
    login_msg = LoginRequest(
        username=username,
        auth_credential=password.encode("utf-8"),
        key_exchange_methods=["X25519"],
        client_public_key=b"\x00" * 32,
        client_udp_port=client_video_udp_port,
        client_video_udp_port=client_video_port,
    )
```

After receiving LoginResponse, set video server address:

```python
    if video_engine is not None and hasattr(resp, 'server_video_udp_port'):
        video_port = resp.server_video_udp_port
        if video_port > 0:
            video_engine.set_server_udp(host, video_port)
        video_engine.set_session_key(session_key)
        video_engine.set_user_info(user_id, 0)
        video_engine.start_receive()
```

- [ ] **Step 4: Commit**

```bash
git add src/client/gui_python/nevo_wire.py src/client/gui_python/nevo_client.py
git commit -m "feat: add video UDP port to client wire protocol and login flow"
```

---

### Task 13: Add Share Screen button to ConnectionBar

**Files:**
- Modify: `src/client/gui_python/views/connection_bar.py`

- [ ] **Step 1: Add share_screen_requested signal and button**

Add signal to ConnectionBar class:

```python
    share_screen_requested = pyqtSignal()
    stop_screen_share_requested = pyqtSignal()
```

Add button after `btn_deafen` in `__init__`:

```python
        self.btn_share_screen = PushButton(self.tr("Share"))
        self.btn_share_screen.setIcon(FluentIcon.VIDEO)
        self.btn_share_screen.setEnabled(False)
        self.btn_share_screen.setCheckable(True)
        self.btn_share_screen.setFixedSize(90, 32)
        self.btn_share_screen.setStyleSheet(
            "QPushButton { "
            "  background-color: rgba(255, 255, 255, 0.08); "
            "  color: #dbdee1; "
            "  border: none; "
            "  border-radius: 6px; "
            "} "
            "QPushButton:hover { "
            "  background-color: rgba(255, 255, 255, 0.12); "
            "} "
            "QPushButton:checked { "
            "  background-color: #c0392b; "
            "  color: #ffffff; "
            "} "
            "QPushButton:disabled { "
            "  background-color: rgba(255, 255, 255, 0.05); "
            "  color: #6d6f78; "
            "}"
        )
        self.btn_share_screen.toggled.connect(self._on_share_screen_toggled)
        layout.addWidget(self.btn_share_screen)
```

Add toggle handler:

```python
    def _on_share_screen_toggled(self, checked):
        if checked:
            self.share_screen_requested.emit()
        else:
            self.stop_screen_share_requested.emit()

    def set_sharing(self, sharing):
        self.btn_share_screen.blockSignals(True)
        self.btn_share_screen.setChecked(sharing)
        self.btn_share_screen.setText(
            self.tr("Stop") if sharing else self.tr("Share")
        )
        self.btn_share_screen.blockSignals(False)
```

Update `set_connected` to enable/disable the share button:

```python
        self.btn_share_screen.setEnabled(connected)
```

- [ ] **Step 2: Commit**

```bash
git add src/client/gui_python/views/connection_bar.py
git commit -m "feat: add Share Screen button to ConnectionBar"
```

---

### Task 14: Wire up VideoEngine in MainWindow

**Files:**
- Modify: `src/client/gui_python/main_window.py`

- [ ] **Step 1: Import VideoEngine and ScreenShareDialog**

Add imports at top:

```python
from video_engine import VideoEngine
from screen_share_dialog import ScreenShareDialog
```

- [ ] **Step 2: Create VideoEngine instance in MainWindow.__init__**

After `self.voice_engine = VoiceEngine()`:

```python
        self.video_engine = VideoEngine()
        self.video_engine.on_video_frame = self._on_video_frame
        self.video_engine.on_share_state_changed = self._on_share_state_changed
```

- [ ] **Step 3: Connect share screen signals**

In `__init__`, after connection_bar signal connections:

```python
        self.connection_bar.share_screen_requested.connect(self._on_share_screen)
        self.connection_bar.stop_screen_share_requested.connect(self._on_stop_share_screen)
```

- [ ] **Step 4: Pass video_engine to client.connect()**

In `_on_connect`, update:

```python
            success = self.client.connect(host, port, username, password,
                                          voice_engine=self.voice_engine,
                                          video_engine=self.video_engine)
```

- [ ] **Step 5: Implement share screen handlers**

```python
    def _on_share_screen(self):
        dialog = ScreenShareDialog(self)
        if dialog.exec_():
            config = dialog.get_config()
            if config:
                success = self.video_engine.start_share(
                    source_type=config["source_type"],
                    source_index=config["source_index"],
                    fps=config["fps"],
                )
                if not success:
                    InfoBar.warning(
                        self.tr("Screen Share Failed"),
                        self.tr("Could not start screen sharing."),
                        parent=self,
                        position=InfoBarPosition.TOP,
                        duration=3000,
                    )
                    self.connection_bar.set_sharing(False)

    def _on_stop_share_screen(self):
        self.video_engine.stop_share()

    def _on_share_state_changed(self, sharing):
        from PyQt5.QtCore import QMetaObject, Qt, Q_ARG
        QMetaObject.invokeMethod(
            self.connection_bar, "set_sharing",
            Qt.QueuedConnection,
            Q_ARG(bool, sharing)
        )

    def _on_video_frame(self, sender_id, frame_bgr, width, height):
        pass  # TODO: render to video widget in Phase 2
```

- [ ] **Step 6: Clean up on disconnect**

In the disconnect handler, add:

```python
        self.video_engine.stop_share()
        self.video_engine.stop_receive()
```

- [ ] **Step 7: Commit**

```bash
git add src/client/gui_python/main_window.py
git commit -m "feat: wire up VideoEngine in MainWindow with share screen UI"
```

---

### Task 15: Build and test server

**Files:** None (verification only)

- [ ] **Step 1: Regenerate protobuf C++ bindings**

Run: `cd C:\Users\yzd20\Desktop\NEVO\build && cmake --build . --config Release --target proto_gen`

- [ ] **Step 2: Build the server**

Run: `cmake --build C:\Users\yzd20\Desktop\NEVO\build --config Release --target nevo_server`

- [ ] **Step 3: Verify server starts with video UDP port**

Run the server and check logs for "Video UDP receive loop starting on port XXXX"

- [ ] **Step 4: Commit any build fixes**

---

### Task 16: Install Python dependencies and test client

**Files:** None (verification only)

- [ ] **Step 1: Install new Python packages**

Run: `pip install mss av`

- [ ] **Step 2: Test screen capture module**

Run: `python -c "from screen_capture import ScreenCapture; s = ScreenCapture(); print(s.enumerate_sources())"`

- [ ] **Step 3: Test video encoder module**

Run: `python -c "from video_encoder import VideoEncoder; e = VideoEncoder(); print(e.init(1920, 1080))"`

- [ ] **Step 4: End-to-end smoke test**

Launch server, connect two clients, start screen share on one, verify video frames are received on the other (check logs).
