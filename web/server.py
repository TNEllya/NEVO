#!/usr/bin/env python3
"""
NEVO Server Management Web Proxy v3
Bridges the TCP ControlServer (localhost:24432) to HTTP REST API + static file serving.
Adds: operation logging, batch kick, channel CRUD, server lifecycle control, SSE live logs.
Zero dependencies — uses only Python stdlib.
"""

import http.server
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.parse

HOST = "127.0.0.1"
WEB_PORT = 8090
TCP_PORT = 24433
WEB_ROOT = os.path.dirname(os.path.abspath(__file__))

_tcp_lock = threading.Lock()
_req_counter = 0
_metrics_cache = {}
_metrics_time = 0.0
_metrics_lock = threading.Lock()

# ---- Operation Log ----
_operation_log = []
_log_lock = threading.Lock()
MAX_LOG_ENTRIES = 200

# ---- SSE subscribers ----
_sse_subscribers = []
_sse_lock = threading.Lock()

# ---- Server process tracking ----
_server_exe_dir = None


def detect_server_exe():
    global _server_exe_dir
    candidates = [
        os.path.join(os.path.dirname(WEB_ROOT), "nevo_server.exe"),
        os.path.join(WEB_ROOT, "..", "nevo_server.exe"),
        os.path.join(os.path.dirname(os.path.dirname(WEB_ROOT)), "build", "server", "Release", "nevo_server.exe"),
    ]
    for c in candidates:
        c = os.path.normpath(c)
        if os.path.isfile(c):
            _server_exe_dir = os.path.dirname(c)
            return
    _server_exe_dir = os.path.dirname(WEB_ROOT)


def add_log(action, user, detail, status="success"):
    entry = {
        "timestamp": int(time.time() * 1000),
        "action": action,
        "user": user,
        "detail": detail,
        "status": status,
    }
    with _log_lock:
        _operation_log.append(entry)
        if len(_operation_log) > MAX_LOG_ENTRIES:
            _operation_log.pop(0)
    _broadcast_sse(entry)


def _broadcast_sse(entry):
    dead = []
    payload = json.dumps(entry, ensure_ascii=False) + "\n\n"
    with _sse_lock:
        subs = list(_sse_subscribers)
    for wfile in subs:
        try:
            wfile.write(b"data: " + payload.encode("utf-8"))
            if hasattr(wfile, "flush"):
                wfile.flush()
        except Exception:
            dead.append(wfile)
    if dead:
        with _sse_lock:
            for d in dead:
                if d in _sse_subscribers:
                    _sse_subscribers.remove(d)


# ---- Metrics ----

def collect_metrics() -> dict:
    global _metrics_cache, _metrics_time
    now = time.time()
    with _metrics_lock:
        if now - _metrics_time < 0.5:
            return dict(_metrics_cache)
        _metrics_time = now
    try:
        import psutil
        return _collect_psutil()
    except ImportError:
        pass
    try:
        return _collect_win32()
    except Exception:
        return {
            "cpu_percent": -1, "memory_mb": -1, "memory_percent": -1,
            "handles": -1, "threads": -1, "connections": -1,
            "error": "psutil not available",
        }


def _collect_psutil() -> dict:
    import psutil
    proc = None
    for p in psutil.process_iter(["pid", "name"]):
        try:
            if p.info["name"] and "nevo_server" in p.info["name"].lower():
                proc = p
                break
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    result = {
        "cpu_percent": round(psutil.cpu_percent(interval=0.2), 1),
        "memory_mb": 0, "memory_percent": 0.0, "handles": 0,
        "threads": 0, "connections": 0, "pid": 0,
        "disk_free_gb": 0, "disk_total_gb": 0,
    }
    if proc:
        try:
            mem = proc.memory_info()
            result["memory_mb"] = round(mem.rss / (1024 * 1024), 1)
            result["memory_percent"] = round(proc.memory_percent(), 1)
            result["handles"] = proc.num_handles() if hasattr(proc, "num_handles") else 0
            result["threads"] = proc.num_threads()
            result["connections"] = len(proc.connections())
            result["pid"] = proc.pid
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    usage = psutil.disk_usage(os.getcwd())
    result["disk_free_gb"] = round(usage.free / (1024 ** 3), 1)
    result["disk_total_gb"] = round(usage.total / (1024 ** 3), 1)
    global _metrics_cache
    _metrics_cache = result
    return result


