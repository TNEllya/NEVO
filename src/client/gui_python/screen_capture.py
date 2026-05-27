import ctypes
import sys
import threading
import time
import numpy as np

_HAS_MSS = False
_MSS_ERROR = ""

try:
    import mss
    _HAS_MSS = True
except ImportError as e:
    _MSS_ERROR = str(e)

_WIN32 = "win32"
_DARWIN = "darwin"
_LINUX = "linux"
_PLATFORM = sys.platform

_HAS_WIN32 = False
if _PLATFORM == _WIN32:
    try:
        import win32gui
        import win32con
        _HAS_WIN32 = True
    except ImportError:
        pass

_HAS_QUARTZ = False
if _PLATFORM == _DARWIN:
    try:
        import Quartz
        _HAS_QUARTZ = True
    except ImportError:
        pass


SOURCE_SCREEN = 0
SOURCE_WINDOW = 1
SOURCE_TAB = 2


class ScreenSource:
    def __init__(self, source_type, index, name, width, height):
        self.source_type = source_type
        self.index = index
        self.name = name
        self.width = width
        self.height = height

    def __repr__(self):
        return f"ScreenSource(type={self.source_type}, idx={self.index}, name={self.name})"


def _macos_enumerate_windows():
    if not _HAS_QUARTZ:
        return []
    sources = []
    seen = set()
    try:
        window_list = Quartz.CGWindowListCopyWindowInfo(
            Quartz.kCGWindowListOptionOnScreenOnly
            | Quartz.kCGWindowListExcludeDesktopElements,
            Quartz.kCGNullWindowID,
        )
        for i, win in enumerate(window_list):
            owner = win.get(Quartz.kCGWindowOwnerName, "")
            name = win.get(Quartz.kCGWindowName, "")
            bounds = win.get(Quartz.kCGWindowBounds, {})
            width = int(bounds.get("Width", 0))
            height = int(bounds.get("Height", 0))
            if width < 200 or height < 100:
                continue
            layer = win.get(Quartz.kCGWindowLayer, 0)
            if layer > 0:
                continue
            if not name:
                name = owner
            display_name = f"{owner} — {name}" if (owner and name and name != owner) else (name or owner or f"Window {i}")
            if display_name in seen:
                continue
            seen.add(display_name)
            w = width if width > 0 else 0
            h = height if height > 0 else 0
            sources.append(ScreenSource(
                source_type=SOURCE_WINDOW,
                index=len(sources),
                name=display_name,
                width=w,
                height=h,
            ))
    except Exception:
        pass
    return sources


def _macos_find_window_index(windows, display_name):
    for i, src in enumerate(windows):
        if src.name == display_name:
            return i
    return -1


def _macos_get_window_bounds(display_name):
    if not _HAS_QUARTZ:
        return None
    try:
        window_list = Quartz.CGWindowListCopyWindowInfo(
            Quartz.kCGWindowListOptionOnScreenOnly
            | Quartz.kCGWindowListExcludeDesktopElements,
            Quartz.kCGNullWindowID,
        )
        for win in window_list:
            owner = win.get(Quartz.kCGWindowOwnerName, "")
            name = win.get(Quartz.kCGWindowName, "")
            candidate = f"{owner} — {name}" if (owner and name and name != owner) else (name or owner)
            if candidate == display_name:
                bounds = win.get(Quartz.kCGWindowBounds, {})
                return {
                    "left": int(bounds.get("X", 0)),
                    "top": int(bounds.get("Y", 0)),
                    "width": int(bounds.get("Width", 0)),
                    "height": int(bounds.get("Height", 0)),
                }
    except Exception:
        pass
    return None


def _win32_enumerate_windows():
    if not _HAS_WIN32:
        return []
    sources = []
    seen = set()
    def _enum_cb(hwnd, _):
        if not win32gui.IsWindowVisible(hwnd):
            return
        title = win32gui.GetWindowText(hwnd)
        if not title or len(title) < 2:
            return
        if title in seen:
            return
        seen.add(title)
        rect = win32gui.GetWindowRect(hwnd)
        w = rect[2] - rect[0]
        h = rect[3] - rect[1]
        sources.append(ScreenSource(
            source_type=SOURCE_WINDOW,
            index=len(sources),
            name=title,
            width=w,
            height=h,
        ))
    try:
        win32gui.EnumWindows(_enum_cb, None)
    except Exception:
        pass
    return sources


