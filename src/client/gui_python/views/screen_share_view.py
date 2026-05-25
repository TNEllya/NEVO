import time
import numpy as np
from PyQt5.QtCore import Qt, QTimer, pyqtSignal
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QFrame, QLabel, QWidget, QSizePolicy, QScrollArea,
)
from PyQt5.QtGui import QPixmap, QImage, QPainter, QColor, QFont, QLinearGradient


SHARER_TIMEOUT = 3.0


class ScreenShareView(QFrame):
    video_frame_received = pyqtSignal(int, object, int, int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._sharers = {}
        self._current_sharer = 0
        self._current_frame = None
        self._current_width = 0
        self._current_height = 0
        self._setup_ui()
        self._stale_timer = QTimer(self)
        self._stale_timer.timeout.connect(self._check_stale)
        self._stale_timer.start(2000)
        self.video_frame_received.connect(self._on_frame)
        self.setStyleSheet(
            "ScreenShareView {"
            "  background-color: #2b2d31;"
            "  border: none;"
            "  border-radius: 12px;"
            "}"
        )

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(8, 8, 8, 8)
        main_layout.setSpacing(0)

        inner = QFrame()
        inner.setStyleSheet(
            "QFrame {"
            "  background-color: #2b2d31;"
            "  border: none;"
            "  border-radius: 12px;"
            "}"
        )
        inner_layout = QVBoxLayout(inner)
        inner_layout.setContentsMargins(0, 0, 0, 0)
        inner_layout.setSpacing(0)

        title_bar = QFrame()
        title_bar.setFixedHeight(44)
        title_bar.setStyleSheet(
            "background-color: #1e1f22;"
            " border: none;"
            " border-top-left-radius: 12px;"
            " border-top-right-radius: 12px;"
        )
        title_layout = QHBoxLayout(title_bar)
        title_layout.setContentsMargins(16, 0, 16, 0)

        self._title_label = QLabel(self.tr("Screen Share"))
        self._title_label.setStyleSheet("color: #dbdee1; font-size: 15px; font-weight: bold; border: none;")
        title_layout.addWidget(self._title_label)

        self._sharer_count = QLabel("")
        self._sharer_count.setStyleSheet("color: #8b8d97; font-size: 12px; border: none;")
        title_layout.addWidget(self._sharer_count)
        title_layout.addStretch()

        inner_layout.addWidget(title_bar)

        self._video_area = QFrame()
        self._video_area.setStyleSheet("background-color: #2b2d31; border: none;")
        self._video_area.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        video_layout = QVBoxLayout(self._video_area)
        video_layout.setContentsMargins(0, 0, 0, 0)
        video_layout.setAlignment(Qt.AlignCenter)

        self._video_label = QLabel()
        self._video_label.setAlignment(Qt.AlignCenter)
        self._video_label.setMinimumSize(320, 180)
        self._video_label.setStyleSheet("background-color: #2b2d31; border: none;")
        video_layout.addWidget(self._video_label)

        self._placeholder = QLabel(self.tr("No active screen shares"))
        self._placeholder.setAlignment(Qt.AlignCenter)
        self._placeholder.setStyleSheet(
            "color: #8b8d97; font-size: 18px; background-color: transparent; border: none;"
        )
        self._placeholder.setVisible(True)
        video_layout.addWidget(self._placeholder)

        inner_layout.addWidget(self._video_area, 1)

        self._sharer_bar = QFrame()
        self._sharer_bar.setFixedHeight(36)
        self._sharer_bar.setStyleSheet(
            "background-color: rgba(30, 31, 34, 0.9); border: none;"
            " border-bottom-left-radius: 12px; border-bottom-right-radius: 12px;"
        )
        sharer_layout = QHBoxLayout(self._sharer_bar)
        sharer_layout.setContentsMargins(12, 0, 12, 0)

        self._sharer_id_label = QLabel("")
        self._sharer_id_label.setStyleSheet("color: #43b581; font-size: 13px; font-weight: bold; border: none;")
        sharer_layout.addWidget(self._sharer_id_label)
        sharer_layout.addStretch()

        self._sharer_bar.setVisible(False)
        inner_layout.addWidget(self._sharer_bar)

        main_layout.addWidget(inner, 1)

    def on_video_frame(self, sender_id, frame_bgr, width, height):
        self.video_frame_received.emit(sender_id, frame_bgr, width, height)

    def _on_frame(self, sender_id, frame_bgr, width, height):
        now = time.time()
        if sender_id not in self._sharers:
            self._sharers[sender_id] = {"first_seen": now, "last_frame": now}
        else:
            self._sharers[sender_id]["last_frame"] = now

        if self._current_sharer != sender_id:
            self._current_sharer = sender_id

        self._current_frame = frame_bgr
        self._current_width = width
        self._current_height = height
        self._render_frame()
        self._update_sharer_info()

    def _render_frame(self):
        if self._current_frame is None:
            return

        try:
            frame_rgb = self._current_frame[:, :, ::-1].copy()
            h, w, ch = frame_rgb.shape
            bytes_per_line = ch * w
            qimg = QImage(frame_rgb.data, w, h, bytes_per_line, QImage.Format_RGB888)

            avail_w = self._video_area.width() - 8
            avail_h = self._video_area.height() - 8
            if avail_w < 1 or avail_h < 1:
                return

            scaled = qimg.scaled(avail_w, avail_h, Qt.KeepAspectRatio, Qt.SmoothTransformation)
            self._video_label.setPixmap(QPixmap.fromImage(scaled))
            self._video_label.setVisible(True)
            self._placeholder.setVisible(False)
        except Exception:
            pass

    def _update_sharer_info(self):
        count = len(self._sharers)
        if count == 0:
            self._sharer_count.setText("")
            self._sharer_id_label.setText("")
            self._sharer_bar.setVisible(False)
            return

        if count == 1:
            self._sharer_count.setText(self.tr("1 sharer"))
        else:
            self._sharer_count.setText(self.tr("{} sharers").format(count))

        if self._current_sharer:
            self._sharer_id_label.setText(
                self.tr("Sharer ID: {}").format(self._current_sharer)
            )
            self._sharer_bar.setVisible(True)
        else:
            self._sharer_bar.setVisible(False)

    def _check_stale(self):
        now = time.time()
        stale = []
        for uid, info in list(self._sharers.items()):
            if now - info["last_frame"] > SHARER_TIMEOUT:
                stale.append(uid)

        for uid in stale:
            del self._sharers[uid]
            if self._current_sharer == uid:
                self._current_sharer = 0
                self._current_frame = None

        if not self._sharers:
            self._current_sharer = 0
            self._current_frame = None
            self._video_label.clear()
            self._video_label.setVisible(False)
            self._placeholder.setVisible(True)
            self._sharer_bar.setVisible(False)
            self._sharer_count.setText("")

        self._update_sharer_info()

    def clear(self):
        self._sharers.clear()
        self._current_sharer = 0
        self._current_frame = None
        self._video_label.clear()
        self._video_label.setVisible(False)
        self._placeholder.setVisible(True)
        self._sharer_bar.setVisible(False)
        self._sharer_count.setText("")
        self._sharer_id_label.setText("")

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self._current_frame is not None:
            self._render_frame()