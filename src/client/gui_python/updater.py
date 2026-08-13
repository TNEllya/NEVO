import hashlib
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Callable, Optional

import requests

import i18n

def _tr(text: str) -> str:
    return i18n._translate(text)

logger = logging.getLogger("NEVO.Updater")

_current_version_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "version.txt")


if sys.platform == "darwin":
    _EXTRACT_PREFIX = "NEVO.app"
    _BUNDLE_CANDIDATES = [
        os.path.expanduser("~/Applications/NEVO.app"),
        "/Applications/NEVO.app",
    ]
else:
    _EXTRACT_PREFIX = "NEVO"
    _BUNDLE_CANDIDATES = []


def _get_platform_asset_keywords():
    if sys.platform == "darwin":
        return ["mac", "darwin", "osx"]
    elif sys.platform == "win32":
        return ["win", "windows"]
    else:
        return ["linux", "ubuntu"]

GITHUB_OWNER = "TNEllya"
GITHUB_REPO = "NEVO"
GITHUB_API_BASE = "https://api.github.com"
UPDATE_CHECK_INTERVAL = 3600
DOWNLOAD_CHUNK_SIZE = 65536
MAX_RETRIES = 5
RETRY_DELAY = 3
REQUEST_TIMEOUT = 30
API_CACHE_TTL = 300  # 5 分钟内重复检查直接走缓存


def _get_github_headers(token: Optional[str] = None) -> dict:
    """构造 GitHub API 请求头，支持 GITHUB_TOKEN 提升速率限制。"""
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "NEVO-Client/Updater",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    effective_token = token or os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if effective_token:
        headers["Authorization"] = f"Bearer {effective_token}"
    return headers


def _settings():
    """获取 QSettings 实例（延迟导入，避免 Qt 未初始化时失败）。"""
    from PyQt5.QtCore import QSettings
    return QSettings("NEVO", "Client")


def load_github_token() -> str:
    """从 QSettings 读取用户绑定的 GitHub token。"""
    try:
        return _settings().value("github_token", "")
    except Exception:
        return ""


def save_github_token(token: str):
    """保存 GitHub token 到 QSettings。"""
    try:
        _settings().setValue("github_token", token)
    except Exception as e:
        logger.warning("Failed to save GitHub token: %s", e)


class _ReleaseCache:
    """简单的内存缓存，用于降低 GitHub API 调用频率。"""
    _data: Optional[dict] = None
    _timestamp: float = 0.0

    @classmethod
    def get(cls) -> Optional[dict]:
        if cls._data and (time.time() - cls._timestamp) < API_CACHE_TTL:
            return cls._data
        return None

    @classmethod
    def set(cls, data: dict):
        cls._data = data
        cls._timestamp = time.time()

    @classmethod
    def clear(cls):
        cls._data = None
        cls._timestamp = 0.0


class VersionInfo:
    def __init__(self, version: str, changelog: str = "",
                 download_url: str = "", sha256: str = "",
                 file_size: int = 0, release_name: str = "",
                 published_at: str = ""):
        self.version = version
        self.changelog = changelog
        self.download_url = download_url
        self.sha256 = sha256
        self.file_size = file_size
        self.release_name = release_name
        self.published_at = published_at

    @staticmethod
    def parse(version_str: str) -> tuple:
        """从任意版本字符串中提取语义化版本号 (major, minor, patch)。

        支持 "v0.1.0"、"B_V0.01"、"1.2.3-beta" 等非标准格式。
        """
        text = version_str.strip().lstrip("vV")
        match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", text)
        if match:
            major = int(match.group(1))
            minor = int(match.group(2))
            patch = int(match.group(3)) if match.group(3) else 0
            return (major, minor, patch)
        return (0, 0, 0)

    def is_newer_than(self, current_version: str) -> bool:
        return self.parse(self.version) > self.parse(current_version)


class UpdateState:
    IDLE = "idle"
    CHECKING = "checking"
    DOWNLOAD_AVAILABLE = "download_available"
    DOWNLOADING = "downloading"
    VERIFYING = "verifying"
    READY_TO_INSTALL = "ready_to_install"
    INSTALLING = "installing"
    ERROR = "error"


class UpdateError(Exception):
    pass


class CheckError(UpdateError):
    pass


class DownloadError(UpdateError):
    pass


