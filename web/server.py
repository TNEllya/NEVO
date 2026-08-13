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

HOST = os.environ.get("NEVO_WEB_HOST", "127.0.0.1")
WEB_PORT = int(os.environ.get("NEVO_WEB_PORT", "8090"))
TCP_PORT = int(os.environ.get("NEVO_CONTROL_PORT", "24433"))
WEB_ROOT = os.environ.get("NEVO_WEB_ROOT", os.path.dirname(os.path.abspath(__file__)))

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

# ---- Admin auth token (obtained from ControlServer via admin_login) ----
_admin_token = None
_token_lock = threading.Lock()

# Commands that mutate server state — require auth token on the ControlServer side
SENSITIVE_COMMANDS = {
    "kick_user", "disconnect_all", "shutdown", "ban_user",
    "set_config", "configure_ssl", "create_channel", "delete_channel",
    "update_channel", "reorder_channels", "set_admin_password",
}

# Maximum accepted request body size (defense against unbounded reads)
MAX_BODY_BYTES = 1024 * 1024

# Allowed Host values (DNS-rebinding defense: only loopback hosts may use this proxy)
ALLOWED_HOSTS = {"127.0.0.1", "localhost", "::1", "[::1]"}


def get_admin_token():
    with _token_lock:
        return _admin_token


def set_admin_token(token):
    global _admin_token
    with _token_lock:
        _admin_token = token

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
    if sys.platform == "win32":
        try:
            return _collect_win32()
        except Exception:
            pass
    try:
        return _collect_procfs()
    except Exception:
        pass
    return {
        "cpu_percent": -1, "memory_mb": -1, "memory_percent": -1,
        "handles": -1, "threads": -1, "connections": -1,
        "error": "metrics backend not available",
    }


def _find_nevo_process():
    """Locate the nevo_server process by executable name or cmdline."""
    import psutil
    for p in psutil.process_iter(["pid", "name", "cmdline"]):
        try:
            name = (p.info.get("name") or "").lower()
            cmdline = p.info.get("cmdline") or []
            cmdline_str = " ".join(cmdline).lower()
            if "nevo_server" in name or "/usr/local/bin/nevo_server" in cmdline_str:
                return p
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return None


def _collect_psutil() -> dict:
    import psutil
    proc = _find_nevo_process()
    result = {
        "cpu_percent": round(psutil.cpu_percent(interval=0.2), 1),
        "memory_mb": 0, "memory_total_gb": 0.0, "memory_percent": 0.0,
        "handles": 0, "threads": 0, "connections": 0, "pid": 0,
        "disk_free_gb": 0, "disk_total_gb": 0,
    }
    if proc:
        try:
            mem = proc.memory_info()
            result["memory_mb"] = round(mem.rss / (1024 * 1024), 1)
            try:
                result["memory_percent"] = round(proc.memory_percent(), 1)
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
            # num_handles is Windows-only; on Linux count open file descriptors.
            if hasattr(proc, "num_handles"):
                try:
                    result["handles"] = proc.num_handles()
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
            else:
                try:
                    result["handles"] = proc.num_fds()
                except (psutil.NoSuchProcess, psutil.AccessDenied, AttributeError):
                    pass
            result["threads"] = proc.num_threads()
            result["connections"] = len(proc.connections())
            result["pid"] = proc.pid
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    try:
        vm = psutil.virtual_memory()
        result["memory_total_gb"] = round(vm.total / (1024 ** 3), 2)
    except Exception:
        pass
    usage = psutil.disk_usage(os.getcwd())
    result["disk_free_gb"] = round(usage.free / (1024 ** 3), 2)
    result["disk_total_gb"] = round(usage.total / (1024 ** 3), 2)
    global _metrics_cache
    _metrics_cache = result
    return result


def _collect_procfs() -> dict:
    """Fallback metrics collector using Linux /proc filesystem."""
    result = {
        "cpu_percent": -1, "memory_mb": -1, "memory_total_gb": 0.0,
        "memory_percent": -1, "handles": -1, "threads": -1,
        "connections": -1, "pid": 0,
        "disk_free_gb": 0, "disk_total_gb": 0,
    }

    # Find nevo_server PID
    nevo_pid = 0
    try:
        for entry in os.listdir("/proc"):
            if not entry.isdigit():
                continue
            try:
                with open(f"/proc/{entry}/cmdline", "rb") as f:
                    cmdline = f.read().replace(b"\x00", b" ").decode("utf-8", errors="replace")
                if "nevo_server" in cmdline:
                    nevo_pid = int(entry)
                    break
            except (OSError, ValueError):
                continue
    except OSError:
        pass

    if nevo_pid:
        result["pid"] = nevo_pid
        try:
            with open(f"/proc/{nevo_pid}/status") as f:
                status = f.read()
            for line in status.splitlines():
                if line.startswith("VmRSS:"):
                    parts = line.split()
                    if len(parts) >= 2:
                        result["memory_mb"] = round(int(parts[1]) / 1024.0, 1)
                elif line.startswith("Threads:"):
                    parts = line.split()
                    if len(parts) >= 2:
                        result["threads"] = int(parts[1])
            # Count open file descriptors as handles equivalent
            try:
                result["handles"] = len(os.listdir(f"/proc/{nevo_pid}/fd"))
            except OSError:
                pass
        except OSError:
            pass

    # CPU load average (1-min) as a rough CPU utilisation proxy
    try:
        with open("/proc/loadavg") as f:
            loadavg = f.read().split()
            if loadavg:
                result["cpu_percent"] = round(float(loadavg[0]) * 100, 1)
    except (OSError, ValueError):
        pass

    # Total system memory from /proc/meminfo
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemTotal:"):
                    parts = line.split()
                    if len(parts) >= 2:
                        result["memory_total_gb"] = round(int(parts[1]) / (1024 * 1024), 2)
                    break
    except (OSError, ValueError):
        pass

    # Disk usage for current working directory
    try:
        st = os.statvfs(os.getcwd())
        total = st.f_blocks * st.f_frsize
        free = st.f_bavail * st.f_frsize
        result["disk_free_gb"] = round(free / (1024 ** 3), 2)
        result["disk_total_gb"] = round(total / (1024 ** 3), 2)
    except OSError:
        pass

    global _metrics_cache
    _metrics_cache = result
    return result


