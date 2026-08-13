"""来电弹窗，显示来电者信息并提供接听/拒绝按钮。"""

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import QVBoxLayout, QHBoxLayout, QLabel, QPushButton
from qfluentwidgets import Dialog, FluentIcon, StrongBodyLabel, SubtitleLabel


class IncomingCallDialog(Dialog):
    """一对一视频通话来电弹窗。"""

    accepted = pyqtSignal()   # 用户点击接听
    rejected = pyqtSignal()   # 用户点击拒绝

    def __init__(self, caller_name: str = "Unknown", parent=None):
        super().__init__(self.tr("Incoming Video Call"), "", parent)
        self.setWindowTitle(self.tr("Incoming Video Call"))

        self._caller_name = caller_name
        self._setup_ui()

    def _setup_ui(self):
        # 隐藏默认的 yes/cancel 按钮，使用自定义接听/拒绝按钮
        self.yesButton.hide()
        self.cancelButton.hide()

        layout = QVBoxLayout()
        layout.setSpacing(12)

        self.subtitle = SubtitleLabel(
            self.tr("{} is calling you...").format(self._caller_name)
        )
        self.subtitle.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.subtitle)

        info = StrongBodyLabel(self.tr("Would you like to answer the video call?"))
        info.setAlignment(Qt.AlignCenter)
        layout.addWidget(info)

        btn_layout = QHBoxLayout()
        btn_layout.setSpacing(16)

        self.btn_accept = QPushButton(self.tr("Answer"))
        self.btn_accept.setStyleSheet(
            "QPushButton {"
            "  background-color: #34D399;"
            "  color: white;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 8px 24px;"
            "  font-size: 14px;"
            "}"
            "QPushButton:hover { background-color: #2E9E5A; }"
        )
        self.btn_accept.setIcon(FluentIcon.ACCEPT.icon())
        self.btn_accept.clicked.connect(self._on_accept)
        btn_layout.addWidget(self.btn_accept)

        self.btn_reject = QPushButton(self.tr("Decline"))
        self.btn_reject.setStyleSheet(
            "QPushButton {"
            "  background-color: #F87171;"
            "  color: white;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 8px 24px;"
            "  font-size: 14px;"
            "}"
            "QPushButton:hover { background-color: #D63B3B; }"
        )
        self.btn_reject.setIcon(FluentIcon.CLOSE.icon())
        self.btn_reject.clicked.connect(self._on_reject)
        btn_layout.addWidget(self.btn_reject)

        layout.addLayout(btn_layout)
        self.contentLabel.hide()
        self.textLayout.insertLayout(self.textLayout.count(), layout)
        self.setFixedSize(360, 220)

    def _on_accept(self):
        self.accepted.emit()
        self.accept()

    def _on_reject(self):
        self.rejected.emit()
        self.reject()

    def closeEvent(self, event):
        # 关闭窗口视为拒绝
        self.rejected.emit()
        super().closeEvent(event)
