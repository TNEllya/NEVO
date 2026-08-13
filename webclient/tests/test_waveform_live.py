"""端到端波形测试：用 Chromium 假麦克风启动语音，验证右上角波形真的会动。"""
import os
import subprocess
import sys
import time
from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATEWAY_HOST = "127.0.0.1"
GATEWAY_PORT = 8088
URL = f"http://{GATEWAY_HOST}:{GATEWAY_PORT}"


def start_gateway():
    env = os.environ.copy()
    env["NEVO_WEB_HOST"] = GATEWAY_HOST
    env["NEVO_WEB_PORT"] = str(GATEWAY_PORT)
    proc = subprocess.Popen(
        [sys.executable, "-u", os.path.join(ROOT, "gateway.py")],
        cwd=ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    # 等待 HTTP 服务就绪
    for _ in range(30):
        line = proc.stdout.readline()
        if "Web UI:" in line or "ERROR" in line:
            break
        time.sleep(0.2)
    return proc


def run_test():
    proc = start_gateway()
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(
                headless=True,
                args=[
                    "--use-fake-device-for-media-stream",
                    "--use-fake-ui-for-media-stream",
                ],
            )
            context = browser.new_context()
            context.grant_permissions(["microphone"])
            page = context.new_page()
            page.goto(URL)
            page.wait_for_load_state("networkidle")

            # 模拟登录态并创建“自己”的成员行
            page.evaluate("""
                window.NevoApp.state.userId = 42;
                const list = document.getElementById('voice-users');
                list.innerHTML = '';
                const row = document.createElement('div');
                row.className = 'voice-user is-me';
                row.dataset.userId = '42';
                const wf = document.createElement('div');
                wf.className = 'waveform-bar';
                for (let i = 0; i < 4; i++) {
                    const b = document.createElement('div');
                    b.className = 'wf-bar';
                    wf.appendChild(b);
                }
                row.appendChild(wf);
                list.appendChild(row);

                // 确保左下波形容器可见（实际由 app.js 控制，测试中手动显示）
                document.getElementById('my-waveform').style.display = 'flex';
            """)

            # 启动语音引擎（使用 Chromium 假麦克风，会产生正弦波音频）
            page.evaluate("window.NevoMedia.startVoice().catch(e => console.error(e))")
            time.sleep(1.5)

            # 连续采样，检查波形有起伏且左右一致
            snapshots = []
            for _ in range(20):
                heights = page.evaluate("""
                    (() => {
                        const right = Array.from(document.querySelectorAll('.voice-user.is-me .waveform-bar .wf-bar'))
                            .map(b => parseFloat(b.style.height) || 0);
                        const left = Array.from(document.querySelectorAll('#my-waveform .wf-bar'))
                            .map(b => parseFloat(b.style.height) || 0);
                        return { right, left };
                    })()
                """)
                snapshots.append(heights)
                # 同一帧内左右必须一致
                assert heights['right'] == heights['left'], f"左下与右上波形不一致: {heights}"
                time.sleep(0.05)

            right_max = [max(s['right']) for s in snapshots]
            assert max(right_max) > 6, f"右上角波形高度没有超过基线: {snapshots}"
            assert max(right_max) - min(right_max) > 1, f"右上角波形完全没有波动: {snapshots}"

            browser.close()
            print("PASS: 端到端语音波形测试通过")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    run_test()
