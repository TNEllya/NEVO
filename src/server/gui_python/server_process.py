"""
NEVO server process manager + ControlServer IPC client.

Manages the NEVO server process lifecycle (start/stop) and connects
to the ControlServer via JSON-over-TCP protocol for management.
Protocol:
  Request:  {"id": N, "command": "cmd", "params": {...}}\\n
  Response: {"id": N, "status": "ok"|"error", "data": {...}}\\n
  Event:    {"event": "name", "data": {...}}\\n
"""

import json
import os
import socket
import subprocess
import sys
import threading
from typing import Callable, Optional

from PyQt5.QtCore import QSettings, QObject, pyqtSignal

# ControlServer IPC port (fixed at 24432 in C++ code)
IPC_PORT = 24432

# QSettings key for persisting server configuration
_SETTINGS_ORG = "NEVO"
_SETTINGS_APP = "ServerManager"


def _find_server_exe() -> str:
    """Auto-detect nevo_server.exe path.

    Search order:
      1. Same directory as this script (development mode)
      2. build/bin/ relative to project root (development mode)
      3. Same directory as the running executable (PyInstaller frozen mode)
      4. Parent directory of the _internal folder (PyInstaller onedir mode)
    """
    candidates = []

    if getattr(sys, "frozen", False):
        # PyInstaller frozen mode
        exe_dir = os.path.dirname(sys.executable)
        candidates.append(os.path.join(exe_dir, "nevo_server.exe"))
        # onedir layout: NEVO Server Manager.exe is one level above _internal/
        internal_dir = os.path.join(exe_dir, "_internal")
        if os.path.isdir(internal_dir):
            candidates.append(os.path.join(internal_dir, "nevo_server.exe"))
    else:
        # Development mode
        script_dir = os.path.dirname(os.path.abspath(__file__))
        # gui_python/ -> server/ -> src/ -> project root
        project_root = os.path.dirname(os.path.dirname(os.path.dirname(script_dir)))
        candidates.append(os.path.join(script_dir, "nevo_server.exe"))
        candidates.append(os.path.join(project_root, "build", "bin", "nevo_server.exe"))
        candidates.append(os.path.join(project_root, "nevo_server.exe"))

    for path in candidates:
        if os.path.isfile(path):
            return path

    return ""


class _ServerSignals(QObject):
    server_running_changed = pyqtSignal(bool)
    ipc_connected_changed = pyqtSignal(bool)
    log_message = pyqtSignal(str)