def _collect_win32() -> dict:
    result = {"cpu_percent": -1, "memory_mb": 0, "memory_percent": 0.0,
              "handles": 0, "threads": 0, "connections": 0, "pid": 0,
              "disk_free_gb": 0, "disk_total_gb": 0}
    try:
        output = subprocess.check_output(
            'wmic cpu get loadpercentage /value', shell=True, timeout=3
        ).decode("utf-8", errors="replace")
        for line in output.splitlines():
            if "LoadPercentage=" in line:
                val = line.split("=")[-1].strip()
                if val.isdigit():
                    result["cpu_percent"] = int(val)
                break
    except Exception:
        pass
    try:
        output = subprocess.check_output(
            'tasklist /FI "IMAGENAME eq nevo_server.exe" /FO CSV /NH', shell=True, timeout=3
        ).decode("utf-8", errors="replace")
        for line in output.splitlines():
            parts = line.replace('"', "").split(",")
            if len(parts) >= 5:
                mem_kb = parts[4].strip().replace("K", "").replace(",", "")
                if mem_kb.isdigit():
                    result["memory_mb"] = round(int(mem_kb) / 1024, 1)
                    break
    except Exception:
        pass
    if result["memory_mb"] > 0:
        try:
            output = subprocess.check_output(
                'wmic computersystem get totalphysicalmemory /value', shell=True, timeout=3
            ).decode("utf-8", errors="replace")
            for line in output.splitlines():
                if "TotalPhysicalMemory=" in line:
                    val = line.split("=")[-1].strip()
                    if val.isdigit():
                        total_mb = int(val) / (1024 * 1024)
                        result["memory_percent"] = round(result["memory_mb"] / total_mb * 100, 1)
                    break
        except Exception:
            pass
    try:
        output = subprocess.check_output(
            'wmic logicaldisk where drivetype=3 get freespace,size /value', shell=True, timeout=3
        ).decode("utf-8", errors="replace")
        free = total = 0
        for line in output.splitlines():
            if "FreeSpace=" in line:
                val = line.split("=")[-1].strip()
                if val.isdigit():
                    free = max(free, int(val))
            if "Size=" in line:
                val = line.split("=")[-1].strip()
                if val.isdigit():
                    total = max(total, int(val))
        result["disk_free_gb"] = round(free / (1024 ** 3), 1)
        result["disk_total_gb"] = round(total / (1024 ** 3), 1)
    except Exception:
        pass
    global _metrics_cache
    _metrics_cache = result
    return result


# ---- TCP bridge ----

def send_tcp_command(command: str, params: dict | None = None) -> dict:
    global _req_counter
    with _tcp_lock:
        _req_counter += 1
        request = {"id": _req_counter, "command": command, "params": params or {}}
        payload = json.dumps(request, ensure_ascii=False) + "\n"
        try:
            sock = socket.create_connection((HOST, TCP_PORT), timeout=5.0)
            sock.sendall(payload.encode("utf-8"))
            sock.shutdown(socket.SHUT_WR)
            response_data = b""
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response_data += chunk
            sock.close()
            response_str = response_data.decode("utf-8").strip()
            if not response_str:
                return {"status": "error", "data": {"message": "Empty response from server"}}
            return json.loads(response_str)
        except ConnectionRefusedError:
            return {"status": "error", "data": {"message": "Server not running — cannot connect to ControlServer on port {}".format(TCP_PORT)}}
        except socket.timeout:
            return {"status": "error", "data": {"message": "Connection to ControlServer timed out"}}
        except Exception as e:
            return {"status": "error", "data": {"message": str(e)}}


def is_server_running():
    try:
        sock = socket.create_connection((HOST, TCP_PORT), timeout=1.0)
        sock.close()
        return True
    except Exception:
        return False


def start_server_process():
    if _server_exe_dir is None:
        detect_server_exe()
    exe = os.path.join(_server_exe_dir, "nevo_server.exe") if _server_exe_dir else None
    if exe and os.path.isfile(exe):
        subprocess.Popen([exe], cwd=_server_exe_dir, creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.5)
        for _ in range(10):
            if is_server_running():
                return True
            time.sleep(0.5)
        return False
    return False


