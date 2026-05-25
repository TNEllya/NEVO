# NEVO Screen Sharing Design

## Overview

Add screen sharing (Screen Capture) to NEVO VoIP, allowing users to share their screen, a specific application window, or a browser tab with other channel participants, optionally including system or per-application audio.

## Architecture

**Transport**: Server-relayed UDP (independent video channel, separate from voice UDP).

```
Sharer → ScreenCapture → H.264 Encode → Encrypt → UDP → VideoRelay → UDP → Viewer → Decrypt → H.264 Decode → Render
```

### Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Transport | Independent UDP channel | Voice/video isolation, no QoS interference |
| Codec | H.264 High Profile | SCC mode for screen content, wide compatibility |
| Encryption | XChaCha20-Poly1305 (reuse VoiceCrypto) | Same security model as voice |
| Relay | Passthrough (no server decrypt/re-encrypt) | All viewers share same session key |
| Audio | Reuse existing Opus voice channel | Marked as screen_audio type |

## New Files

### Client (Python)

| File | Purpose |
|------|---------|
| `src/client/gui_python/screen_capture.py` | Screen/window/tab capture via mss + platform APIs |
| `src/client/gui_python/video_encoder.py` | H.264 encode/decode via PyAV |
| `src/client/gui_python/video_engine.py` | Video send/receive engine (parallel to voice_engine.py) |
| `src/client/gui_python/screen_share_dialog.py` | Source selection dialog |
| `src/client/gui_python/screen_audio_capture.py` | System/app audio capture |

### Server (C++)

| File | Purpose |
|------|---------|
| `src/server/include/nevo/server/VideoRelay.h` | Video packet relay header |
| `src/server/src/VideoRelay.cpp` | Video packet relay implementation |

### Protocol

| File | Purpose |
|------|---------|
| `proto/video.proto` | VideoPacketHeader definition |
| `proto/control.proto` | Screen share control messages (types 60-62) |

## Protocol Design

### video.proto

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

### Control Messages (control.proto additions)