class VerifyError(UpdateError):
    pass


class InstallError(UpdateError):
    pass


def get_current_version() -> str:
    version_file = Path(__file__).parent / "version.txt"
    if version_file.exists():
        return version_file.read_text(encoding="utf-8").strip()
    return "0.0.0"


def get_update_dir() -> Path:
    if getattr(sys, "frozen", False):
        base = Path(sys.executable).parent
    else:
        base = Path(tempfile.gettempdir())
    update_dir = base / ".nevo_update"
    update_dir.mkdir(parents=True, exist_ok=True)
    return update_dir


def get_update_log_path() -> Path:
    return get_update_dir() / "update_log.json"


def log_update_event(event_type: str, details: dict):
    log_path = get_update_log_path()
    entries = []
    if log_path.exists():
        try:
            with open(log_path, "r", encoding="utf-8") as f:
                entries = json.load(f)
        except (json.JSONDecodeError, IOError):
            entries = []

    entries.append({
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "event": event_type,
        "details": details,
    })

    if len(entries) > 200:
        entries = entries[-200:]

    try:
        with open(log_path, "w", encoding="utf-8") as f:
            json.dump(entries, f, indent=2, ensure_ascii=False)
    except IOError as e:
        logger.warning("Failed to write update log: %s", e)


def sha256_file(filepath: Path) -> str:
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while True:
            chunk = f.read(DOWNLOAD_CHUNK_SIZE)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