def stop_server_process():
    resp = send_tcp_command("shutdown")
    time.sleep(0.5)
    if sys.platform == "win32":
        try:
            subprocess.run(["taskkill", "/F", "/IM", "nevo_server.exe"],
                           capture_output=True, timeout=5)
        except Exception:
            pass
    else:
        try:
            subprocess.run(["pkill", "-f", "nevo_server"], capture_output=True, timeout=5)
        except Exception:
            pass
    return resp


# ---- HTTP Handler ----

class WebHandler(http.server.SimpleHTTPRequestHandler):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_ROOT, **kwargs)

    def log_message(self, format, *args):
        timestamp = time.strftime("%H:%M:%S")
        sys.stdout.write("[{}] {}\n".format(timestamp, args[0]))
        sys.stdout.flush()

    def _send_json(self, data: dict, status: int = 200):
        body = json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self) -> dict | None:
        length = int(self.headers.get("Content-Length", 0))
        if length == 0:
            return None
        raw = self.rfile.read(length)
        return json.loads(raw.decode("utf-8"))

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    # ---- GET ----
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip("/")
        qs = urllib.parse.parse_qs(parsed.query)

        if path == "/api/status":
            self._send_json(send_tcp_command("get_status"))
        elif path == "/api/sessions":
            self._send_json(send_tcp_command("get_sessions"))
        elif path == "/api/channels":
            self._send_json(send_tcp_command("get_channels"))
        elif path == "/api/config":
            self._send_json(send_tcp_command("get_config"))
        elif path == "/api/health":
            self._send_json({"status": "ok", "timestamp": int(time.time() * 1000)})
        elif path == "/api/metrics":
            self._send_json({"status": "ok", "data": collect_metrics()})
        elif path == "/api/logs":
            limit = int(qs.get("limit", [50])[0])
            with _log_lock:
                logs = list(_operation_log)
            self._send_json({"status": "ok", "data": logs[-limit:]})
        elif path == "/api/server_check":
            running = is_server_running()
            self._send_json({"status": "ok", "data": {"running": running}})
        elif path == "/api/logs/stream":
            self._handle_sse()
        else:
            super().do_GET()

    def _handle_sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(b":ok\n\n")
        self.wfile.flush()
        with _sse_lock:
            _sse_subscribers.append(self.wfile)
        try:
            while True:
                time.sleep(30)
                self.wfile.write(b":heartbeat\n\n")
                self.wfile.flush()
        except Exception:
            pass
        finally:
            with _sse_lock:
                if self.wfile in _sse_subscribers:
                    _sse_subscribers.remove(self.wfile)

    # ---- POST ----
    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip("/")
        body = self._read_body() or {}
        admin_pwd = body.pop("_admin_password", None)

        if path == "/api/kick":
            result = send_tcp_command("kick_user", body)
            user = body.get("username", body.get("session_id", "unknown"))
            ok = result.get("status") == "ok"
            add_log("kick_user", "admin", "踢出用户: {}".format(user), "success" if ok else "error")
            self._send_json(result)

        elif path == "/api/kick_batch":
            sessions = body.get("sessions", [])
            results = []
            for sid in sessions:
                r = send_tcp_command("kick_user", {"session_id": sid})
                results.append({"session_id": sid, "status": r.get("status")})
            add_log("kick_batch", "admin", "批量踢出 {} 个用户".format(len(sessions)),
                    "success" if all(r["status"] == "ok" for r in results) else "partial")
            self._send_json({"status": "ok", "data": {"results": results}})

        elif path == "/api/disconnect_all":
            result = send_tcp_command("disconnect_all", body)
            add_log("disconnect_all", "admin", "断开所有连接")
            self._send_json(result)

        elif path == "/api/shutdown":
            result = send_tcp_command("shutdown", body)
            add_log("shutdown", "admin", "关闭服务器")
            self._send_json(result)

        elif path == "/api/restart":
            add_log("restart", "admin", "重启服务器")
            send_tcp_command("shutdown", {})
            time.sleep(1)
            success = start_server_process()
            self._send_json({
                "status": "ok" if success else "error",
                "data": {"message": "服务器已重启" if success else "服务器重启失败 — 请手动启动"}
            })

        elif path == "/api/start":
            success = start_server_process()
            add_log("start", "admin", "启动服务器", "success" if success else "error")
            self._send_json({
                "status": "ok" if success else "error",
                "data": {"message": "服务器已启动" if success else "服务器启动失败"}
            })

        elif path == "/api/config":
            result = send_tcp_command("set_config", body)
            add_log("config", "admin", "修改系统配置")
            self._send_json(result)

        elif path == "/api/ban":
            result = send_tcp_command("ban_user", body)
            add_log("ban", "admin", "封禁用户: {}".format(body.get("username", "unknown")))
            self._send_json(result)

        elif path == "/api/ssl":
            result = send_tcp_command("configure_ssl", body)
            add_log("ssl", "admin", "配置SSL")
            self._send_json(result)

        # ---- Channel CRUD ----
        elif path == "/api/channel/create":
            name = body.get("name", "").strip()
            parent_id = body.get("parent_id")
            permissions = body.get("permissions", {})
            if not name:
                self._send_json({"status": "error", "data": {"message": "频道名称不能为空"}}, 400)
                return
            result = send_tcp_command("create_channel", {
                "name": name, "parent_id": parent_id, "permissions": permissions
            })
            ok = result.get("status") == "ok" and result.get("data", {}).get("success", False)
            add_log("channel_create", "admin", "创建频道: {}".format(name),
                    "success" if ok else "error")
            self._send_json(result)

        elif path == "/api/channel/delete":
            channel_id = body.get("channel_id")
            if not channel_id:
                self._send_json({"status": "error", "data": {"message": "缺少 channel_id"}}, 400)
                return
            result = send_tcp_command("delete_channel", {"channel_id": channel_id})
            ok = result.get("status") == "ok" and result.get("data", {}).get("success", False)
            add_log("channel_delete", "admin", "删除频道: {}".format(channel_id),
                    "success" if ok else "error")
            self._send_json(result)

        elif path == "/api/channel/update":
            channel_id = body.get("channel_id")
            if not channel_id:
                self._send_json({"status": "error", "data": {"message": "缺少 channel_id"}}, 400)
                return
            result = send_tcp_command("update_channel", {
                "channel_id": channel_id,
                "name": body.get("name"),
                "parent_id": body.get("parent_id"),
                "permissions": body.get("permissions"),
                "sort_order": body.get("sort_order"),
            })
            ok = result.get("status") == "ok" and result.get("data", {}).get("success", False)
            add_log("channel_update", "admin", "更新频道: {}".format(channel_id),
                    "success" if ok else "error")
            self._send_json(result)

        elif path == "/api/channel/batch_delete":
            ids = body.get("channel_ids", [])
            results = []
            for cid in ids:
                r = send_tcp_command("delete_channel", {"channel_id": cid})
                results.append({"channel_id": cid, "status": r.get("status")})
            add_log("channel_batch_delete", "admin", "批量删除 {} 个频道".format(len(ids)))
            self._send_json({"status": "ok", "data": {"results": results}})

        elif path == "/api/channel/reorder":
            order = body.get("order", [])
            result = send_tcp_command("reorder_channels", {"order": order})
            add_log("channel_reorder", "admin", "频道排序调整")
            self._send_json(result)

        else:
            self._send_json({"status": "error", "data": {"message": "Unknown endpoint: {}".format(path)}}, 404)


def main():
    detect_server_exe()
    print("=" * 48)
    print("  NEVO Server Management Web Proxy v3")
    print("=" * 48)
    print("  Web UI:      http://{}:{}".format(HOST, WEB_PORT))
    print("  TCP Bridge:  {}:{} -> {}:{}".format(HOST, WEB_PORT, HOST, TCP_PORT))
    print("  Root:        {}".format(WEB_ROOT))
    if _server_exe_dir:
        print("  Server EXE:  {}".format(os.path.join(_server_exe_dir, "nevo_server.exe")))
    print("=" * 48)

    server = http.server.ThreadingHTTPServer((HOST, WEB_PORT), WebHandler)
    server.daemon_threads = True

    try:
        print("\n  Listening on http://{}:{} ...\n".format(HOST, WEB_PORT))
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.shutdown()


if __name__ == "__main__":
    main()