class ServerProcess:
    """Manages the NEVO server process lifecycle and IPC connection."""

    def __init__(self):
        # Load persisted settings
        self._settings = QSettings(_SETTINGS_ORG, _SETTINGS_APP)

        # Thread-safe signal helper for cross-thread UI updates
        self._signals = _ServerSignals()

        # Server process
        self._process: Optional[subprocess.Popen] = None
        self._exe_path: str = self._settings.value("exe_path", _find_server_exe(), type=str)
        self._tcp_port: int = int(self._settings.value("tcp_port", 24430, type=int))
        self._udp_port: int = int(self._settings.value("udp_port", 24431, type=int))
        self._db_path: str = self._settings.value("db_path", "nevo_server.db", type=str)
        self._log_level: str = self._settings.value("log_level", "info", type=str)
        self._config_path: str = self._settings.value("config_path", "", type=str)

        # IPC client state
        self._ipc_sock: Optional[socket.socket] = None
        self._request_id = 0
        self._pending: dict[int, tuple[Callable, Callable]] = {}
        self._event_handlers: dict[str, list[Callable]] = {}
        self._lock = threading.Lock()
        self._ipc_connected = False
        self._ipc_thread: Optional[threading.Thread] = None
        self._ipc_buffer = ""

        # Flag to prevent double cleanup between stop_server() and _monitor_process()
        self._stopping = False

        # Stdout reader thread
        self._stdout_thread: Optional[threading.Thread] = None

    # ---- Properties ----

    @property
    def exe_path(self) -> str:
        return self._exe_path

    @exe_path.setter
    def exe_path(self, value: str):
        self._exe_path = value
        self._settings.setValue("exe_path", value)

    @property
    def tcp_port(self) -> int:
        return self._tcp_port

    @tcp_port.setter
    def tcp_port(self, value: int):
        self._tcp_port = value
        self._settings.setValue("tcp_port", value)

    @property
    def udp_port(self) -> int:
        return self._udp_port

    @udp_port.setter
    def udp_port(self, value: int):
        self._udp_port = value
        self._settings.setValue("udp_port", value)

    @property
    def db_path(self) -> str:
        return self._db_path

    @db_path.setter
    def db_path(self, value: str):
        self._db_path = value
        self._settings.setValue("db_path", value)

    @property
    def log_level(self) -> str:
        return self._log_level

    @log_level.setter
    def log_level(self, value: str):
        self._log_level = value
        self._settings.setValue("log_level", value)

    @property
    def config_path(self) -> str:
        return self._config_path

    @config_path.setter
    def config_path(self, value: str):
        self._config_path = value
        self._settings.setValue("config_path", value)

    @property
    def server_running(self) -> bool:
        return self._process is not None and self._process.poll() is None

    @property
    def ipc_connected(self) -> bool:
        return self._ipc_connected

    # ---- Callback setters ----

    def set_on_server_running_changed(self, callback: Callable[[bool], None]):
        self._signals.server_running_changed.connect(callback)

    def set_on_ipc_connected_changed(self, callback: Callable[[bool], None]):
        self._signals.ipc_connected_changed.connect(callback)

    def set_on_log(self, callback: Callable[[str], None]):
        self._signals.log_message.connect(callback)

    def _notify_server_running(self, running: bool):
        self._signals.server_running_changed.emit(running)

    def _notify_ipc_connected(self, connected: bool):
        self._ipc_connected = connected
        self._signals.ipc_connected_changed.emit(connected)

    def _log(self, msg: str):
        self._signals.log_message.emit(msg)

    # ---- Server process management ----

    def start_server(self) -> bool:
        """Start the NEVO server process. Returns True on success."""
        if self.server_running:
            self._log("Server is already running.")
            return False

        if not self._exe_path or not os.path.isfile(self._exe_path):
            self._log(f"Server executable not found: {self._exe_path}")
            return False

        cmd = [self._exe_path]
        cmd.extend(["--tcp-port", str(self._tcp_port)])
        cmd.extend(["--udp-port", str(self._udp_port)])
        cmd.extend(["--db", self._db_path])
        cmd.extend(["--log-level", self._log_level])
        if self._config_path:
            cmd.extend(["--config", self._config_path])

        try:
            work_dir = os.path.dirname(os.path.abspath(self._exe_path))
            self._process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                cwd=work_dir,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )
            self._log(f"Server started (PID: {self._process.pid})")
            self._notify_server_running(True)

            # Start stdout reader thread
            self._stdout_thread = threading.Thread(
                target=self._read_stdout, daemon=True
            )
            self._stdout_thread.start()

            # Start monitoring thread to detect process exit
            threading.Thread(target=self._monitor_process, daemon=True).start()

            # Try to connect IPC after a brief delay
            threading.Thread(target=self._auto_connect_ipc, daemon=True).start()

            return True
        except Exception as e:
            self._log(f"Failed to start server: {e}")
            return False

    def stop_server(self):
        """Stop the NEVO server process."""
        if not self.server_running:
            return

        self._stopping = True

        # Try graceful shutdown via IPC first
        if self._ipc_connected:
            try:
                self.send_command("shutdown")
                self._log("Sent shutdown command via IPC.")
                # Give it a moment to shut down gracefully
                try:
                    self._process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
            except Exception:
                pass

        # Force kill if still running
        if self.server_running:
            try:
                self._process.terminate()
                try:
                    self._process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self._process.kill()
                self._log("Server process terminated.")
            except Exception as e:
                self._log(f"Error terminating server: {e}")

        self._process = None
        self._disconnect_ipc()
        self._notify_server_running(False)
        self._stopping = False

    def _read_stdout(self):
        """Read server process stdout in background thread."""
        if not self._process or not self._process.stdout:
            return
        try:
            for line in iter(self._process.stdout.readline, b""):
                try:
                    text = line.decode("utf-8", errors="replace").rstrip()
                    if text:
                        self._log(text)
                except Exception:
                    pass
        except Exception:
            pass

    def _monitor_process(self):
        """Monitor the server process and detect when it exits."""
        if self._process:
            self._process.wait()
            self._log("Server process has exited.")
            if not self._stopping:
                self._disconnect_ipc()
                self._process = None
                self._notify_server_running(False)

    def _auto_connect_ipc(self):
        """Try to connect IPC to ControlServer after server starts."""
        import time
        for attempt in range(10):
            if not self.server_running:
                return
            time.sleep(0.5)
            if self._connect_ipc():
                return
        self._log("Could not connect to ControlServer IPC after server start.")

    # ---- IPC connection ----

    def _connect_ipc(self) -> bool:
        """Connect to the ControlServer IPC. Returns True on success."""
        try:
            self._ipc_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._ipc_sock.settimeout(3)
            self._ipc_sock.connect(("127.0.0.1", IPC_PORT))
            self._ipc_sock.settimeout(None)
            self._notify_ipc_connected(True)
            self._log("Connected to ControlServer IPC.")

            self._ipc_thread = threading.Thread(target=self._ipc_recv_loop, daemon=True)
            self._ipc_thread.start()
            return True
        except Exception:
            self._notify_ipc_connected(False)
            return False

    def _disconnect_ipc(self):
        """Disconnect from the ControlServer IPC."""
        was_connected = self._ipc_connected
        self._ipc_connected = False
        if self._ipc_sock:
            try:
                self._ipc_sock.close()
            except Exception:
                pass
            self._ipc_sock = None
        if was_connected:
            self._notify_ipc_connected(False)

    # ---- IPC event system ----

    def on_event(self, event_name: str, handler: Callable[[dict], None]):
        if event_name not in self._event_handlers:
            self._event_handlers[event_name] = []
        self._event_handlers[event_name].append(handler)

    # ---- IPC command sending ----

    def send_command(self, command: str, params: dict = None, timeout: float = 5.0) -> dict:
        """Send a command via IPC and wait for the response."""
        import queue
        q = queue.Queue()

        def on_response(resp):
            q.put(("ok", resp))

        def on_error(err):
            q.put(("error", err))

        with self._lock:
            self._request_id += 1
            req_id = self._request_id
            self._pending[req_id] = (on_response, on_error)

        request = {"id": req_id, "command": command}
        if params:
            request["params"] = params

        try:
            data = json.dumps(request, ensure_ascii=False) + "\n"
            self._ipc_sock.sendall(data.encode("utf-8"))
        except Exception as e:
            with self._lock:
                self._pending.pop(req_id, None)
            raise ConnectionError(f"Send failed: {e}")

        try:
            status, result = q.get(timeout=timeout)
            if status == "error":
                raise RuntimeError(result)
            return result
        except queue.Empty:
            with self._lock:
                self._pending.pop(req_id, None)
            raise TimeoutError(f"Command '{command}' timed out")

    # ---- High-level API (via IPC) ----

    def get_status(self) -> dict:
        return self.send_command("get_status")

    def get_sessions(self) -> list:
        resp = self.send_command("get_sessions")
        return resp.get("sessions", [])

    def get_channels(self) -> list:
        resp = self.send_command("get_channels")
        return resp.get("channels", [])

    def get_config(self) -> dict:
        return self.send_command("get_config")

    def kick_user(self, session_id: int) -> dict:
        return self.send_command("kick_user", {"session_id": session_id})

    def ban_user(self, user_id: int = -1, ip_address: str = "",
                 reason: str = "Banned via control API",
                 expires_at: int = 0) -> dict:
        params = {"reason": reason, "expires_at": expires_at}
        if user_id >= 0:
            params["user_id"] = user_id
        if ip_address:
            params["ip_address"] = ip_address
        return self.send_command("ban_user", params)

    def disconnect_all(self) -> dict:
        return self.send_command("disconnect_all")

    def shutdown(self) -> dict:
        return self.send_command("shutdown")

    def set_admin_password(self, password: str):
        return self.send_command("set_admin_password", {"password": password})

    def set_config(self, **kwargs) -> dict:
        return self.send_command("set_config", kwargs)

    def configure_ssl(self, enabled: bool = False, cert_file: str = "",
                      key_file: str = "", ca_file: str = "") -> dict:
        params = {"enabled": enabled}
        if cert_file:
            params["cert_file"] = cert_file
        if key_file:
            params["key_file"] = key_file
        if ca_file:
            params["ca_file"] = ca_file
        return self.send_command("configure_ssl", params)

    # ---- IPC internal ----

    def _ipc_recv_loop(self):
        while self._ipc_connected and self._ipc_sock:
            try:
                data = self._ipc_sock.recv(8192)
                if not data:
                    break
                self._ipc_buffer += data.decode("utf-8", errors="replace")

                while "\n" in self._ipc_buffer:
                    line, self._ipc_buffer = self._ipc_buffer.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        msg = json.loads(line)
                    except json.JSONDecodeError:
                        continue

                    if "event" in msg:
                        self._dispatch_event(msg["event"], msg.get("data", {}))
                    elif "id" in msg:
                        self._dispatch_response(msg)

            except Exception:
                break

        if self._ipc_connected:
            self._ipc_connected = False
            self._signals.ipc_connected_changed.emit(False)

    def _dispatch_response(self, msg: dict):
        req_id = msg.get("id")
        with self._lock:
            callbacks = self._pending.pop(req_id, None)

        if not callbacks:
            return

        resolve, reject = callbacks
        status = msg.get("status", "error")
        data = msg.get("data", {})

        if status == "ok":
            resolve(data)
        else:
            error_msg = data.get("message", "Unknown error") if isinstance(data, dict) else str(data)
            reject(error_msg)

    def _dispatch_event(self, event_name: str, data: dict):
        handlers = self._event_handlers.get(event_name, [])
        for handler in handlers:
            try:
                handler(data)
            except Exception:
                pass