```protobuf
// Message types
MSG_SCREEN_SHARE_START = 60;
MSG_SCREEN_SHARE_STOP = 61;
MSG_SCREEN_SHARE_STATE = 62;

message ScreenShareStartRequest {
    uint32 channel_id = 1;
    uint32 source_type = 2;     // 0=screen, 1=window, 2=tab
    string source_name = 3;
    uint32 width = 4;
    uint32 height = 5;
    uint32 fps = 6;
    bool share_audio = 7;
    uint32 audio_source = 8;    // 0=none, 1=system, 2=application
    uint32 audio_app_pid = 9;   // PID for per-app audio (Windows)
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

### Wire Format

Video UDP packets use the same format as voice:

```
[2-byte header length][protobuf VideoPacketHeader][encrypted H.264 NAL fragment]
```

Large NAL units are fragmented at the application level to fit UDP MTU (max 1200 bytes per fragment).

## Client Modules

### ScreenCapture

Cross-platform screen capture using `mss` library:

- `enumerate_sources()` → list of screens and windows
- `start(source_type, source_index, fps)` → begin capture
- `capture_frame()` → returns BGR numpy array
- `stop()` → end capture

Platform-specific window enumeration:
- Windows: `win32gui.EnumWindows`
- macOS: `pyobjc` NSWorkspace
- Linux: `ewmh` / X11

### VideoEncoder / VideoDecoder

H.264 via PyAV (FFmpeg bindings):

```python
# Encoding parameters for screen sharing
codec_options = {
    'preset': 'ultrafast',
    'tune': 'zerolatency',
    'profile': 'high',
}
```

### VideoEngine

Parallel architecture to VoiceEngine:

- `start_share(source_type, source_index, fps)` → capture + encode + send loop
- `stop_share()` → end sharing
- `_capture_loop()` → capture frame → encode → fragment → encrypt → UDP send
- `_recv_loop()` → UDP recv → reassemble fragments → decrypt → decode → render callback
- Fragment reassembly buffer: maps `(sender_id, sequence_number)` → list of fragments

### ScreenAudioCapture

Audio source options:

| Source | Implementation | Platform |
|--------|---------------|----------|
| None | No audio | All |
| System Audio | WASAPI loopback / CoreAudio tap / PulseAudio monitor | Win/Mac/Linux |
| Per-App Audio | WASAPI per-process capture | Windows only |

Captured audio is encoded with Opus and sent through the existing voice UDP channel with a `screen_audio` flag in the VoicePacketHeader.

## Server Module

### VideoRelay

Mirrors AudioRelay architecture but simpler (passthrough only):

```cpp
class VideoRelay {
public:
    void handleVideoPacket(const uint8_t* data, uint32_t size,
                           const udp::endpoint& sender);
    void addClientMapping(UserId, const udp::endpoint&);
    void removeClientMapping(UserId);
    void updateClientChannel(UserId, ChannelId);
    void setChannelManager(std::shared_ptr<ChannelManager>);
    void setUdpSocket(std::shared_ptr<UdpSocket>);
    void setIoContext(boost::asio::io_context&);
private:
    std::mutex mutex_;
    std::unordered_map<UserId, ClientUdpMapping> client_map_;
    // ... same structure as AudioRelay
};
```

Key difference from AudioRelay: **no decrypt/re-encrypt**. All channel members share the same session key, so video packets are forwarded as-is.

### ServerCore Changes

- Add `VideoRelay` instance alongside `AudioRelay`
- Bind video UDP socket on a separate port (e.g., tcp_port + 2)
- Add video UDP port to `LoginResponse`
- Handle `ScreenShareStart/Stop` control messages
- Broadcast `ScreenShareState` to channel members

## User Interaction

### ScreenShareDialog

Three-tab dialog for source selection:

1. **Screen tab**: Thumbnail previews of each monitor
2. **Window tab**: List of application windows with icons
3. **Browser Tab tab**: (Electron/Chromium only, future)

Additional options:
- Audio source dropdown (None / System / Application)
- FPS slider (5-30, default 15)
- Resolution limit (Original / 1080p / 720p / 480p)

### ConnectionBar Integration

- Add "Share Screen" button (monitor icon)
- When sharing: button turns red, shows "Stop Sharing"
- Other users see a "screen" icon next to the sharer's name in the channel user list

### Video Rendering

- Render decoded frames in a `QLabel` or custom `QWidget` using `QImage`
- Overlay in a separate window or embedded in the main UI
- Click to fullscreen

## Encoding Parameters

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Codec | H.264 High Profile | SCC support for screen content |
| Preset | `ultrafast` | Minimum encoding latency |
| Tune | `zerolatency` | Disable lookahead, zero-delay |
| FPS | 15 (default), 5-30 range | Screen changes slowly |
| Bitrate | 1.5 Mbps @ 720p, 3 Mbps @ 1080p | Adaptive to content |
| Max Resolution | 1920x1080 | Downscale if larger |
| Keyframe Interval | 3 seconds (45 frames @ 15fps) | Balance seek vs bandwidth |
| Pixel Format | YUV420P | Maximum compatibility |
| NAL Fragment Size | ≤1200 bytes | UDP MTU safe |
| Jitter Buffer | 3 frames | Balance latency vs smoothness |

## Platform Compatibility

### Windows

- Screen capture: DXGI Desktop Duplication API (via mss) — best performance
- Window enumeration: `win32gui.EnumWindows`
- System audio: WASAPI loopback device
- Per-app audio: WASAPI `IAudioSessionManager2` by PID
- Permissions: None required

### macOS

- Screen capture: `CGWindowListCreateImage` (via mss)
- Window enumeration: `pyobjc` NSWorkspace
- System audio: CoreAudio tap (requires third-party driver like BlackHole for loopback)
- Permissions: Screen Recording permission required (System Preferences → Privacy & Security)
- Must detect and guide user to grant permission on first use

### Linux

- Screen capture: X11 `XGetImage` (via mss)
- Window enumeration: `ewmh` / X11
- System audio: PulseAudio/PipeWire monitor source
- **Wayland**: Requires XDG Desktop Portal + PipeWire (significant complexity)
- Permissions: Varies by display server; X11 generally open, Wayland requires portal permission

### Mobile (Future)

- Android: MediaProjection API (requires user confirmation dialog)
- iOS: ReplayKit (requires screen recording permission)
- Not in MVP scope

## Fragment Reassembly

Video frames (NAL units) may be split across multiple UDP packets. Reassembly logic:

```python
class FragmentReassembler:
    def __init__(self):
        self._buffers = {}  # (sender_id, seq) → {fragments, total, received}

    def add_fragment(self, sender_id, seq, frag_idx, frag_total, data):
        key = (sender_id, seq)
        if key not in self._buffers:
            self._buffers[key] = {
                'total': frag_total,
                'received': {},
                'timestamp': time.time()
            }
        self._buffers[key]['received'][frag_idx] = data

        if len(self._buffers[key]['received']) == frag_total:
            # All fragments received, reassemble
            nal = b''.join(self._buffers[key]['received'][i]
                          for i in range(frag_total))
            del self._buffers[key]
            return nal
        return None

    def cleanup_stale(self, max_age=5.0):
        """Remove incomplete buffers older than max_age seconds"""
        now = time.time()
        stale = [k for k, v in self._buffers.items()
                 if now - v['timestamp'] > max_age]
        for k in stale:
            del self._buffers[k]
```

## Dependencies

### New Python Packages

```
mss>=9.0          # Cross-platform screen capture
av>=12.0          # PyAV - FFmpeg bindings for H.264
pywin32>=306      # Windows window enumeration (optional, Windows only)
pyobjc>=9.0       # macOS window enumeration (optional, macOS only)
```

### Server (C++)

No new dependencies. VideoRelay reuses existing UdpSocket, ChannelManager, and protobuf infrastructure.

## Scope

### MVP (Phase 1)

- Screen sharing (full screen + window selection)
- H.264 encode/decode
- Server VideoRelay passthrough
- Basic source selection dialog
- Windows support (primary target)

### Phase 2

- System audio capture (WASAPI loopback)
- Per-application audio capture (Windows)
- macOS support with permission handling
- Resolution scaling options

### Phase 3

- Linux support (X11 + Wayland Portal)
- Browser tab sharing (Electron integration)
- Adaptive bitrate based on network conditions
- Mobile screen sharing
