import threading
import time
import numpy as np

try:
    import mss
    HAS_MSS = True
except ImportError as e:
    HAS_MSS = False
    _MSS_ERROR = str(e)

try:
    import win32gui
    import win32ui
    import win32con
    HAS_WIN32 = True
except ImportError:
    HAS_WIN32 = False


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


class ScreenCapture:
    def __init__(self):
        self._source_type = SOURCE_SCREEN
        self._source_index = 0
        self._fps = 15
        self._running = False
        self._capture_rect = None
        self._window_hwnd = None

    @staticmethod
    def is_available():
        return HAS_MSS

    def enumerate_sources(self):
        sources = []
        if HAS_MSS:
            with mss.mss() as sct:
                for i, mon in enumerate(sct.monitors[1:], 0):
                    sources.append(ScreenSource(
                        source_type=SOURCE_SCREEN,
                        index=i,
                        name=f"Monitor {i + 1} ({mon['width']}x{mon['height']})",
                        width=mon['width'],
                        height=mon['height'],
                    ))
        if HAS_WIN32:
            seen = []
            def _enum_cb(hwnd, _):
                if not win32gui.IsWindowVisible(hwnd):
                    return
                title = win32gui.GetWindowText(hwnd)
                if not title or len(title) < 2:
                    return
                if title in seen:
                    return
                seen.append(title)
                sources.append(ScreenSource(
                    source_type=SOURCE_WINDOW,
                    index=len(seen) - 1,
                    name=title,
                    width=0,
                    height=0,
                ))
            try:
                win32gui.EnumWindows(_enum_cb, None)
            except Exception:
                pass
        return sources

    def start(self, source_type, source_index, fps=15):
        self._source_type = source_type
        self._source_index = source_index
        self._fps = fps
        self._running = True
        if source_type == SOURCE_WINDOW and HAS_WIN32:
            seen = []
            def _find_cb(hwnd, _):
                if not win32gui.IsWindowVisible(hwnd):
                    return
                title = win32gui.GetWindowText(hwnd)
                if not title or len(title) < 2:
                    return
                if title in seen:
                    return
                seen.append(title)
                if len(seen) - 1 == source_index:
                    self._window_hwnd = hwnd
            try:
                win32gui.EnumWindows(_find_cb, None)
            except Exception:
                pass

    def stop(self):
        self._running = False
        self._window_hwnd = None

    def capture_frame(self):
        if not HAS_MSS:
            return None
        try:
            with mss.mss() as sct:
                if self._source_type == SOURCE_SCREEN:
                    monitors = sct.monitors
                    idx = self._source_index + 1
                    if idx >= len(monitors):
                        idx = 1
                    monitor = monitors[idx]
                elif self._source_type == SOURCE_WINDOW and self._window_hwnd:
                    rect = win32gui.GetWindowRect(self._window_hwnd)
                    monitor = {
                        "left": rect[0],
                        "top": rect[1],
                        "width": rect[2] - rect[0],
                        "height": rect[3] - rect[1],
                    }
                else:
                    monitor = sct.monitors[1]

                shot = sct.grab(monitor)
                frame = np.array(shot, dtype=np.uint8)
                return np.ascontiguousarray(frame[:, :, :3])
        except Exception:
            return None
        return None

    @property
    def fps(self):
        return self._fps

    @property
    def running(self):
        return self._running
