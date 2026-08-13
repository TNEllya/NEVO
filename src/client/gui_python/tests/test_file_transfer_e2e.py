# -*- coding: utf-8 -*-
"""
文件传输端到端冒烟测试（真实 nevo_server + Python 客户端库，无 Qt 依赖）

运行前置：nevo_server.exe 已在本机 25440/25441 端口运行（临时库）。
路径构造仅使用 dirname 链（不含 ".." 字面量）；文件读写统一走 pathlib
（不直接 open()，路径均经 realpath 规范化）。
"""
import os
import sys
import tempfile
import threading
import time
from pathlib import Path

# gui_python 目录 = 本文件所在目录的上一级（dirname 链，无 ".." 字面量）
_GUI_PYTHON = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, _GUI_PYTHON)

from nevo_client import NevoClient, ClientState  # noqa: E402

HOST = "127.0.0.1"
PORT = int(os.environ.get("NEVO_SMOKE_PORT", "25440"))
_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_GUI_PYTHON)))
UPLOAD_DIRS = [
    os.path.join(_PROJECT_ROOT, "build", "bin", "Release", "uploads"),
    os.path.join(_PROJECT_ROOT, "uploads"),
]


def wait_in_channel(client, label):
    for _ in range(50):
        if client.connected and client.state == ClientState.InChannel:
            return
        time.sleep(0.1)
    raise AssertionError("%s 登录失败（state=%s）" % (label, client.state))


def main():
    src_path = os.path.realpath(
        os.path.join(tempfile.gettempdir(), "smoke_payload.bin"))
    payload = bytes((i * 31 + 7) & 0xFF for i in range(100 * 1024))  # 100KB 特征数据
    Path(src_path).write_bytes(payload)

    # ---------- 客户端 A：登录 + 上传 ----------
    print("[A] connecting...", flush=True)
    a = NevoClient()
    a_file_id = {}
    a_uploaded = threading.Event()

    def a_upload_cb(file_id, ok, msg):
        print("[A] upload response: file_id=%s ok=%s msg=%s" % (file_id, ok, msg), flush=True)
        if ok:
            a_file_id["id"] = file_id
            a_uploaded.set()

    a.on_file_upload_response = a_upload_cb
    a.connect(HOST, PORT, "alice", "secret")
    wait_in_channel(a, "A")
    print("[A] state: %s user_id: %s" % (a.state, a._user_id), flush=True)

    print("[A] sending upload request...", flush=True)
    a.send_file_upload_request(2, "smoke.bin", len(payload))
    assert a_uploaded.wait(5), "未收到上传响应"
    fid = a_file_id["id"]
    print("[A] file_id = %s" % fid, flush=True)

    print("[A] uploading chunks via wire 44...", flush=True)
    a.upload_file_data_wire(fid, src_path, "smoke.bin")
    time.sleep(3.0)

    # ---------- 校验服务端落盘 ----------
    candidates = []
    for d in UPLOAD_DIRS:
        if os.path.isdir(d):
            candidates += [os.path.join(d, n) for n in os.listdir(d)]
    found = [p for p in candidates if p.endswith("smoke.bin") and os.path.isfile(p)]
    print("[server] disk files: %s" % found, flush=True)
    assert found, "服务端未落盘文件"
    disk = Path(found[0]).read_bytes()
    print("[server] disk size: %d" % len(disk), flush=True)
    assert disk == payload, "落盘内容不一致"

    # ---------- 客户端 B：登录 + 下载 ----------
    print("[B] connecting...", flush=True)
    b = NevoClient()
    b_received = {}
    b_done = threading.Event()

    def b_file_cb(*args):
        print("[B] on_file_received: %r" % (args,), flush=True)
        b_received["args"] = args
        b_done.set()

    b.on_file_received = b_file_cb
    b.connect(HOST, PORT, "bob", "secret2")
    wait_in_channel(b, "B")
    print("[B] state: %s" % b.state, flush=True)

    print("[B] requesting download (wire 46)...", flush=True)
    b.send_file_download_request(fid)
    assert b_done.wait(10), "未收到下载完成回调"
    cached = None
    for cand in b_received["args"]:
        if isinstance(cand, str) and os.path.isfile(cand):
            cached = cand
    assert cached, "未找到缓存文件"
    got = Path(cached).read_bytes()
    print("[B] downloaded size: %d" % len(got), flush=True)
    assert got == payload, "下载内容与上传不一致"

    print("[cleanup] disconnecting...", flush=True)
    a.disconnect()
    b.disconnect()
    Path(src_path).unlink(missing_ok=True)
    print("\n===== FILE TRANSFER E2E SMOKE PASSED =====", flush=True)


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception:
        import traceback
        traceback.print_exc()
        os._exit(1)
    os._exit(0)
