import http.server
import os
import socketserver
import threading
from playwright.sync_api import sync_playwright

WEBCLIENT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = "127.0.0.1"
PORT = 8766
URL = f"http://{HOST}:{PORT}"


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEBCLIENT_DIR, **kwargs)

    def log_message(self, format, *args):
        pass


def start_server():
    socketserver.TCPServer.allow_reuse_address = True
    server = socketserver.TCPServer((HOST, PORT), QuietHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def run_test():
    server = start_server()
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page()
            page.add_init_script("""
                class FakeWebSocket {
                    static OPEN = 1;
                    constructor() {
                        this.readyState = FakeWebSocket.OPEN;
                        setTimeout(() => {
                            if (this.onopen) this.onopen();
                            this.emit({ event: 'ws_connected', data: { client_available: true } });
                        }, 0);
                    }
                    send(raw) {
                        const message = JSON.parse(raw);
                        if (message.action !== 'login') return;
                        this.emit({
                            ok: true,
                            user_id: 42,
                            username: message.params.username,
                            is_admin: false,
                            id: message.id
                        });
                        this.emit({ event: 'state_changed', data: { state: 'in_channel' } });
                        this.emit({
                            event: 'channel_list',
                            data: {
                                channels: [
                                    { id: 1, name: 'Root', parent_id: 0, users: [] },
                                    { id: 2, name: 'Lobby', parent_id: 0, users: [
                                        { id: 42, username: message.params.username, muted: false, deafened: false }
                                    ] }
                                ]
                            }
                        });
                    }
                    emit(message) {
                        if (this.onmessage) this.onmessage({ data: JSON.stringify(message) });
                    }
                }
                window.WebSocket = FakeWebSocket;
            """)
            page.goto(URL)
            page.wait_for_load_state("networkidle")
            page.evaluate("""
                window.__voiceStartCount = 0;
                window.NevoMedia.startVoice = async () => {
                    window.__voiceStartCount += 1;
                    window.NevoMedia.state.voiceActive = true;
                };
            """)
            page.fill("#input-username", "AutoChannelUser")
            page.click("#btn-connect")
            page.wait_for_function("window.NevoApp.state.inChannel === true")

            result = page.evaluate("""
                ({
                    channelId: window.NevoApp.state.currentChannelId,
                    channelName: window.NevoApp.state.currentChannelName,
                    userIds: window.NevoApp.state.channelUsers.map(user => user.id),
                    voiceStartCount: window.__voiceStartCount
                })
            """)
            assert result["channelId"] == 2, result
            assert result["channelName"] == "Lobby", result
            assert result["userIds"] == [42], result
            assert result["voiceStartCount"] == 1, result

            page.evaluate("""
                window.NevoApp.state.ws.emit({
                    event: 'channel_list',
                    data: { channels: window.NevoApp.state.channels }
                });
            """)
            assert page.evaluate("window.__voiceStartCount") == 1
            browser.close()
    finally:
        server.shutdown()


if __name__ == "__main__":
    run_test()
