"""摄像头采集模块（基于 OpenCV）。

提供在独立线程中读取摄像头帧的能力，避免阻塞 UI 主线程。
"""
import threading
import time
from typing import Optional, Tuple, List

import numpy as np

_HAS_CV2 = False
_CV2_ERROR = ""

try:
    import cv2
    _HAS_CV2 = True
except ImportError as e:
    _CV2_ERROR = str(e)


class CameraCapture:
    """摄像头采集器，运行在独立线程中。"""

    def __init__(self):
        self._cap: Optional[object] = None
        self._device_index: int = 0
        self._width: int = 640
        self._height: int = 480
        self._fps: int = 30
        self._running: bool = False
        self._capture_thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()
        self._latest_frame: Optional[np.ndarray] = None
        self._frame_ready = threading.Event()
        self._last_frame_time: float = 0.0

    @staticmethod
    def is_available() -> bool:
        """返回当前环境是否可用 OpenCV 摄像头。"""
        return _HAS_CV2

    @staticmethod
    def enumerate_devices(max_index: int = 10) -> List[Tuple[int, str]]:
        """枚举可用的摄像头设备，返回 (index, name) 列表。

        仅在 OpenCV 可用时有效；否则返回空列表。
        """
        if not _HAS_CV2:
            return []
        devices = []
        for i in range(max_index):
            try:
                cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)
                if cap is None or not cap.isOpened():
                    cap = cv2.VideoCapture(i)
                if cap is not None and cap.isOpened():
                    # 尝试读取一帧以确认设备真实可用
                    ret, _ = cap.read()
                    if ret:
                        name = f"Camera {i}"
                        # Windows 下可尝试读取 friendly name
                        try:
                            backend = cap.getBackendName()
                            name = f"Camera {i} ({backend})"
                        except Exception:
                            pass
                        devices.append((i, name))
                    cap.release()
            except Exception:
                pass
        return devices

    def start(self, device_index: int = 0, width: int = 640,
              height: int = 480, fps: int = 30) -> bool:
        """启动指定摄像头的采集线程。"""
        if not _HAS_CV2:
            return False
        self.stop()

        self._device_index = device_index
        self._width = width
        self._height = height
        self._fps = fps

        try:
            # 优先使用 DirectShow 后端，Windows 下更稳定
            cap = cv2.VideoCapture(device_index, cv2.CAP_DSHOW)
            if not cap.isOpened():
                cap = cv2.VideoCapture(device_index)
            if not cap.isOpened():
                return False

            cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
            cap.set(cv2.CAP_PROP_FPS, fps)
            cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

            self._cap = cap
            self._running = True
            self._latest_frame = None
            self._frame_ready.clear()
            self._capture_thread = threading.Thread(target=self._capture_loop, daemon=True)
            self._capture_thread.start()
            return True
        except Exception as e:
            print(f"[CAMERA] start failed: {e}")
            self._running = False
            self._cap = None
            return False

    def stop(self):
        """停止采集线程并释放摄像头。"""
        self._running = False
        if self._capture_thread and self._capture_thread.is_alive():
            self._capture_thread.join(timeout=2.0)
        self._capture_thread = None
        if self._cap is not None:
            try:
                self._cap.release()
            except Exception:
                pass
            self._cap = None
        with self._lock:
            self._latest_frame = None

    def is_running(self) -> bool:
        """返回采集线程是否正在运行。"""
        return self._running and self._cap is not None and self._cap.isOpened()

    def capture_frame(self, timeout: float = 0.5) -> Optional[np.ndarray]:
        """获取最新的 BGR 帧（numpy 数组），不会阻塞。

        如果还没有帧就绪，返回 None。
        """
        with self._lock:
            frame = self._latest_frame
            if frame is not None:
                # 复制一份，避免后续被覆盖
                return frame.copy()
        return None

    def _capture_loop(self):
        """采集线程主循环。"""
        interval = 1.0 / max(self._fps, 1)
        while self._running:
            if self._cap is None:
                time.sleep(0.01)
                continue
            try:
                ret, frame = self._cap.read()
                if ret and frame is not None and frame.size > 0:
                    # 转换并裁剪为指定分辨率
                    frame = self._ensure_resolution(frame)
                    with self._lock:
                        self._latest_frame = frame
                    self._frame_ready.set()
                    self._last_frame_time = time.time()
                else:
                    # 读取失败时短暂等待，避免 CPU 占用过高
                    time.sleep(0.01)
            except Exception as e:
                print(f"[CAMERA] capture_loop error: {e}")
                time.sleep(0.05)

            # 按目标帧率休眠
            sleep_time = interval - (time.time() - self._last_frame_time)
            if sleep_time > 0:
                time.sleep(sleep_time)

    def _ensure_resolution(self, frame: np.ndarray) -> np.ndarray:
        """将帧缩放到目标分辨率，保持纵横比并居中裁剪。"""
        h, w = frame.shape[:2]
        if w == self._width and h == self._height:
            return frame
        # 保持纵横比缩放
        scale = max(self._width / max(w, 1), self._height / max(h, 1))
        new_w = int(w * scale)
        new_h = int(h * scale)
        try:
            resized = cv2.resize(frame, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
        except Exception:
            return frame
        # 居中裁剪
        x0 = (new_w - self._width) // 2
        y0 = (new_h - self._height) // 2
        x1 = x0 + self._width
        y1 = y0 + self._height
        return resized[y0:y1, x0:x1]

    @property
    def width(self) -> int:
        return self._width

    @property
    def height(self) -> int:
        return self._height

    @property
    def fps(self) -> int:
        return self._fps
