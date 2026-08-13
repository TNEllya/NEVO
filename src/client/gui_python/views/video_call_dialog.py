"""视频通话中窗口，显示本地预览和远端视频，并提供挂断、静音视频、切换摄像头。"""

import numpy as np

from PyQt5.QtCore import Qt, pyqtSignal, QTimer
from PyQt5.QtGui import QImage, QPixmap
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QComboBox,
    QWidget, QSplitter, QSizePolicy,
)
from qfluentwidgets import (
    Dialog, FluentIcon, StrongBodyLabel, CaptionLabel,
)


class VideoCallDialog(Dialog):
    """一对一视频通话窗口。"""

    hangup_requested = pyqtSignal()          # 请求挂断
    video_mute_toggled = pyqtSignal(bool)    # 本地视频暂停/恢复
    camera_changed = pyqtSignal(int)         # 切换摄像头

    def __init__(self, peer_name: str = "Peer", parent=None):
        super().__init__(self.tr("Video Call"), "", parent)
        self.setWindowTitle(self.tr("Video Call - {}").format(peer_name))
        self._peer_name = peer_name
        self._video_muted = False
        self._camera_devices = []
        self._setup_ui()

        # 定时刷新 UI（帧由外部通过 on_video_frame 传入）
        self._local_frame = None
        self._remote_frame = None
        self._update_timer = QTimer(self)
        self._update_timer.timeout.connect(self._update_frames)
        self._update_timer.start(33)  # ~30fps

    def _setup_ui(self):
        # 隐藏默认按钮
        self.yesButton.hide()
        self.cancelButton.hide()

        layout = QVBoxLayout()
        layout.setSpacing(10)

        self.title_label = StrongBodyLabel(
            self.tr("In call with {}").format(self._peer_name)
        )
        layout.addWidget(self.title_label)

        # 视频区域：使用 QSplitter 分隔本地/远端
        self.splitter = QSplitter(Qt.Horizontal)

        self.local_container = self._create_video_container(self.tr("Local"))
        self.remote_container = self._create_video_container(self.tr("Remote"))

        self.splitter.addWidget(self.local_container)
        self.splitter.addWidget(self.remote_container)
        self.splitter.setSizes([400, 400])
        self.splitter.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        layout.addWidget(self.splitter, 1)

        # 控制栏
        ctrl_layout = QHBoxLayout()
        ctrl_layout.setSpacing(12)

        self.btn_mute_video = QPushButton(self.tr("Mute Video"))
        self.btn_mute_video.setCheckable(True)
        self.btn_mute_video.setIcon(FluentIcon.VIDEO.icon())
        from theme_manager import ThemeManager
        tm = ThemeManager.instance()
        pal = tm.palette()
        self.btn_mute_video.setStyleSheet(
            f"QPushButton {{"
            f"  background-color: {pal['primary']};"
            f"  color: white;"
            f"  border: none;"
            f"  border-radius: 6px;"
            f"  padding: 6px 16px;"
            f"}}"
            f"QPushButton:checked {{ background-color: {pal['error']}; }}"
            f"QPushButton:hover {{ background-color: {pal['primary_hover']}; }}"
        )
        self.btn_mute_video.toggled.connect(self._on_mute_video_toggled)
        ctrl_layout.addWidget(self.btn_mute_video)

        self.combo_camera = QComboBox()
        self.combo_camera.setMinimumWidth(160)
        self.combo_camera.setEnabled(False)
        self.combo_camera.currentIndexChanged.connect(self._on_camera_changed)
        ctrl_layout.addWidget(self.combo_camera)

        ctrl_layout.addStretch(1)

        self.btn_hangup = QPushButton(self.tr("Hang Up"))
        self.btn_hangup.setIcon(FluentIcon.CANCEL.icon())
        self.btn_hangup.setStyleSheet(
            f"QPushButton {{"
            f"  background-color: {pal['error']};"
            f"  color: white;"
            f"  border: none;"
            f"  border-radius: 6px;"
            f"  padding: 6px 24px;"
            f"  font-size: 14px;"
            f"}}"
            f"QPushButton:hover {{ background-color: {pal['error_hover']}; }}"
        )
        self.btn_hangup.clicked.connect(self._on_hangup)
        ctrl_layout.addWidget(self.btn_hangup)

        layout.addLayout(ctrl_layout)
        self.contentLabel.hide()
        self.textLayout.insertLayout(self.textLayout.count(), layout)
        self.resize(840, 560)

    def _create_video_container(self, label_text: str) -> QWidget:
        container = QWidget()
        container.setStyleSheet("background-color: #1e1f22; border-radius: 8px;")
        layout = QVBoxLayout(container)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)

        label = QLabel()
        label.setAlignment(Qt.AlignCenter)
        label.setStyleSheet("background-color: #2b2d31; border-radius: 6px;")
        label.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        layout.addWidget(label, 1)

        caption = CaptionLabel(label_text)
        caption.setAlignment(Qt.AlignCenter)
        caption.setStyleSheet("color: #a0a0a0;")
        layout.addWidget(caption)

        if label_text == self.tr("Local"):
            self.local_video = label
        else:
            self.remote_video = label
        return container

    def set_camera_devices(self, devices):
        """设置可选摄像头列表，devices 为 (index, name) 列表。"""
        self._camera_devices = devices or []
        self.combo_camera.clear()
        for idx, name in devices:
            self.combo_camera.addItem(name, idx)
        self.combo_camera.setEnabled(len(devices) > 1)

    def on_video_frame(self, sender_id: int, frame_bgr: np.ndarray, width: int, height: int):
        """由外部媒体引擎回调，更新本地或远端画面。"""
        if frame_bgr is None or frame_bgr.size == 0:
            return
        pixmap = self._bgr_to_pixmap(frame_bgr, width, height)
        if pixmap is None:
            return
        # 本地预览使用 sender_id == self user_id，远端为其他 sender_id
        # 由于外部调用时已区分，这里简单判断：如果 frame 尺寸和本地一致则视为本地
        if sender_id == getattr(self, "_local_user_id", 0):
            self._local_frame = pixmap
        else:
            self._remote_frame = pixmap

    def set_local_user_id(self, user_id: int):
        """设置本地用户 ID，用于区分本地/远端画面。"""
        self._local_user_id = user_id

    def _update_frames(self):
        """定时刷新 QLabel 画面。"""
        if self._local_frame:
            self._set_pixmap_scaled(self.local_video, self._local_frame)
        if self._remote_frame:
            self._set_pixmap_scaled(self.remote_video, self._remote_frame)

    def _set_pixmap_scaled(self, label: QLabel, pixmap: QPixmap):
        label_size = label.size()
        if label_size.width() <= 0 or label_size.height() <= 0:
            return
        scaled = pixmap.scaled(
            label_size.width(), label_size.height(),
            Qt.KeepAspectRatio, Qt.SmoothTransformation
        )
        label.setPixmap(scaled)

    @staticmethod
    def _bgr_to_pixmap(frame_bgr: np.ndarray, width: int, height: int) -> QPixmap:
        try:
            if frame_bgr.shape[1] != width or frame_bgr.shape[0] != height:
                frame_bgr = frame_bgr[:height, :width]
            h, w = frame_bgr.shape[:2]
            bytes_per_line = 3 * w
            image = QImage(frame_bgr.data, w, h, bytes_per_line, QImage.Format_RGB888)
            image = image.rgbSwapped()  # BGR -> RGB
            return QPixmap.fromImage(image)
        except Exception:
            return None

    def _on_mute_video_toggled(self, checked: bool):
        self._video_muted = checked
        self.btn_mute_video.setText(
            self.tr("Unmute Video") if checked else self.tr("Mute Video")
        )
        self.video_mute_toggled.emit(checked)

    def _on_camera_changed(self, index: int):
        if index < 0 or index >= len(self._camera_devices):
            return
        device_index = self._camera_devices[index][0]
        self.camera_changed.emit(device_index)

    def _on_hangup(self):
        self.hangup_requested.emit()
        self.reject()

    def closeEvent(self, event):
        self.hangup_requested.emit()
        self._update_timer.stop()
        super().closeEvent(event)
