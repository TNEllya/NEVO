#!/usr/bin/env python3
"""NEVO 完整链路诊断脚本（复现 clientv2 Web 客户端网关路径）

链路: NevoClient(TCP 控制) + MediaBridge(UDP 语音) <-> 服务器
用法: python tools/test_full_link.py [host]
"""
import base64
import io
import json
import os
import struct
import sys
import threading
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("NEVO_TEST_HOST", "your-server-host")
PORT = 24430
NAMES = sys.argv[2:4] or ["LinkTestA", "LinkTestB"]

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUI_DIR = os.path.join(ROOT, "src", "client", "gui_python")
WEB_DIR = os.path.join(ROOT, "webclient")
sys.path.insert(0, GUI_DIR)
sys.path.insert(0, WEB_DIR)

from nevo_client import NevoClient  # noqa: E402
from gateway import MediaBridge     # noqa: E402


class FakeWfile:
    """模拟 WebSocket 输出流，捕获网关发往浏览器的事件。"""

    def __init__(self):
        self.buf = io.BytesIO()
        self.events = []
        self.lock = threading.Lock()

    def write(self, data):
        with self.lock:
            self.buf.write(data)
            self._parse()

    def flush(self):
        pass

    def _parse(self):
        """解析已缓冲的 WS 文本帧。"""
        data = self.buf.getvalue()
        pos = 0
        while pos + 2 <= len(data):
            b0, b1 = data[pos], data[pos + 1]
            length = b1 & 0x7F
            off = pos + 2
            if length == 126:
                if off + 2 > len(data):
                    break
                length = struct.unpack(">H", data[off:off + 2])[0]
                off += 2
            elif length == 127:
                if off + 8 > len(data):
                    break
                length = struct.unpack(">Q", data[off:off + 8])[0]
                off += 8
            if off + length > len(data):
                break
            payload = data[off:off + length]
            pos = off + length
            if (b0 & 0x0F) == 0x01:
                try:
                    self.events.append(json.loads(payload.decode("utf-8")))
                except Exception:
                    pass
        self.buf = io.BytesIO(data[pos:])


def make_client(name):
    """创建一个模拟网关客户端：NevoClient + MediaBridge。"""
    ctx = {"client": NevoClient(), "wfile": FakeWfile(), "ws_lock": threading.Lock()}
    c = ctx["client"]
    ctx["state_changes"] = []
    ctx["chat_messages"] = []
    ctx["users_joined"] = []

    def on_state(new, old):
        ctx["state_changes"].append((int(old), int(new)))
        print(f"[{name}] state: {int(old)} -> {int(new)}")

    def on_chat(uid, uname, cid, text, ts):
        ctx["chat_messages"].append((uid, uname, text))
        print(f"[{name}] chat from {uname}: {text}")

    def on_user_joined(user):
        ctx["users_joined"].append(user)

    def on_error(code, msg):
        print(f"[{name}] ERROR {code}: {msg}")

    c.on_state_changed = on_state
    c.on_chat_message = on_chat
    c.on_user_joined = on_user_joined
    c.on_error = on_error

    print(f"[{name}] connecting to {HOST}:{PORT} ...")
    ok = c.connect(HOST, PORT, name, "")
    print(f"[{name}] login ok={ok} user_id={c.user_id} "
          f"session_key={'yes(' + c.session_key[:4].hex() + '...)' if c.session_key else 'NO'} "
          f"server_udp_port={c.server_udp_port} video_port={c.server_video_udp_port}")
    if not ok:
        return None

    # 与 gateway.py login 分支一致：创建 MediaBridge
    mb = MediaBridge(ctx["wfile"], ctx["ws_lock"], c)
    mb.start(HOST, c.server_udp_port, c.server_video_udp_port, c.user_id, c.session_key)
    ctx["bridge"] = mb
    return ctx


