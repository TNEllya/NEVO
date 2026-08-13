#!/usr/bin/env python3
"""双探针：A 发送、B 监听，同时挂在同一个网关上，验证该网关实例的语音中继。"""
import base64
import json
import os
import socket
import struct
import sys
import threading
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8088
SERVER_HOST = "192.168.31.39"
CHANNEL_ID = 2


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
        self.login_resp = None
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
                        if msg.get("event") == "voice_frame":
                            self.voice_frames.append(msg["data"])
                        elif msg.get("id") == 1:
                            self.login_resp = msg

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
    a, b = WSClient(), WSClient()
    a.send({"action": "login", "id": 1, "params": {
        "host": SERVER_HOST, "port": 24430, "username": "ProbeSend", "password": ""}})
    b.send({"action": "login", "id": 1, "params": {
        "host": SERVER_HOST, "port": 24430, "username": "ProbeRecv", "password": ""}})
    time.sleep(4)
    print(f"[A] login: ok={a.login_resp and a.login_resp.get('ok')} udp={a.login_resp and a.login_resp.get('server_udp_port')}")
    print(f"[B] login: ok={b.login_resp and b.login_resp.get('ok')} udp={b.login_resp and b.login_resp.get('server_udp_port')}")
    a.send({"action": "join_channel", "id": 2, "params": {"channel_id": CHANNEL_ID}})
    b.send({"action": "join_channel", "id": 2, "params": {"channel_id": CHANNEL_ID}})
    time.sleep(3)
    dummy = base64.b64encode(bytes([0xCD] * 80)).decode()
    for _ in range(50):
        a.send({"action": "media_frame", "id": 0, "params": {"type": "voice", "data": dummy}})
        time.sleep(0.02)
    deadline = time.time() + 6
    while time.time() < deadline and not b.voice_frames:
        time.sleep(0.2)
    print(f"[RESULT] B received {len(b.voice_frames)} voice frames via gateway port {PORT}")
    print("GATEWAY VOICE RELAY: " + ("OK" if b.voice_frames else "FAILED"))
    a.close()
    b.close()


if __name__ == "__main__":
    main()