class Updater:
    def __init__(self):
        self._state = UpdateState.IDLE
        self._current_version = get_current_version()
        self._latest_info: Optional[VersionInfo] = None
        self._download_progress = 0.0
        self._download_speed = 0.0
        self._error_message = ""
        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._check_timer: Optional[threading.Timer] = None
        self._on_state_changed: Optional[Callable] = None
        self._on_progress: Optional[Callable] = None
        self._downloaded_size = 0
        self._total_size = 0
        self._download_file_path: Optional[Path] = None
        self._github_token: str = load_github_token()

    def set_github_token(self, token: str):
        """设置用户绑定的 GitHub token，会立即清除 API 缓存。"""
        self._github_token = token.strip()
        save_github_token(self._github_token)
        _ReleaseCache.clear()
        log_update_event("github_token_set", {"has_token": bool(self._github_token)})

    @property
    def state(self) -> str:
        return self._state

    @property
    def current_version(self) -> str:
        return self._current_version

    @property
    def latest_info(self) -> Optional[VersionInfo]:
        return self._latest_info

    @property
    def download_progress(self) -> float:
        return self._download_progress

    @property
    def download_speed(self) -> float:
        return self._download_speed

    @property
    def error_message(self) -> str:
        return self._error_message

    def set_callbacks(self, on_state_changed: Callable = None,
                      on_progress: Callable = None):
        self._on_state_changed = on_state_changed
        self._on_progress = on_progress

    def _set_state(self, new_state: str):
        old = self._state
        self._state = new_state
        logger.info("Update state: %s -> %s", old, new_state)
        if self._on_state_changed:
            try:
                self._on_state_changed(old, new_state)
            except Exception as e:
                logger.warning("State callback error: %s", e)

    def _notify_progress(self):
        if self._on_progress:
            try:
                self._on_progress(self._download_progress, self._download_speed,
                                  self._downloaded_size, self._total_size)
            except Exception as e:
                logger.warning("Progress callback error: %s", e)

    def start_periodic_check(self, interval: int = UPDATE_CHECK_INTERVAL):
        self._schedule_next_check(interval)

    def stop_periodic_check(self):
        if self._check_timer:
            self._check_timer.cancel()
            self._check_timer = None

    def _schedule_next_check(self, interval: int):
        if self._stop_event.is_set():
            return
        self._check_timer = threading.Timer(interval, self._periodic_check_task)
        self._check_timer.daemon = True
        self._check_timer.start()

    def _periodic_check_task(self):
        if self._stop_event.is_set():
            return
        try:
            self.check_for_updates(silent=True)
        except Exception as e:
            logger.warning("Periodic update check failed: %s", e)
        self._schedule_next_check(UPDATE_CHECK_INTERVAL)

    def check_for_updates(self, silent: bool = False) -> Optional[VersionInfo]:
        with self._lock:
            if self._state in (UpdateState.DOWNLOADING, UpdateState.INSTALLING):
                return None
            self._set_state(UpdateState.CHECKING)

        try:
            info = self._fetch_latest_release()
            with self._lock:
                if info and info.is_newer_than(self._current_version):
                    self._latest_info = info
                    self._set_state(UpdateState.DOWNLOAD_AVAILABLE)
                    log_update_event("update_available", {
                        "current": self._current_version,
                        "latest": info.version,
                        "silent": silent,
                    })
                    return info
                else:
                    self._set_state(UpdateState.IDLE)
                    if not silent:
                        log_update_event("no_update", {
                            "current": self._current_version,
                        })
                    return None
        except Exception as e:
            with self._lock:
                self._error_message = str(e)
                self._set_state(UpdateState.ERROR)
            log_update_event("check_error", {
                "error": str(e),
                "silent": silent,
            })
            raise CheckError(str(e))

    def _fetch_latest_release(self) -> Optional[VersionInfo]:
        url = f"{GITHUB_API_BASE}/repos/{GITHUB_OWNER}/{GITHUB_REPO}/releases/latest"

        cached = _ReleaseCache.get()
        if cached is not None:
            logger.debug("Using cached release info")
            return self._parse_release_data(cached)

        headers = _get_github_headers(self._github_token)
        resp = requests.get(url, headers=headers, timeout=REQUEST_TIMEOUT)
        if resp.status_code == 404:
            logger.info(_tr("No release found on GitHub"))
            return None
        if resp.status_code == 403:
            # 速率限制或 IP 限制
            try:
                detail = resp.json().get("message", "")
            except Exception:
                detail = resp.text
            if "rate limit" in detail.lower():
                raise CheckError(
                    _tr("GitHub API rate limit exceeded. Try again later or set GITHUB_TOKEN environment variable."))
            raise CheckError(f"GitHub API forbidden: {detail}")
        resp.raise_for_status()

        data = resp.json()
        _ReleaseCache.set(data)
        return self._parse_release_data(data)

    def _parse_release_data(self, data: dict) -> Optional[VersionInfo]:
        tag_name = data.get("tag_name", "")
        version = tag_name.lstrip("vV")
        changelog = data.get("body", "")
        published_at = data.get("published_at", "")

        download_url = ""
        sha256_hash = ""
        file_size = 0

        platform_keywords = _get_platform_asset_keywords()
        if sys.platform == "win32":
            supported_extensions = (".zip", ".exe")
        elif sys.platform == "darwin":
            supported_extensions = (".zip", ".dmg")
        else:
            supported_extensions = (".zip", ".tar.gz")

        def _is_platform_match(name: str) -> bool:
            lower = name.lower()
            for kw in platform_keywords:
                if kw in lower:
                    return True
            # Windows 单文件可执行文件通常没有 win 关键字，通过扩展名兜底
            if sys.platform == "win32" and lower.endswith(".exe"):
                return True
            return False

        def _is_client_asset(name: str) -> bool:
            lower = name.lower()
            # 排除明显非客户端的资源
            if "server" in lower:
                return False
            return True

        for asset in data.get("assets", []):
            name = asset.get("name", "")
            lower_name = name.lower()

            is_match = _is_platform_match(name)
            is_client = _is_client_asset(name)

            if is_match and is_client and lower_name.endswith(supported_extensions):
                download_url = asset.get("browser_download_url", "")
                file_size = asset.get("size", 0)
                sha256_hash = asset.get("digest", "").replace("sha256:", "")

        # 兜底：如果没有平台匹配，优先找客户端 exe/zip/dmg
        if not download_url:
            for asset in data.get("assets", []):
                name = asset.get("name", "")
                lower_name = name.lower()
                if not _is_client_asset(name):
                    continue
                if lower_name.endswith(supported_extensions):
                    download_url = asset.get("browser_download_url", "")
                    file_size = asset.get("size", 0)
                    sha256_hash = asset.get("digest", "").replace("sha256:", "")
                    break

        return VersionInfo(
            version=version,
            changelog=changelog,
            download_url=download_url,
            sha256=sha256_hash,
            file_size=file_size,
            release_name=data.get("name", ""),
            published_at=published_at,
        )

    def download_update(self) -> Path:
        if not self._latest_info or not self._latest_info.download_url:
            raise DownloadError(_tr("No download URL available"))

        with self._lock:
            if self._state == UpdateState.DOWNLOADING:
                raise DownloadError(_tr("Download already in progress"))
            self._set_state(UpdateState.DOWNLOADING)
            self._stop_event.clear()

        update_dir = get_update_dir()
        info = self._latest_info

        filename = info.download_url.split("/")[-1]
        dest_path = update_dir / filename
        temp_path = dest_path.with_suffix(".part")
        self._download_file_path = dest_path

        try:
            existing_size = 0
            if temp_path.exists():
                existing_size = temp_path.stat().st_size
                if info.file_size > 0 and existing_size >= info.file_size:
                    existing_size = 0
                    temp_path.unlink(missing_ok=True)

            headers = {}
            if existing_size > 0:
                headers["Range"] = f"bytes={existing_size}-"

            for attempt in range(MAX_RETRIES):
                if self._stop_event.is_set():
                    self._set_state(UpdateState.IDLE)
                    raise DownloadError(_tr("Download cancelled"))

                try:
                    resp = requests.get(
                        info.download_url,
                        headers=headers,
                        stream=True,
                        timeout=REQUEST_TIMEOUT,
                    )
                    if resp.status_code == 416:
                        temp_path.unlink(missing_ok=True)
                        existing_size = 0
                        headers.pop("Range", None)
                        continue

                    if resp.status_code not in (200, 206):
                        raise DownloadError(
                            f"HTTP {resp.status_code} " + _tr("downloading update"))

                    total = int(resp.headers.get("Content-Length", 0))
                    if resp.status_code == 206:
                        total += existing_size
                    elif resp.status_code == 200:
                        existing_size = 0
                        temp_path.unlink(missing_ok=True)

                    self._total_size = total if total > 0 else info.file_size

                    mode = "ab" if existing_size > 0 else "wb"
                    downloaded = existing_size
                    start_time = time.time()
                    last_notify = 0

                    with open(temp_path, mode) as f:
                        for chunk in resp.iter_content(
                                chunk_size=DOWNLOAD_CHUNK_SIZE):
                            if self._stop_event.is_set():
                                self._set_state(UpdateState.IDLE)
                                raise DownloadError(_tr("Download cancelled"))

                            f.write(chunk)
                            downloaded += len(chunk)
                            self._downloaded_size = downloaded

                            elapsed = time.time() - start_time
                            if elapsed > 0:
                                self._download_speed = downloaded / elapsed

                            if self._total_size > 0:
                                self._download_progress = (
                                    downloaded / self._total_size) * 100
                            else:
                                self._download_progress = 0

                            now = time.time()
                            if now - last_notify > 0.2:
                                self._notify_progress()
                                last_notify = now

                    self._download_progress = 100.0
                    self._notify_progress()
                    break

                except (requests.ConnectionError, requests.Timeout) as e:
                    if attempt < MAX_RETRIES - 1:
                        wait = RETRY_DELAY * (attempt + 1)
                        logger.warning(
                            _tr("Download attempt %d failed, retrying in %ds: %s"),
                            attempt + 1, wait, e)
                        time.sleep(wait)
                    else:
                        raise DownloadError(
                            _tr("Download failed after %d attempts: %s") % (MAX_RETRIES, e))

            temp_path.rename(dest_path)

            with self._lock:
                self._set_state(UpdateState.VERIFYING)

            if info.sha256:
                actual_hash = sha256_file(dest_path)
                if actual_hash != info.sha256:
                    dest_path.unlink(missing_ok=True)
                    raise VerifyError(
                        _tr("SHA256 mismatch: expected %s, got %s") % (info.sha256, actual_hash))

            with self._lock:
                self._set_state(UpdateState.READY_TO_INSTALL)

            log_update_event("download_complete", {
                "version": info.version,
                "file": str(dest_path),
                "size": downloaded,
                "sha256_verified": bool(info.sha256),
            })

            return dest_path

        except (DownloadError, VerifyError):
            with self._lock:
                self._error_message = _tr("Download/verify failed")
                self._set_state(UpdateState.ERROR)
            raise
        except Exception as e:
            with self._lock:
                self._error_message = str(e)
                self._set_state(UpdateState.ERROR)
            log_update_event("download_error", {"error": str(e)})
            raise DownloadError(str(e))

    def cancel_download(self):
        self._stop_event.set()
        if self._download_file_path:
            part = self._download_file_path.with_suffix(".part")
            part.unlink(missing_ok=True)
        self._set_state(UpdateState.IDLE)
        log_update_event("download_cancelled", {})

    def install_update(self, downloaded_file: Path):
        with self._lock:
            if self._state != UpdateState.READY_TO_INSTALL:
                raise InstallError(_tr("Not ready to install"))
            self._set_state(UpdateState.INSTALLING)

        self._install_manifest: list = []
        self._installed_files: list = []

        try:
            install_dir = self._get_install_dir()
            if not install_dir:
                raise InstallError(_tr("Cannot determine install directory"))

            if sys.platform == "darwin" and downloaded_file.suffix == ".dmg":
                self._install_dmg_update(downloaded_file)
            elif sys.platform == "win32" and downloaded_file.suffix == ".exe" and getattr(sys, "frozen", False):
                self._install_windows_exe_update(downloaded_file, install_dir)
            else:
                self._install_archive_update(downloaded_file, install_dir)

            log_update_event("install_complete", {
                "version": self._latest_info.version if self._latest_info else "unknown",
                "previous_version": self._current_version,
            })

            self._restart_application()

        except Exception as e:
            with self._lock:
                self._error_message = str(e)
                self._set_state(UpdateState.ERROR)
            log_update_event("install_error", {"error": str(e)})
            self._rollback_update()
            raise InstallError(str(e))

    def _install_windows_exe_update(self, new_exe: Path, install_dir: Path):
        """Windows 单文件 exe 热更新。

        原理：当前进程无法被自身替换，因此将新 exe 复制到 .nevo_update/
        并生成一个临时批处理脚本，由脚本等待当前进程退出后替换原 exe 并启动。
        """
        current_exe = Path(sys.executable)
        update_dir = get_update_dir()
        staged_exe = update_dir / "NEVO.exe.new"
        backup_exe = update_dir / "NEVO.exe.bak"

        # 复制新版本到暂存目录
        shutil.copy2(str(new_exe), str(staged_exe))

        # 备份当前 exe
        shutil.copy2(str(current_exe), str(backup_exe))

        # 生成替换脚本
        script_path = update_dir / "apply_update.bat"
        script_content = self._build_windows_update_bat(
            str(current_exe),
            str(staged_exe),
            str(backup_exe),
        )
        script_path.write_text(script_content, encoding="gbk")

        # 记录清单，用于回滚
        self._install_manifest = [
            {"type": "backup", "path": str(backup_exe), "original": str(current_exe)},
        ]
        self._installed_files = [str(script_path), str(staged_exe), str(backup_exe)]

        # 启动替换脚本（独立进程，不阻塞）
        subprocess.Popen(
            ["cmd.exe", "/c", str(script_path)],
            creationflags=subprocess.CREATE_NO_WINDOW,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def _build_windows_update_bat(self, current_exe: str, staged_exe: str, backup_exe: str) -> str:
        return f'''@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
set "CURRENT_EXE={current_exe}"
set "STAGED_EXE={staged_exe}"
set "BACKUP_EXE={backup_exe}"
set "PID={os.getpid()}"

:wait_loop
tasklist | findstr /I " %PID% " >nul
if %errorlevel% == 0 (
    timeout /t 1 /nobreak >nul
    goto wait_loop
)

timeout /t 1 /nobreak >nul

if exist "%BACKUP_EXE%" (
    del /f /q "%BACKUP_EXE%" >nul 2>&1
)

move /y "%CURRENT_EXE%" "%BACKUP_EXE%" >nul 2>&1
if %errorlevel% neq 0 (
    echo Failed to backup current executable >&2
    exit /b 1
)

move /y "%STAGED_EXE%" "%CURRENT_EXE%" >nul 2>&1
if %errorlevel% neq 0 (
    echo Failed to stage new executable >&2
    move /y "%BACKUP_EXE%" "%CURRENT_EXE%" >nul 2>&1
    exit /b 1
)

start "" "%CURRENT_EXE%"
endlocal
'''

    def _install_archive_update(self, downloaded_file: Path, install_dir: Path):
        temp_extract = get_update_dir() / "extracted"
        if temp_extract.exists():
            shutil.rmtree(temp_extract, ignore_errors=True)

        shutil.unpack_archive(str(downloaded_file), str(temp_extract))

        manifest = self._apply_update(temp_extract, install_dir)
        self._install_manifest = manifest
        self._installed_files = [str(downloaded_file), str(temp_extract)]

    def _install_dmg_update(self, dmg_path: Path):
        mount_point = Path(tempfile.mkdtemp(prefix="nevo_mount_"))
        try:
            subprocess.run(
                ["hdiutil", "attach", str(dmg_path),
                 "-mountpoint", str(mount_point),
                 "-nobrowse", "-quiet"],
                check=True, timeout=30,
            )

            app_path = mount_point / "NEVO.app"
            if not app_path.exists():
                app_paths = list(mount_point.glob("*.app"))
                if app_paths:
                    app_path = app_paths[0]

            if not app_path.exists():
                raise InstallError(_tr("No .app bundle found in DMG"))

            for candidate in _BUNDLE_CANDIDATES:
                if candidate.exists():
                    backup = candidate.parent / ".nevo_backup"
                    if backup.exists():
                        shutil.rmtree(backup, ignore_errors=True)
                    shutil.move(str(candidate), str(backup))
                    break

            target = _BUNDLE_CANDIDATES[0]
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                shutil.rmtree(target, ignore_errors=True)

            shutil.copytree(str(app_path), str(target), symlinks=True)

        finally:
            subprocess.run(
                ["hdiutil", "detach", str(mount_point), "-quiet"],
                timeout=10,
            )
            shutil.rmtree(mount_point, ignore_errors=True)

    def _get_install_dir(self) -> Optional[Path]:
        if getattr(sys, "frozen", False):
            exe_dir = Path(sys.executable).parent
            # 安全检查：避免在 Windows 系统目录下操作
            forbidden = {"Windows", "System32", "SysWOW64"}
            for part in exe_dir.parts:
                if part in forbidden:
                    return None
            return exe_dir
        return Path(__file__).parent.parent.parent.parent

    def _apply_update(self, source: Path, target: Path) -> list:
        """应用更新并返回操作清单，用于回滚。"""
        manifest = []
        for item in source.rglob("*"):
            if item.is_dir():
                continue
            relative = item.relative_to(source)
            dest = target / relative
            dest.parent.mkdir(parents=True, exist_ok=True)

            backup_entry = None
            if dest.exists():
                # 备份原文件
                backup_path = get_update_dir() / "backup" / relative
                backup_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(str(dest), str(backup_path))
                backup_entry = str(backup_path)

            shutil.copy2(str(item), str(dest))
            manifest.append({
                "relative": str(relative).replace("\\", "/"),
                "target": str(dest),
                "backup": backup_entry,
            })
        return manifest

    def _rollback_update(self):
        if not self._install_manifest:
            return
        try:
            for entry in self._install_manifest:
                if isinstance(entry, dict):
                    if entry.get("type") == "backup":
                        # Windows exe 回滚
                        original = Path(entry["original"])
                        backup = Path(entry["path"])
                        if backup.exists() and original.exists():
                            original.unlink()
                            shutil.copy2(str(backup), str(original))
                    else:
                        target = Path(entry["target"])
                        backup = Path(entry["backup"]) if entry.get("backup") else None
                        if backup and backup.exists():
                            shutil.copy2(str(backup), str(target))
                        elif target.exists():
                            target.unlink()
            log_update_event("rollback_complete", {})
        except Exception as e:
            logger.error("Rollback failed: %s", e)
            log_update_event("rollback_error", {"error": str(e)})

    def _restart_application(self):
        if sys.platform == "darwin":
            for candidate in _BUNDLE_CANDIDATES:
                if candidate.exists():
                    subprocess.Popen(["open", str(candidate)])
                    os._exit(0)
                    return

        if getattr(sys, "frozen", False):
            subprocess.Popen([sys.executable])
        else:
            python = sys.executable
            subprocess.Popen([python] + sys.argv)
        os._exit(0)

    def cleanup(self):
        self.stop_periodic_check()
        self._stop_event.set()
        update_dir = get_update_dir()
        for item in update_dir.iterdir():
            if item.suffix == ".part":
                item.unlink(missing_ok=True)