def main():
    print("=" * 60)
    print(f"NEVO full-link diagnosis against {HOST}:{PORT}")
    print("=" * 60)

    a = make_client(NAMES[0])
    b = make_client(NAMES[1])
    if not a or not b:
        print("LOGIN FAILED - 控制链路(TCP登录)不通")
        return 2

    time.sleep(1.5)

    # ---- 获取频道列表，选择公共频道 ----
    channels_a = a["client"].channels
    channels_b = b["client"].channels
    print(f"[A] channels: {json.dumps(channels_a, ensure_ascii=False)}")
    if not channels_a:
        print("CHANNEL LIST EMPTY - 服务器未下发频道列表")
        return 3

    # 找一个叶子频道（优先两边都能看到的）
    def leaf_channels(chs):
        out = []
        for ch in chs:
            kids = ch.get("children") or []
            if kids:
                out.extend(leaf_channels(kids))
            else:
                out.append(ch)
        return out

    leaves = leaf_channels(channels_a)
    target = leaves[0]
    cid = target["id"]
    print(f"Both clients joining channel id={cid} name={target.get('name')}")

    ok_a = a["client"].join_channel(cid)
    a["bridge"].set_channel(cid)
    time.sleep(0.5)
    ok_b = b["client"].join_channel(cid)
    b["bridge"].set_channel(cid)
    print(f"join ok: A={ok_a} B={ok_b}")

    # 等 UDP 注册包到达服务器
    print("Waiting 3s for UDP registration ...")
    time.sleep(3)

    # ---- 测试 1: 文字聊天 (TCP 控制链路) ----
    print("\n--- Test 1: chat (TCP control path) ---")
    marker = f"ping-{int(time.time())}"
    a["client"].send_chat(marker, cid)
    deadline = time.time() + 5
    while time.time() < deadline:
        if any(marker in t for _, _, t in b["chat_messages"]):
            break
        time.sleep(0.2)
    chat_ok = any(marker in t for _, _, t in b["chat_messages"])
    print(f"CHAT PATH: {'OK' if chat_ok else 'FAILED'}")

    # ---- 测试 2: UDP 语音中继 ----
    print("\n--- Test 2: voice relay (UDP path) ---")
    dummy_opus = bytes(range(80))
    for i in range(10):
        a["bridge"].send_voice_frame(dummy_opus)
        time.sleep(0.05)

    deadline = time.time() + 6
    voice_frames = 0
    while time.time() < deadline:
        voice_frames = sum(
            1 for e in b["wfile"].events
            if e.get("event") == "voice_frame")
        if voice_frames > 0:
            break
        time.sleep(0.2)
    print(f"B received {voice_frames} voice_frame event(s) from A")
    voice_ok = voice_frames > 0
    print(f"VOICE PATH: {'OK' if voice_ok else 'FAILED'}")

    if voice_ok:
        # 校验解密后的载荷
        ev = next(e for e in b["wfile"].events if e.get("event") == "voice_frame")
        payload = base64.b64decode(ev["data"]["data"])
        print(f"voice_frame sender_id={ev['data']['sender_id']} "
              f"payload_match={payload == dummy_opus}")

    # ---- 测试 3: user_joined 事件广播 ----
    print("\n--- Test 3: presence events ---")
    joined_ok = len(b["users_joined"]) > 0 or any(
        e.get("event") == "user_joined" for e in b["wfile"].events)
    print(f"B saw user_joined events: {joined_ok}")

    # ---- 清理 ----
    a["bridge"].stop()
    b["bridge"].stop()
    a["client"].disconnect()
    b["client"].disconnect()

    print("\n" + "=" * 60)
    print(f"SUMMARY: login=OK  chat={'OK' if chat_ok else 'FAIL'}  "
          f"voice={'OK' if voice_ok else 'FAIL'}  presence={'OK' if joined_ok else 'FAIL'}")
    print("=" * 60)
    return 0 if (chat_ok and voice_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
