import json
import sys
import time

from playwright.sync_api import sync_playwright

SERVER = sys.argv[1] if len(sys.argv) > 1 else "192.168.31.39"
PORT = sys.argv[2] if len(sys.argv) > 2 else "24430"
NAME_A = sys.argv[3] if len(sys.argv) > 3 else "E2E_A"
NAME_B = sys.argv[4] if len(sys.argv) > 4 else "E2E_B"
CDP_A = int(sys.argv[5]) if len(sys.argv) > 5 else 9222
CDP_B = int(sys.argv[6]) if len(sys.argv) > 6 else 9223

INJECT = r"""
if (!window.__voiceStats) {
  window.__voiceStats = { sent: 0, received: 0, injected: false };
  try {
    if (window.NevoMedia && typeof window.NevoMedia.handleVoiceFrame === 'function') {
      const origHandle = window.NevoMedia.handleVoiceFrame;
      window.NevoMedia.handleVoiceFrame = function (payload) {
        window.__voiceStats.received++;
        return origHandle.call(this, payload);
      };
    }
  } catch (e) { window.__voiceStats.injectErr = String(e); }
  try {
    const origSend = WebSocket.prototype.send;
    WebSocket.prototype.send = function (data) {
      try {
        const d = JSON.parse(data);
        if (d && d.action === 'media_frame' && d.params && d.params.type === 'voice') window.__voiceStats.sent++;
      } catch (e) {}
      return origSend.apply(this, arguments);
    };
  } catch (e) { window.__voiceStats.injectErr2 = String(e); }
} else {
  window.__voiceStats.sent = 0;
  window.__voiceStats.received = 0;
}
window.__voiceStats.injected = true;
"""

STATE_JS = r"""
() => {
  const s = window.NevoApp.state;
  const m = window.NevoMedia ? window.NevoMedia.state : {};
  const enc = m.audioEncoder || null;
  const dec = m.audioDecoder || null;
  const rctx = m.remoteAudioContext || null;
  return {
    connected: !!s.connected,
    inChannel: !!s.inChannel,
    currentChannelId: s.currentChannelId,
    currentChannelName: s.currentChannelName,
    channelUsers: (s.channelUsers || []).map(u => ({ id: u.id, username: u.username })),
    userId: s.userId,
    voiceActive: !!m.voiceActive,
    encoder: enc ? enc.state : null,
    decoder: dec ? dec.state : null,
    audioContext: m.audioContext ? m.audioContext.state : null,
    remoteAudioContext: rctx ? rctx.state : null,
    voiceStats: window.__voiceStats || null
  };
}
"""


def find_app_page(browser):
    for ctx in browser.contexts:
        for pg in ctx.pages:
            try:
                if pg.url.startswith("http://127.0.0.1:8088"):
                    return pg
            except Exception:
                continue
    raise RuntimeError("app page not found in CDP browser")


def login(page, name):
    page.reload()
    page.wait_for_selector("#input-host", state="visible", timeout=15000)
    page.wait_for_function("() => !!(window.NevoApp && window.NevoMedia && window.NevoMedia.handleVoiceFrame)")
    page.evaluate(INJECT)
    page.fill("#input-host", SERVER)
    page.fill("#input-port", PORT)
    page.fill("#input-username", name)
    page.fill("#input-password", "")
    page.click("#btn-connect")


def wait_ready(page, timeout_s=25):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            st = page.evaluate(
                "() => ({ c: window.NevoApp.state.connected, id: window.NevoApp.state.currentChannelId })"
            )
            if st["c"] and st["id"] and st["id"] > 0:
                return True
        except Exception:
            pass
        time.sleep(0.5)
    return False


def read_state(page):
    return page.evaluate(STATE_JS)


def chat_texts(page):
    return page.evaluate(
        "() => Array.from(document.querySelectorAll('#messages .msg-text')).map(e => e.textContent)"
    )


def send_chat(page, text):
    page.fill("#chat-input", text)
    page.press("#chat-input", "Enter")


def main():
    marker = "E2E-%d" % int(time.time())
    results = {"server": SERVER, "port": PORT, "names": [NAME_A, NAME_B], "cdp": [CDP_A, CDP_B]}
    with sync_playwright() as p:
        b_a = p.chromium.connect_over_cdp("http://127.0.0.1:%d" % CDP_A)
        b_b = p.chromium.connect_over_cdp("http://127.0.0.1:%d" % CDP_B)
        page_a = find_app_page(b_a)
        page_b = find_app_page(b_b)
        print("[E2E] pages:", page_a.url, page_b.url)

        login(page_a, NAME_A)
        login(page_b, NAME_B)

        ok_a = wait_ready(page_a)
        ok_b = wait_ready(page_b)
        results["login_ready"] = {"A": ok_a, "B": ok_b}
        print("[E2E] login ready:", results["login_ready"])

        time.sleep(2)
        st_a = read_state(page_a)
        st_b = read_state(page_b)
        results["clientA"] = st_a
        results["clientB"] = st_b

        ids_a = {u["id"] for u in st_a["channelUsers"]}
        ids_b = {u["id"] for u in st_b["channelUsers"]}
        results["checks"] = {
            "A_in_lobby": st_a["currentChannelId"] == 2 and st_a["currentChannelName"] == "Lobby",
            "B_in_lobby": st_b["currentChannelId"] == 2 and st_b["currentChannelName"] == "Lobby",
            "A_sees_B": st_a["userId"] in ids_a and NAME_B in {u["username"] for u in st_a["channelUsers"]},
            "B_sees_A": st_b["userId"] in ids_b and NAME_A in {u["username"] for u in st_b["channelUsers"]},
            "A_voice_active": st_a["voiceActive"] and st_a["encoder"] == "configured" and st_a["audioContext"] == "running",
            "B_voice_active": st_b["voiceActive"] and st_b["encoder"] == "configured" and st_b["audioContext"] == "running",
            "A_remote_running": st_a["remoteAudioContext"] == "running" and st_a["decoder"] == "configured",
            "B_remote_running": st_b["remoteAudioContext"] == "running" and st_b["decoder"] == "configured",
        }

        send_chat(page_a, "hello from A " + marker)
        got = None
        deadline = time.time() + 12
        while time.time() < deadline and got is None:
            for t in chat_texts(page_b):
                if marker in t:
                    got = t
                    break
            time.sleep(0.5)
        results["chat_A_to_B_delivered"] = bool(got)
        results["chat_b_rendered"] = got

        time.sleep(8)
        st_a2 = read_state(page_a)
        st_b2 = read_state(page_b)
        results["voice_stats_A"] = st_a2["voiceStats"]
        results["voice_stats_B"] = st_b2["voiceStats"]
        results["checks"]["A_sending_frames"] = bool(st_a2["voiceStats"] and st_a2["voiceStats"]["sent"] > 0)
        results["checks"]["B_sending_frames"] = bool(st_b2["voiceStats"] and st_b2["voiceStats"]["sent"] > 0)
        results["checks"]["A_receiving_frames"] = bool(st_a2["voiceStats"] and st_a2["voiceStats"]["received"] > 0)
        results["checks"]["B_receiving_frames"] = bool(st_b2["voiceStats"] and st_b2["voiceStats"]["received"] > 0)

        all_ok = all(results["checks"].values()) and bool(got)
        results["ALL_PASS"] = all_ok
        print(json.dumps(results, ensure_ascii=False, indent=2))
        b_a.close()
        b_b.close()
        return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