def _collect_win32() -> dict:
    result = {"cpu_percent": -1, "memory_mb": 0, "memory_total_gb": 0.0,
              "memory_percent": 0.0, "handles": 0, "threads": 0,
              "connections": 0, "pid": 0,
              "disk_free_gb": 0, "disk_total_gb": 0}
    try:
        # 参数列表方式调用（shell=False，杜绝命令注入面）
        output = subprocess.check_output(
            ["wmic", "cpu", "get", "loadpercentage", "/value"], timeout=3
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
            ["tasklist", "/FI", "IMAGENAME eq nevo_server.exe", "/FO", "CSV", "/NH"],
            timeout=3,
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
                ["wmic", "computersystem", "get", "totalphysicalmemory", "/value"],
                timeout=3,
            ).decode("utf-8", errors="replace")
            for line in output.splitlines():
                if "TotalPhysicalMemory=" in line:
                    val = line.split("=")[-1].strip()
                    if val.isdigit():
                        total_gb = int(val) / (1024 ** 3)
                        result["memory_total_gb"] = round(total_gb, 2)
                        result["memory_percent"] = round(result["memory_mb"] / (total_gb * 1024) * 100, 1)
                    break
        except Exception:
            pass
    try:
        output = subprocess.check_output(
            ["wmic", "logicaldisk", "where", "drivetype=3",
             "get", "freespace,size", "/value"],
            timeout=3,
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
        result["disk_free_gb"] = round(free / (1024 ** 3), 2)
        result["disk_total_gb"] = round(total / (1024 ** 3), 2)
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
        params = dict(params or {})
        # 敏感命令自动附加管理认证令牌（ControlServer 侧 fail-closed 校验）
        if command in SENSITIVE_COMMANDS:
            token = get_admin_token()
            if token:
                params["auth_token"] = token
        request = {"id": _req_counter, "command": command, "params": params}
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

    # ---- 安全防护：DNS rebinding / 跨站请求 ----

    def _host_allowed(self) -> bool:
        """只允许 loopback 主机访问（防 DNS rebinding 攻击）。"""
        host = self.headers.get("Host", "")
        hostname = host.split(":")[0] if host else ""
        return hostname in ALLOWED_HOSTS

    def _reject_foreign_host(self) -> bool:
        """Host 非本地时拒绝请求。返回 True 表示已拒绝。"""
        if not self._host_allowed():
            self._send_json({"status": "error", "data": {"message": "Forbidden host"}}, 403)
            return True
        return False

    def _require_auth(self) -> bool:
        """校验 Bearer 令牌。未通过时返回 False 并已发送 401 响应。"""
        token = get_admin_token()
        if not token:
            self._send_json({"status": "error", "data": {"message": "未登录管理面，请先 /api/login"}}, 401)
            return False
        auth = self.headers.get("Authorization", "")
        expected = "Bearer " + token
        if not auth or auth != expected:
            self._send_json({"status": "error", "data": {"message": "AUTH_REQUIRED: 无效的管理令牌"}}, 401)
            return False
        return True

    def _send_json(self, data: dict, status: int = 200):
        body = json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        # 不发送 CORS 头：管理面仅允许同源访问（防任意网页跨站调用本机管理 API）
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self) -> dict | None:
        length = int(self.headers.get("Content-Length", 0))
        if length == 0:
            return None
        if length > MAX_BODY_BYTES:
            self._send_json({"status": "error", "data": {"message": "Request body too large"}}, 413)
            return None
        raw = self.rfile.read(length)
        try:
            return json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            self._send_json({"status": "error", "data": {"message": "Invalid JSON body"}}, 400)
            return None

    def do_OPTIONS(self):
        if self._reject_foreign_host():
            return
        self.send_response(204)
        # 不发送 CORS 头（同源访问）
        self.end_headers()

    # ---- GET ----
    def do_GET(self):
        if self._reject_foreign_host():
            return
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
        # 不发送 CORS 头（同源访问）
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
        if self._reject_foreign_host():
            return
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip("/")
        body = self._read_body()
        if body is None:
            body = {}

        # ---- 管理面登录（换取管理认证令牌） ----
        if path == "/api/login":
            password = body.get("password", "")
            result = send_tcp_command("admin_login", {"password": password})
            if result.get("status") == "ok" and result.get("data", {}).get("authenticated"):
                set_admin_token(result["data"].get("auth_token"))
                add_log("admin_login", "admin", "管理面登录成功")
                self._send_json(result)
            else:
                add_log("admin_login", "admin", "管理面登录失败", "error")
                self._send_json({"status": "error", "data": {
                    "authenticated": False,
                    "message": result.get("data", {}).get("message", "密码错误")
                }}, 401)
            return

        # ---- 其余 POST 均为敏感操作：要求管理认证 ----
        if not self._require_auth():
            return

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
            # 修改管理员密码后 ControlServer 会轮换令牌，同步更新本地令牌
            new_token = result.get("data", {}).get("auth_token")
            if new_token:
                set_admin_token(new_token)
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