def _win32_find_window_rect(win_index, window_sources):
    if not _HAS_WIN32:
        return None
    seen = []
    result = [None]

    def _find_cb(hwnd, _):
        if result[0] is not None:
            return
        if not win32gui.IsWindowVisible(hwnd):
            return
        title = win32gui.GetWindowText(hwnd)
        if not title or len(title) < 2:
            return
        if title in seen:
            return
        seen.append(title)
        if len(seen) - 1 == win_index:
            rect = win32gui.GetWindowRect(hwnd)
            result[0] = {
                "left": rect[0],
                "top": rect[1],
                "width": rect[2] - rect[0],
                "height": rect[3] - rect[1],
            }
    try:
        win32gui.EnumWindows(_find_cb, None)
    except Exception:
        pass
    return result[0]


class ScreenCapture:
    def __init__(self):
        self._source_type = SOURCE_SCREEN
        self._source_index = 0
        self._fps = 15
        self._running = False
        self._capture_rect = None
        self._window_hwnd = None
        self._window_name = None
        self._window_sources = []

    @staticmethod
    def is_available():
        return _HAS_MSS

    def enumerate_sources(self):
        sources = []
        if _HAS_MSS:
            with mss.mss() as sct:
                for i, mon in enumerate(sct.monitors[1:], 0):
                    sources.append(ScreenSource(
                        source_type=SOURCE_SCREEN,
                        index=i,
                        name=f"Display {i + 1} ({mon['width']}x{mon['height']})",
                        width=mon['width'],
                        height=mon['height'],
                    ))
            for sct_mon in sct.monitors[1:]:
                pass

        if _PLATFORM == _DARWIN:
            mac_windows = _macos_enumerate_windows()
            self._window_sources = mac_windows
            for src in mac_windows:
                sources.append(src)
        elif _PLATFORM == _WIN32:
            win_windows = _win32_enumerate_windows()
            self._window_sources = win_windows
            for src in win_windows:
                sources.append(src)

        return sources

    def start(self, source_type, source_index, fps=15):
        self._source_type = source_type
        self._source_index = source_index
        self._fps = fps
        self._running = True
        self._window_hwnd = None
        self._window_name = None
        self._capture_rect = None

        if source_type == SOURCE_WINDOW:
            if _PLATFORM == _DARWIN:
                mac_windows = _macos_enumerate_windows()
                self._window_sources = mac_windows
                if 0 <= source_index < len(mac_windows):
                    self._window_name = mac_windows[source_index].name
            elif _PLATFORM == _WIN32:
                irect = _win32_find_window_rect(source_index, self._window_sources)
                if irect is not None:
                    self._capture_rect = irect

    def stop(self):
        self._running = False
        self._window_hwnd = None
        self._window_name = None
        self._capture_rect = None

    def capture_frame(self):
        if not _HAS_MSS:
            return None
        try:
            with mss.mss() as sct:
                if self._source_type == SOURCE_SCREEN:
                    monitors = sct.monitors
                    idx = self._source_index + 1
                    if idx >= len(monitors):
                        idx = 1
                    monitor = monitors[idx]

                elif self._source_type == SOURCE_WINDOW:
                    if _PLATFORM == _DARWIN and self._window_name:
                        bounds = _macos_get_window_bounds(self._window_name)
                        if not bounds or bounds["width"] <= 0 or bounds["height"] <= 0:
                            return None
                        monitor = bounds
                    elif _PLATFORM == _WIN32 and self._capture_rect:
                        monitor = self._capture_rect
                    elif _PLATFORM == _WIN32 and self._window_hwnd:
                        rect = win32gui.GetWindowRect(self._window_hwnd)
                        monitor = {
                            "left": rect[0],
                            "top": rect[1],
                            "width": rect[2] - rect[0],
                            "height": rect[3] - rect[1],
                        }
                    else:
                        monitor = sct.monitors[1]
                else:
                    monitor = sct.monitors[1]

                shot = sct.grab(monitor)
                frame = np.array(shot, dtype=np.uint8)
                return np.ascontiguousarray(frame[:, :, :3])
        except Exception:
            return None

    @property
    def fps(self):
        return self._fps

    @property
    def running(self):
        return self._running
