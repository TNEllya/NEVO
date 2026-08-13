#!/usr/bin/env python3
"""Two-gateway voice relay smoke test."""
import base64
import json
import time
import threading
import websocket

GATEWAY_A = "ws://127.0.0.1:8088/ws"
GATEWAY_B = "ws://127.0.0.1:8089/ws"
CHANNEL_ID = 2  # Lobby

def make_client(url, user):
    state = {"connected": False, "in_channel": False, "voice_frames": 0, "speaking_events": [], "closed": False}
    ws = {"obj": None}

    def on_open(ws_obj):
        ws["obj"] = ws_obj
        ws_obj.send(json.dumps({
            "action": "login",
            "params": {"host": "127.0.0.1", "port": 24430, "username": user, "password": ""}
        }))

    def on_msg(ws_obj, msg):
        data = json.loads(msg)
        # print(f"[{user}] {data}")
        if data.get("event") == "state_changed":
            if data["data"]["state"] == "connected":
                state["connected"] = True
            elif data["data"]["state"] == "in_channel":
                state["in_channel"] = True
        elif data.get("event") == "voice_frame":
            state["voice_frames"] += 1
        elif data.get("event") == "user_speaking":
            state["speaking_events"].append(data.get("data", {}))
        elif data.get("event") == "channel_list" and not state["in_channel"]:
            # server auto-joined default channel
            pass

    def on_close(ws_obj, code, reason):
        state["closed"] = True

    app = websocket.WebSocketApp(url,
                                 on_open=on_open,
                                 on_message=on_msg,
                                 on_close=on_close)
    t = threading.Thread(target=app.run_forever, daemon=True)
    t.start()
    return app, state, ws

print("Connecting A...")
app_a, state_a, ws_a = make_client(GATEWAY_A, "FirstUser")
print("Connecting B...")
app_b, state_b, ws_b = make_client(GATEWAY_B, "SecondUser")

# Wait for both connected
for _ in range(30):
    time.sleep(0.5)
    if state_a["connected"] and state_b["connected"]:
        break
print(f"A connected={state_a['connected']}, B connected={state_b['connected']}")

# Explicitly join channel
if ws_a["obj"]:
    ws_a["obj"].send(json.dumps({"action": "join_channel", "params": {"channel_id": CHANNEL_ID}}))
if ws_b["obj"]:
    ws_b["obj"].send(json.dumps({"action": "join_channel", "params": {"channel_id": CHANNEL_ID}}))

for _ in range(30):
    time.sleep(0.5)
    if state_a["in_channel"] and state_b["in_channel"]:
        break
print(f"A in_channel={state_a['in_channel']}, B in_channel={state_b['in_channel']}")

# Give time for UDP registrations to reach server
print("Waiting for UDP registrations...")
time.sleep(3)

# Send a dummy voice frame from A
DUMMY_OPUS = base64.b64encode(b"\x00" * 80).decode("ascii")
print("Sending media_frame from A...")
if ws_a["obj"]:
    ws_a["obj"].send(json.dumps({
        "action": "media_frame",
        "params": {"type": "voice", "data": DUMMY_OPUS}
    }))

# Also send speaking_state action from A to test the TCP broadcast path
print("Sending speaking_state(true) from A...")
if ws_a["obj"]:
    ws_a["obj"].send(json.dumps({
        "action": "speaking_state",
        "params": {"speaking": True},
        "id": 0
    }))

# Wait for B to receive relayed voice_frame
print("Waiting for B to receive voice_frame...")
for _ in range(20):
    time.sleep(0.5)
    if state_b["voice_frames"] > 0:
        break

# Wait a bit longer for user_speaking state to propagate via TCP
print("Waiting for speaking state events...")
time.sleep(2)

print(f"\nRESULT: B received {state_b['voice_frames']} voice_frame event(s)")
if state_b["voice_frames"] > 0:
    print("Voice relay path is WORKING (A -> server -> B).")
else:
    print("Voice relay path NOT working.")

speaking_true_events = [e for e in state_b["speaking_events"] if e.get("speaking")]
print(f"RESULT: B received {len(speaking_true_events)} user_speaking(true) event(s) from A")
if speaking_true_events:
    print("Speaking state sync path is WORKING (A -> server -> B).")
    # Show which user_id the speaking events are for
    for e in speaking_true_events:
        print(f"  user_id={e.get('user_id')}, speaking={e.get('speaking')}")
else:
    print("Speaking state sync path NOT working.")

if ws_a["obj"]:
    ws_a["obj"].close()
if ws_b["obj"]:
    ws_b["obj"].close()
time.sleep(1)
print("Done")
