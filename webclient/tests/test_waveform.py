"""Playwright 自动化测试：验证右上角成员列表中自己的麦克风波形条能随音量更新。"""
import http.server
import socketserver
import threading
import time
import os
from playwright.sync_api import sync_playwright

WEBCLIENT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOST = "127.0.0.1"
PORT = 8765
URL = f"http://{HOST}:{PORT}"


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEBCLIENT_DIR, **kwargs)

    def log_message(self, format, *args):
        pass


def start_server():
    socketserver.TCPServer.allow_reuse_address = True
    server = socketserver.TCPServer((HOST, PORT), QuietHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


def run_test():
    server = start_server()
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page()
            page.goto(URL)
            page.wait_for_load_state("networkidle")

            # 注入当前用户 ID 并创建成员列表中“自己”那一行
            page.evaluate("""
                window.NevoApp.state.userId = 123;
                const container = document.getElementById('voice-users');
                container.innerHTML = '';
                const userEl = document.createElement('div');
                userEl.className = 'voice-user is-me';
                userEl.dataset.userId = '123';
                const wf = document.createElement('div');
                wf.className = 'waveform-bar';
                for (let i = 0; i < 4; i++) {
                    const bar = document.createElement('div');
                    bar.className = 'wf-bar';
                    bar.style.height = '4px';
                    wf.appendChild(bar);
                }
                userEl.appendChild(wf);
                const sb = document.createElement('div');
                sb.className = 'speaking-bars';
                for (let i = 0; i < 4; i++) {
                    const bar = document.createElement('div');
                    bar.className = 'bar';
                    sb.appendChild(bar);
                }
                userEl.appendChild(sb);
                container.appendChild(userEl);

                // 右下角波形也需要可见
                const my = document.getElementById('my-waveform');
                my.style.display = 'flex';
            """)

            # 1. updateWaveformUI 应被导出并正确更新右上角与右下角波形
            assert page.evaluate("typeof window.NevoMedia.updateWaveformUI === 'function'"), "updateWaveformUI 未导出"
            page.evaluate("window.NevoMedia.updateWaveformUI('123', 0.9)")
            time.sleep(0.05)

            def heights(sel):
                return page.evaluate(f"""
                    Array.from(document.querySelectorAll('{sel}')).map(b => parseFloat(b.style.height))
                """)

            right_heights = heights('.voice-user[data-user-id="123"] .waveform-bar .wf-bar')
            left_heights = heights('#my-waveform .wf-bar')
            assert all(h > 4 for h in right_heights), f"右上角波形条高度未更新: {right_heights}"
            assert all(h > 4 for h in left_heights), f"右下角波形条高度未更新: {left_heights}"

            # 1.5 静默（data=0）时两条形应回到基线 2px
            page.evaluate("window.NevoMedia.updateWaveformUI('123', 0)")
            time.sleep(0.05)
            right_heights = heights('.voice-user[data-user-id="123"] .waveform-bar .wf-bar')
            left_heights = heights('#my-waveform .wf-bar')
            assert all(h == 2 for h in right_heights), f"右上角静默时未回到基线: {right_heights}"
            assert all(h == 2 for h in left_heights), f"右下角静默时未回到基线: {left_heights}"

            # 2. 右上角波形容器应具备正确的 CSS（可见、flex 布局、条形有背景色）
            styles = page.evaluate("""
                (() => {
                    const wf = document.querySelector('.voice-user.is-me .waveform-bar');
                    const bar = wf.querySelector('.wf-bar');
                    const cs = getComputedStyle(wf);
                    const cbar = getComputedStyle(bar);
                    return {
                        display: cs.display,
                        height: cs.height,
                        barWidth: cbar.width,
                        barBg: cbar.backgroundColor
                    };
                })()
            """)
            assert styles["display"] == "flex", f"右上角波形容器 display 不正确: {styles['display']}"
            assert styles["height"] == "16px", f"右上角波形容器高度不正确: {styles['height']}"
            assert styles["barWidth"] == "3px", f"波形条宽度不正确: {styles['barWidth']}"
            assert styles["barBg"] != "rgba(0, 0, 0, 0)", f"波形条无背景色: {styles['barBg']}"

            # 3. startWaveformMonitor 应使用 NevoApp.state.userId 而不是未定义的 state
            assert page.evaluate("typeof window.NevoMedia._startWaveformMonitor === 'function'"), "_startWaveformMonitor 未导出"
            page.evaluate("""
                window.NevoMedia.state.voiceActive = true;
                window.NevoMedia.state.audioAnalyser = {
                    frequencyBinCount: 32,
                    getByteFrequencyData(arr) {
                        for (let i = 0; i < arr.length; i++) arr[i] = 240;
                    }
                };
                window.NevoMedia._startWaveformMonitor();
            """)
            time.sleep(0.05)
            right_heights = heights('.voice-user[data-user-id="123"] .waveform-bar .wf-bar')
            assert all(h > 4 for h in right_heights), f"startWaveformMonitor 未正确更新右上角波形: {right_heights}"

            browser.close()
            print("PASS: 波形更新与样式测试通过")
    finally:
        server.shutdown()


if __name__ == "__main__":
    run_test()
