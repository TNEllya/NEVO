"""NEVO v2 video call window — full-screen call view (design/pages/视频通话.html).

Shows the remote video full-bleed (or a caller-avatar placeholder when the
peer camera is off), a self-view PiP, a frosted control bar and a top bar
with caller info / connection quality.
"""

import os
import sys
import time

from PyQt5.QtCore import Qt, pyqtSignal, QTimer, QSize
from PyQt5.QtGui import QPixmap, QImage, QColor, QPainter, QLinearGradient
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QFrame, QSizePolicy,
)

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from v2.theme import palette, render_icon, render_icon_qicon, IconButton, Avatar, v2_qss


class _VideoLabel(QLabel):
    """A QLabel that scales a video frame to fill the widget, keeping aspect."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAlignment(Qt.AlignCenter)
        self._frame = None
        self.setStyleSheet("background: transparent;")
        self.setMinimumSize(1, 1)

    def set_frame(self, frame_bgr, w, h):
        # frame_bgr is a numpy ndarray (BGR). Convert to QImage (RGB).
        try:
            import numpy as np
            if frame_bgr is None or w == 0 or h == 0:
                return
            rgb = frame_bgr[:, :, ::-1].copy()  # BGR -> RGB
            qimg = QImage(rgb.data, w, h, 3 * w, QImage.Format_RGB888)
            self._frame = QPixmap.fromImage(qimg)
            self.update()
        except Exception:
            pass

    def clear_frame(self):
        self._frame = None
        self.clear()

    def paintEvent(self, e):
        if self._frame is not None and not self._frame.isNull():
            p = QPainter(self)
            p.setRenderHint(QPainter.SmoothPixmapTransform)
            scaled = self._frame.scaled(self.size(), Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
            x = (self.width() - scaled.width()) // 2
            y = (self.height() - scaled.height()) // 2
            p.drawPixmap(x, y, scaled)
        else:
            super().paintEvent(e)


class VideoCallWindow(QWidget):
    """Full-screen one-to-one video call window."""

    hangup_requested = pyqtSignal()
    video_mute_toggled = pyqtSignal(bool)
    camera_changed = pyqtSignal(int)
    mic_mute_toggled = pyqtSignal(bool)
    screen_share_requested = pyqtSignal()
    back_requested = pyqtSignal()

    def __init__(self, peer_name: str = "对方", parent=None):
        super().__init__(parent)
        self.setWindowFlags(Qt.Window)
        self.setWindowTitle(f"视频通话 — {peer_name}")
        self.resize(960, 720)
        self.setMinimumSize(640, 480)
        self.setStyleSheet(v2_qss())

        self._peer_name = peer_name
        self._local_user_id = 0
        self._camera_devices = []
        self._call_start = time.time()
        self._has_remote_video = False
        self._video_muted = False
        self._mic_muted = False
        self._setup_ui()

        # Duration timer
        self._timer = QTimer(self)
        self._timer.setInterval(1000)
        self._timer.timeout.connect(self._update_duration)
        self._timer.start()

    def _setup_ui(self):
        # Root: a frame with dark gradient background
        self.setObjectName("VideoCallRoot")
        root = QFrame(self)
        root.setStyleSheet(self._bg_style())
        root.setGeometry(0, 0, self.width(), self.height())

        # Remote video (full-bleed)
        self._remote_video = _VideoLabel(root)
        self._remote_video.setGeometry(0, 0, self.width(), self.height())

        # Caller avatar placeholder (no-video state)
        self._placeholder = QFrame(root)
        self._placeholder.setGeometry(0, 0, self.width(), self.height())
        play = QVBoxLayout(self._placeholder)
        play.setContentsMargins(0, 0, 0, 0)
        play.setAlignment(Qt.AlignCenter)
        ph_wrap = QVBoxLayout()
        ph_wrap.setSpacing(16)
        ph_wrap.setAlignment(Qt.AlignCenter)
        av = Avatar(96)
        av.set_user(self._peer_name)
        ph_wrap.addWidget(av, alignment=Qt.AlignCenter)
        self._ph_name = QLabel(self._peer_name)
        self._ph_name.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 20px; font-weight: 600;")
        ph_wrap.addWidget(self._ph_name, alignment=Qt.AlignCenter)
        self._ph_sub = QLabel("等待对方视频…")
        self._ph_sub.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 13px;")
        ph_wrap.addWidget(self._ph_sub, alignment=Qt.AlignCenter)
        play.addLayout(ph_wrap)

        # Top bar
        topbar = QFrame(root)
        topbar.setStyleSheet("background: transparent;")
        topbar.setGeometry(0, 0, self.width(), 64)
        tlay = QHBoxLayout(topbar)
        tlay.setContentsMargins(20, 12, 32, 12)
        tlay.setSpacing(12)
        back = QPushButton("返回")
        back.setObjectName("GhostBtn")
        back.setCursor(Qt.PointingHandCursor)
        back.setIcon(render_icon_qicon("arrow-left", 18, palette().get("text_secondary", "#9CA3B4")))
        back.setIconSize(QSize(18, 18))
        back.clicked.connect(self.back_requested.emit)
        tlay.addWidget(back)
        tlay.addStretch()
        self._name_lbl = QLabel(self._peer_name)
        self._name_lbl.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 17px; font-weight: 600;")
        tlay.addWidget(self._name_lbl)
        self._dur_lbl = QLabel("00:00")
        self._dur_lbl.setStyleSheet(f"color: {palette().get('text_secondary','')}; font-size: 13px; font-family: 'Consolas',monospace;")
        tlay.addWidget(self._dur_lbl)
        dot = QLabel()
        dot.setFixedSize(6, 6)
        dot.setStyleSheet(f"background: {palette().get('primary','#2DD4A8')}; border-radius: 3px;")
        tlay.addWidget(dot)
        tlay.addStretch()
        q = QLabel()
        q.setPixmap(render_icon("signal", 18, palette().get("primary", "#2DD4A8")))
        tlay.addWidget(q)
        ql = QLabel("良好")
        ql.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")
        tlay.addWidget(ql)
        enc = QLabel()
        enc.setPixmap(render_icon("shield", 16, palette().get("text_muted", "#6B7280")))
        tlay.addWidget(enc)
        self._topbar = topbar

        # Self-view PiP
        self._self_video = _VideoLabel(root)
        self._self_video.setStyleSheet(f"border: 1px solid {palette().get('bg_hover','')}; border-radius: 12px; background: {palette().get('bg_secondary','')};")
        self._pip = QFrame(root)
        self._pip.setStyleSheet(self._pip_style())
        pip_lay = QVBoxLayout(self._pip)
        pip_lay.setContentsMargins(0, 0, 0, 0)
        self._self_video_lower = _VideoLabel(self._pip)
        pip_lay.addWidget(self._self_video_lower)
        self._pip_hint = QLabel("你的摄像头")
        self._pip_hint.setAlignment(Qt.AlignCenter)
        self._pip_hint.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px; padding: 4px;")
        pip_lay.addWidget(self._pip_hint)

        # Bottom control bar
        ctrl_wrap = QFrame(root)
        ctrl_wrap.setStyleSheet("background: transparent;")
        self._ctrl = QFrame(ctrl_wrap)
        self._ctrl.setStyleSheet(self._ctrl_style())
        clayout = QHBoxLayout(self._ctrl)
        clayout.setContentsMargins(28, 16, 28, 16)
        clayout.setSpacing(8)
        clayout.setAlignment(Qt.AlignCenter)

        self._btn_mic = self._make_ctrl_btn("mic", "麦克风", active=True)
        self._btn_mic.clicked.connect(self._toggle_mic)
        clayout.addWidget(self._btn_mic)

        self._btn_cam = self._make_ctrl_btn("video", "摄像头", active=True)
        self._btn_cam.clicked.connect(self._toggle_cam)
        clayout.addWidget(self._btn_cam)

        self._btn_share = self._make_ctrl_btn("monitor", "共享屏幕", active=False)
        self._btn_share.clicked.connect(self.screen_share_requested.emit)
        clayout.addWidget(self._btn_share)

        # Divider
        div = QFrame()
        div.setFixedSize(1, 56)
        div.setStyleSheet(f"background: {palette().get('bg_hover','')};")
        clayout.addWidget(div)

        # Hangup
        hang = self._make_ctrl_btn("phone-off", "结束通话", hangup=True)
        hang.clicked.connect(self.hangup_requested.emit)
        clayout.addWidget(hang)

        self._ctrl_wrap = ctrl_wrap
        self._layout_children()

    def _bg_style(self):
        return ("background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "stop:0 #1A1B1E, stop:0.4 #22242A, stop:0.7 #1E2228, stop:1 #1A1B1E);")

    def _pip_style(self):
        return (f"border: 1px solid {palette().get('bg_hover','')}; border-radius: 12px; "
                f"background: {palette().get('bg_secondary','')};")

    def _ctrl_style(self):
        return ("background: rgba(34,36,42,0.65); border-radius: 12px; "
                "border: 1px solid rgba(255,255,255,0.06);")

    def _make_ctrl_btn(self, icon: str, label: str, active: bool = False, hangup: bool = False) -> QPushButton:
        btn = QPushButton()
        btn.setCursor(Qt.PointingHandCursor)
        btn.setCheckable(False)
        col = palette().get("primary", "#2DD4A8") if active else palette().get("text_secondary", "#9CA3B4")
        if hangup:
            btn.setObjectName("HangupBtn")
            btn.setFixedSize(56, 56)
            btn.setIcon(render_icon_qicon(icon, 22, palette().get("bg_primary", "#0A1A14")))
        else:
            btn.setStyleSheet(self._ctrl_btn_style(active))
            btn.setFixedSize(64, 72)
            btn.setIcon(render_icon_qicon(icon, 22, col))
        btn.setIconSize(QSize(22, 22))
        btn.setToolTip(label)
        return btn

    def _ctrl_btn_style(self, active: bool):
        col = palette().get("primary", "#2DD4A8") if active else "transparent"
        return (f"QPushButton {{ background: {palette().get('bg_card_solid','#2A2D35')}; "
                f"border: 2px solid {col}; border-radius: 24px; }} "
                f"QPushButton:hover {{ background: {palette().get('bg_hover','')}; }}")

    def _layout_children(self):
        w, h = self.width(), self.height()
        # Placeholder & remote video fill
        self._remote_video.setGeometry(0, 0, w, h)
        self._placeholder.setGeometry(0, 0, w, h)
        self._topbar.setGeometry(0, 0, w, 64)
        # PiP bottom-right
        pip_w, pip_h = 240, 180
        self._pip.setGeometry(w - pip_w - 32, h - 120 - pip_h, pip_w, pip_h)
        # Control bar bottom-center
        self._ctrl.adjustSize()
        cw = self._ctrl.sizeHint().width()
        self._ctrl_wrap.setGeometry(0, 0, w, h)
        self._ctrl.move((w - cw) // 2, h - 120)

    def resizeEvent(self, e):
        super().resizeEvent(e)
        self._layout_children()

    # ---- Public API ----
    def set_local_user_id(self, uid: int):
        self._local_user_id = uid

    def set_camera_devices(self, devices):
        self._camera_devices = devices or []

    def set_peer_name(self, name: str):
        self._peer_name = name
        self._name_lbl.setText(name)
        self._ph_name.setText(name)

    def on_video_frame(self, sender_id, frame_bgr, width, height):
        if sender_id == self._local_user_id:
            self._self_video_lower.set_frame(frame_bgr, width, height)
        else:
            self._has_remote_video = True
            self._remote_video.set_frame(frame_bgr, width, height)
            self._placeholder.hide()

    def _toggle_mic(self):
        self._mic_muted = not self._mic_muted
        self._btn_mic.setIcon(render_icon_qicon("mic-off" if self._mic_muted else "mic", 22,
                                          palette().get("error", "#F87171") if self._mic_muted else palette().get("primary", "#2DD4A8")))
        self._btn_mic.setStyleSheet(self._ctrl_btn_style(not self._mic_muted))
        self.mic_mute_toggled.emit(self._mic_muted)

    def _toggle_cam(self):
        self._video_muted = not self._video_muted
        self._btn_cam.setIcon(render_icon_qicon("video-off" if self._video_muted else "video", 22,
                                          palette().get("error", "#F87171") if self._video_muted else palette().get("primary", "#2DD4A8")))
        self._btn_cam.setStyleSheet(self._ctrl_btn_style(not self._video_muted))
        self.video_mute_toggled.emit(self._video_muted)

    def _update_duration(self):
        elapsed = int(time.time() - self._call_start)
        m, s = divmod(elapsed, 60)
        self._dur_lbl.setText(f"{m:02d}:{s:02d}")

    def closeEvent(self, e):
        self._timer.stop()
        self.hangup_requested.emit()
        super().closeEvent(e)

    def refresh_theme(self):
        # Re-render static labels with new palette
        self._ph_name.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 20px; font-weight: 600;")
        self._ph_sub.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 13px;")
        self._name_lbl.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 17px; font-weight: 600;")
        self._dur_lbl.setStyleSheet(f"color: {palette().get('text_secondary','')}; font-size: 13px; font-family: 'Consolas',monospace;")
        self._pip.setStyleSheet(self._pip_style())
        self._ctrl.setStyleSheet(self._ctrl_style())
        self._layout_children()
