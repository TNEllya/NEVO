#!/usr/bin/env python3
"""Simple WebSocket client to exercise NEVO gateway login/join."""
import json
import sys
import time
import threading
import websocket

USER = "WsTester"
CHANNEL_ID = 2  # Lobby

received = []

def on_message(ws, msg):
    print("[WS RECV]", msg)
    received.append(json.loads(msg))

def on_open(ws):
    print("[WS] open")
    ws.send(json.dumps({"action": "login", "params": {"host": "127.0.0.1", "port": 24430, "username": USER, "password": ""}}))

def on_error(ws, err):
    print("[WS ERROR]", err)

def on_close(ws, code, reason):
    print("[WS CLOSE]", code, reason)

ws = websocket.WebSocketApp("ws://127.0.0.1:8088/ws",
                            on_open=on_open,
                            on_message=on_message,
                            on_error=on_error,
                            on_close=on_close)

t = threading.Thread(target=ws.run_forever, daemon=True)
t.start()

# wait for login response
for _ in range(20):
    time.sleep(0.5)
    if any(e.get("event") == "state_changed" and e.get("data", {}).get("state") == "connected" for e in received):
        break

# join Lobby
print("[WS] sending join_channel", CHANNEL_ID)
ws.send(json.dumps({"action": "join_channel", "params": {"channel_id": CHANNEL_ID}}))

# keep alive for 10s so UDP keepalive packets go out
for _ in range(20):
    time.sleep(0.5)
    if any(e.get("event") == "user_joined" for e in received):
        print("[WS] saw user_joined")

print("[WS] closing")
ws.close()
t.join(timeout=2)
print("done")
