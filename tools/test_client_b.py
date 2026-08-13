#!/usr/bin/env python3
"""客户端 B：加入 Lobby，与浏览器客户端 A 互测 聊天/语音/在线状态。"""
import base64
import json
import os
import socket
import struct
import sys
import threading
import time

HOST = "127.0.0.1"
PORT = 8088
SERVER_HOST = os.environ.get("NEVO_TEST_HOST", "your-server-host")
CHANNEL_ID = 2  # Lobby


class WSClient:
    def __init__(self):
        self.sock = socket.create_connection((HOST, PORT), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET /ws HTTP/1.1\r\nHost: {HOST}:{PORT}\r\n"
               f"Upgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
        self.sock.sendall(req.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            resp += self.sock.recv(1024)
        self.voice_frames = []
        self.chat_messages = []
        self.user_joined = []
        self.user_left = []
        self.user_speaking = []
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
                        ev = msg.get("event")
                        if ev == "voice_frame":
                            self.voice_frames.append(msg["data"])
                        elif ev == "chat_message":
                            self.chat_messages.append(msg["data"])
                        elif ev == "user_joined":
                            self.user_joined.append(msg["data"])
                        elif ev == "user_left":
                            self.user_left.append(msg["data"])
                        elif ev == "user_speaking":
                            self.user_speaking.append(msg["data"])

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
    mode = sys.argv[1] if len(sys.argv) > 1 else "listen"
    b = WSClient()
    b.send({"action": "login", "id": 1, "params": {
        "host": SERVER_HOST, "port": 24430, "username": "ProbeClientB", "password": ""}})
    time.sleep(3)
    b.send({"action": "join_channel", "id": 2, "params": {"channel_id": CHANNEL_ID}})
    time.sleep(3)
    print(f"[B] joined Lobby, users seen: {[u.get('user', {}).get('username') for u in b.user_joined]}")

    if mode == "listen":
        # 监听模式：等待浏览器 A 发来的聊天与语音
        print("[B] listening 25s for chat/voice from browser client A ...")
        deadline = time.time() + 25
        while time.time() < deadline:
            time.sleep(0.5)
            if b.voice_frames:
                break
        print(f"[B] chat received: {[(m['username'], m['text']) for m in b.chat_messages]}")
        print(f"[B] voice frames received: {len(b.voice_frames)}")
        if b.voice_frames:
            sizes = [len(base64.b64decode(f['data'])) for f in b.voice_frames[:5]]
            senders = set(f['sender_id'] for f in b.voice_frames)
            print(f"[B] sample payload sizes: {sizes}, senders: {senders}")
        print(f"[B] speaking events: {b.user_speaking[:5]}")
    else:
        # 发送模式：向 A 发聊天 + 语音帧
        marker = f"from-B-{int(time.time())}"
        b.send({"action": "send_chat", "id": 3,
                "params": {"text": marker, "channel_id": CHANNEL_ID}})
        print(f"[B] sent chat: {marker}")
        # 发送 2 秒的"音频"（20ms 帧 x 100）
        dummy = base64.b64encode(bytes([0xAB] * 80)).decode()
        for _ in range(100):
            b.send({"action": "media_frame", "id": 0,
                    "params": {"type": "voice", "data": dummy}})
            time.sleep(0.02)
        print("[B] sent 100 voice frames")
        time.sleep(2)

    b.close()
    print("[B] done")


if __name__ == "__main__":
    main()
