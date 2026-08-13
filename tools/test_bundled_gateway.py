#!/usr/bin/env python3
"""通过 WebSocket 测试打包好的 nevo_gateway.exe 链路（模拟浏览器行为）。"""
import base64
import json
import os
import socket
import struct
import sys
import threading
import time
import hashlib

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8098
SERVER_HOST = sys.argv[2] if len(sys.argv) > 2 else "192.168.31.39"
NAMES = sys.argv[3:5] or ["BundleTestA", "BundleTestB"]


class WSClient:
    def __init__(self, name):
        self.name = name
        self.sock = socket.create_connection((HOST, PORT), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET /ws HTTP/1.1\r\nHost: {HOST}:{PORT}\r\n"
               f"Upgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
        self.sock.sendall(req.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            resp += self.sock.recv(1024)
        assert b"101" in resp.split(b"\r\n")[0], f"upgrade failed: {resp[:100]}"
        self.events = []
        self.responses = []
        self.voice_frames = []
        self.chat_messages = []
        self.user_joined = []
        self.lock = threading.Lock()
        threading.Thread(target=self._recv_loop, daemon=True).start()

    def _recv_loop(self):
        buf = b""
        while True:
            try:
                chunk = self.sock.recv(4096)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while True:
                if len(buf) < 2:
                    break
                b0, b1 = buf[0], buf[1]
                ln = b1 & 0x7F
                off = 2
                if ln == 126:
                    if len(buf) < 4:
                        break
                    ln = struct.unpack(">H", buf[2:4])[0]
                    off = 4
                elif ln == 127:
                    if len(buf) < 10:
                        break
                    ln = struct.unpack(">Q", buf[2:10])[0]
                    off = 10
                if len(buf) < off + ln:
                    break
                payload = buf[off:off + ln]
                buf = buf[off + ln:]
                if (b0 & 0x0F) == 0x01:
                    try:
                        msg = json.loads(payload.decode("utf-8"))
                    except Exception:
                        continue
                    with self.lock:
                        self.events.append(msg)
                        ev = msg.get("event")
                        if ev == "voice_frame":
                            self.voice_frames.append(msg["data"])
                        elif ev == "chat_message":
                            self.chat_messages.append(msg["data"])
                        elif ev == "user_joined":
                            self.user_joined.append(msg["data"])
                        elif "id" in msg:
                            self.responses.append(msg)

    def send(self, obj):
        data = json.dumps(obj).encode()
        mask = os.urandom(4)
        header = bytearray([0x81])
        ln = len(data)
        if ln < 126:
            header.append(0x80 | ln)
        elif ln < 65536:
            header.append(0x80 | 126)
            header += struct.pack(">H", ln)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", ln)
        header += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        self.sock.sendall(bytes(header) + masked)

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def main():
    print(f"Testing bundled gateway at ws://{HOST}:{PORT}/ws -> server {SERVER_HOST}")
    a = WSClient("A")
    b = WSClient("B")
    time.sleep(0.5)

    a.send({"action": "login", "id": 1, "params": {
        "host": SERVER_HOST, "port": 24430, "username": NAMES[0], "password": ""}})
    b.send({"action": "login", "id": 1, "params": {
        "host": SERVER_HOST, "port": 24430, "username": NAMES[1], "password": ""}})
    time.sleep(4)

    def login_result(c):
        for r in c.responses:
            if r.get("id") == 1:
                return r
        return None

    la, lb = login_result(a), login_result(b)
    print(f"A login: {json.dumps(la, ensure_ascii=False)}")
    print(f"B login: {json.dumps(lb, ensure_ascii=False)}")
    if not (la and la.get("ok") and lb and lb.get("ok")):
        print("LOGIN FAILED via bundled gateway")
        return 2

    # 找频道
    channels = None
    for ev in a.events:
        if ev.get("event") == "channel_list":
            channels = ev["data"]["channels"]
    print(f"A channel_list: {json.dumps(channels, ensure_ascii=False)}")

    # 选择双方可见的频道（优先 Lobby）
    cid = None
    if channels:
        for ch in channels:
            if ch.get("name") == "Lobby":
                cid = ch["id"]
                break
        if cid is None:
            cid = channels[-1]["id"]
    print(f"Both join channel id={cid}")
    a.send({"action": "join_channel", "id": 2, "params": {"channel_id": cid}})
    b.send({"action": "join_channel", "id": 2, "params": {"channel_id": cid}})
    time.sleep(3)

    # ---- 聊天测试 ----
    marker = f"bundled-{int(time.time())}"
    a.send({"action": "send_chat", "id": 3,
            "params": {"text": marker, "channel_id": cid}})
    deadline = time.time() + 5
    while time.time() < deadline:
        if any(marker in m.get("text", "") for m in b.chat_messages):
            break
        time.sleep(0.2)
    chat_ok = any(marker in m.get("text", "") for m in b.chat_messages)
    print(f"CHAT via bundled gateway: {'OK' if chat_ok else 'FAILED'}")

    # ---- 语音测试 ----
    dummy = base64.b64encode(bytes(range(80))).decode()
    for _ in range(10):
        a.send({"action": "media_frame", "id": 4,
                "params": {"type": "voice", "data": dummy}})
        time.sleep(0.05)
    deadline = time.time() + 6
    while time.time() < deadline and not b.voice_frames:
        time.sleep(0.2)
    voice_ok = len(b.voice_frames) > 0
    print(f"B received {len(b.voice_frames)} voice frames")
    if voice_ok:
        payload = base64.b64decode(b.voice_frames[0]["data"])
        print(f"voice sender_id={b.voice_frames[0]['sender_id']} payload_match={payload == bytes(range(80))}")
    print(f"VOICE via bundled gateway: {'OK' if voice_ok else 'FAILED'}")

    a.close()
    b.close()
    print(f"\nSUMMARY(bundled): chat={'OK' if chat_ok else 'FAIL'} voice={'OK' if voice_ok else 'FAIL'}")
    return 0 if (chat_ok and voice_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
