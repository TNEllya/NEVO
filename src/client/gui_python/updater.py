import hashlib
import json
import logging
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Callable, Optional

import requests

logger = logging.getLogger("nevo.updater")

GITHUB_OWNER = "TNEllya"
GITHUB_REPO = "NEVO"
GITHUB_API_BASE = "https://api.github.com"
UPDATE_CHECK_INTERVAL = 3600
DOWNLOAD_CHUNK_SIZE = 65536
MAX_RETRIES = 5
RETRY_DELAY = 3
REQUEST_TIMEOUT = 30


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
        parts = version_str.strip().lstrip("v").split(".")
        result = []
        for p in parts:
            try:
                result.append(int(p))
            except ValueError:
                result.append(0)
        while len(result) < 3:
            result.append(0)
        return tuple(result[:3])

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
        headers = {"Accept": "application/vnd.github+json"}

        resp = requests.get(url, headers=headers, timeout=REQUEST_TIMEOUT)
        if resp.status_code == 404:
            logger.info("No release found on GitHub")
            return None
        resp.raise_for_status()

        data = resp.json()
        tag_name = data.get("tag_name", "")
        version = tag_name.lstrip("v")
        changelog = data.get("body", "")
        published_at = data.get("published_at", "")

        download_url = ""
        sha256_hash = ""
        file_size = 0

        for asset in data.get("assets", []):
            name = asset.get("name", "").lower()
            if "client" in name and name.endswith(".zip"):
                download_url = asset.get("browser_download_url", "")
                file_size = asset.get("size", 0)
            elif "sha256" in name or name.endswith(".sha256"):
                sha256_url = asset.get("browser_download_url", "")
                if sha256_url:
                    try:
                        sha_resp = requests.get(sha256_url, timeout=REQUEST_TIMEOUT)
                        sha_resp.raise_for_status()
                        sha256_hash = sha_resp.text.strip().split()[0]
                    except Exception as e:
                        logger.warning("Failed to fetch SHA256: %s", e)

        if not download_url:
            for asset in data.get("assets", []):
                name = asset.get("name", "")
                if name.endswith(".zip"):
                    download_url = asset.get("browser_download_url", "")
                    file_size = asset.get("size", 0)
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
            raise DownloadError("No download URL available")

        with self._lock:
            if self._state == UpdateState.DOWNLOADING:
                raise DownloadError("Download already in progress")
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
                    raise DownloadError("Download cancelled")

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
                            f"HTTP {resp.status_code} downloading update")

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
                                raise DownloadError("Download cancelled")

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
                            "Download attempt %d failed, retrying in %ds: %s",
                            attempt + 1, wait, e)
                        time.sleep(wait)
                    else:
                        raise DownloadError(
                            f"Download failed after {MAX_RETRIES} attempts: {e}")

            temp_path.rename(dest_path)

            with self._lock:
                self._set_state(UpdateState.VERIFYING)

            if info.sha256:
                actual_hash = sha256_file(dest_path)
                if actual_hash != info.sha256:
                    dest_path.unlink(missing_ok=True)
                    raise VerifyError(
                        f"SHA256 mismatch: expected {info.sha256}, "
                        f"got {actual_hash}")

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
                self._error_message = "Download/verify failed"
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
                raise InstallError("Not ready to install")
            self._set_state(UpdateState.INSTALLING)

        try:
            install_dir = self._get_install_dir()
            if not install_dir:
                raise InstallError("Cannot determine install directory")

            backup_dir = install_dir.parent / ".nevo_backup"
            if backup_dir.exists():
                shutil.rmtree(backup_dir, ignore_errors=True)
            shutil.copytree(install_dir, backup_dir)

            temp_extract = get_update_dir() / "extracted"
            if temp_extract.exists():
                shutil.rmtree(temp_extract, ignore_errors=True)

            shutil.unpack_archive(str(downloaded_file), str(temp_extract))

            self._apply_update(temp_extract, install_dir)

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

    def _get_install_dir(self) -> Optional[Path]:
        if getattr(sys, "frozen", False):
            return Path(sys.executable).parent
        return Path(__file__).parent.parent.parent.parent

    def _apply_update(self, source: Path, target: Path):
        for item in source.rglob("*"):
            if item.is_dir():
                continue
            relative = item.relative_to(source)
            dest = target / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(str(item), str(dest))

    def _rollback_update(self):
        install_dir = self._get_install_dir()
        if not install_dir:
            return
        backup_dir = install_dir.parent / ".nevo_backup"
        if not backup_dir.exists():
            return
        try:
            shutil.rmtree(install_dir, ignore_errors=True)
            shutil.copytree(backup_dir, install_dir)
            shutil.rmtree(backup_dir, ignore_errors=True)
            log_update_event("rollback_complete", {})
        except Exception as e:
            logger.error("Rollback failed: %s", e)
            log_update_event("rollback_error", {"error": str(e)})

    def _restart_application(self):
        python = sys.executable
        if getattr(sys, "frozen", False):
            subprocess.Popen([sys.executable])
        else:
            subprocess.Popen([python] + sys.argv)
        os._exit(0)

    def cleanup(self):
        self.stop_periodic_check()
        self._stop_event.set()
        update_dir = get_update_dir()
        for item in update_dir.iterdir():
            if item.suffix == ".part":
                item.unlink(missing_ok=True)
