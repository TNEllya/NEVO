"""Connection bar for the NEVO client."""

from PyQt5.QtCore import QPoint, QSize, pyqtSignal, pyqtSlot
from PyQt5.QtWidgets import QHBoxLayout, QVBoxLayout, QFrame
from qfluentwidgets import (
    LineEdit, SpinBox, PrimaryPushButton, PushButton,
    CaptionLabel, FluentIcon, StrongBodyLabel,
    RoundMenu, Action,
)


class ConnectionBar(QFrame):
    """Bottom connection bar with address input, connect/disconnect, status."""

    connect_requested = pyqtSignal(str, int, str, str)  # host, port, username, password
    disconnect_requested = pyqtSignal()
    admin_action_requested = pyqtSignal(str)  # action: "login", "create_channel", "set_server_name"
    share_screen_requested = pyqtSignal()
    stop_screen_share_requested = pyqtSignal()
    audio_share_requested = pyqtSignal(str)  # source_type: "app" or "system"
    stop_audio_share_requested = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._connected = False
        self._admin_authenticated = False
        self._setup_ui()

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(12, 4, 12, 4)
        main_layout.setSpacing(2)

        # ── Row 1: Connection info ──
        row1 = QHBoxLayout()
        row1.setSpacing(6)

        self.status_dot = CaptionLabel("●")
        self.status_dot.setStyleSheet("color: #e74c3c; font-size: 16px;")
        row1.addWidget(self.status_dot)

        row1.addWidget(StrongBodyLabel(self.tr("Host:")))
        self.edit_host = LineEdit()
        self.edit_host.setText("127.0.0.1")
        self.edit_host.setFixedWidth(110)
        self.edit_host.setPlaceholderText("127.0.0.1")
        row1.addWidget(self.edit_host)

        row1.addWidget(StrongBodyLabel(self.tr("Port:")))
        self.spin_port = SpinBox()
        self.spin_port.setRange(1, 65535)
        self.spin_port.setValue(24430)
        self.spin_port.setFixedWidth(120)
        row1.addWidget(self.spin_port)

        row1.addWidget(StrongBodyLabel(self.tr("User:")))
        self.edit_username = LineEdit()
        self.edit_username.setFixedWidth(80)
        self.edit_username.setPlaceholderText(self.tr("Username"))
        row1.addWidget(self.edit_username)

        self.btn_connect = PrimaryPushButton(self.tr("Connect"))
        self.btn_connect.setIcon(FluentIcon.LINK)
        self.btn_connect.clicked.connect(self._on_connect)
        row1.addWidget(self.btn_connect)

        self.btn_disconnect = PushButton(self.tr("Disconnect"))
        self.btn_disconnect.setIcon(FluentIcon.CANCEL)
        self.btn_disconnect.setEnabled(False)
        self.btn_disconnect.clicked.connect(self.disconnect_requested.emit)
        row1.addWidget(self.btn_disconnect)

        row1.addStretch(1)

        self.lbl_latency = CaptionLabel(self.tr("Latency: --"))
        row1.addWidget(self.lbl_latency)

        main_layout.addLayout(row1)

        # ── Row 2: Action buttons ──
        row2 = QHBoxLayout()
        row2.setSpacing(6)

        self.btn_mute = PushButton(self.tr("闭麦"))
        self.btn_mute.setIcon(FluentIcon.MICROPHONE)
        self.btn_mute.setIconSize(QSize(16, 16))
        self.btn_mute.setCheckable(True)
        self.btn_mute.setEnabled(False)
        self.btn_mute.setFixedSize(100, 32)
        self.btn_mute.setStyleSheet(
            "QPushButton { "
            "  background-color: rgba(255, 255, 255, 0.08); "
            "  color: #dbdee1; "
            "  border: none; "
            "  border-radius: 6px; "
            "  padding-left: 20px; "
            "} "
            "QPushButton:hover { "
            "  background-color: rgba(255, 255, 255, 0.12); "
            "} "
            "QPushButton:checked { "
            "  background-color: rgba(255, 255, 255, 0.15); "
            "  color: #ffffff; "
            "} "
            "QPushButton:disabled { "
            "  background-color: rgba(255, 255, 255, 0.05); "
            "  color: #6d6f78; "
            "}"
        )
        row2.addWidget(self.btn_mute)

        self.btn_deafen = PushButton(self.tr("禁言"))
        self.btn_deafen.setIcon(FluentIcon.VOLUME)
        self.btn_deafen.setIconSize(QSize(16, 16))
        self.btn_deafen.setCheckable(True)
        self.btn_deafen.setEnabled(False)
        self.btn_deafen.setFixedSize(110, 32)
        self.btn_deafen.setStyleSheet(
            "QPushButton { "
            "  background-color: rgba(255, 255, 255, 0.08); "
            "  color: #dbdee1; "
            "  border: none; "
            "  border-radius: 6px; "
            "  padding-left: 20px; "
            "} "
            "QPushButton:hover { "
            "  background-color: rgba(255, 255, 255, 0.12); "
            "} "
            "QPushButton:checked { "
            "  background-color: rgba(255, 255, 255, 0.15); "
            "  color: #ffffff; "
            "} "
            "QPushButton:disabled { "
            "  background-color: rgba(255, 255, 255, 0.05); "
            "  color: #6d6f78; "
            "}"
        )
        row2.addWidget(self.btn_deafen)

        self.btn_share_screen = PushButton(self.tr("Share"))
        self.btn_share_screen.setIcon(FluentIcon.VIDEO)
        self.btn_share_screen.setIconSize(QSize(16, 16))
        self.btn_share_screen.setEnabled(False)
        self.btn_share_screen.setCheckable(True)
        self.btn_share_screen.setFixedSize(100, 32)
        self.btn_share_screen.setStyleSheet(
            "QPushButton { "
            "  background-color: rgba(255, 255, 255, 0.08); "
            "  color: #dbdee1; "
            "  border: none; "
            "  border-radius: 6px; "
            "  padding-left: 20px; "
            "} "
            "QPushButton:hover { "
            "  background-color: rgba(255, 255, 255, 0.12); "
            "} "
            "QPushButton:checked { "
            "  background-color: #c0392b; "
            "  color: #ffffff; "
            "} "
            "QPushButton:disabled { "
            "  background-color: rgba(255, 255, 255, 0.05); "
            "  color: #6d6f78; "
            "}"
        )
        self.btn_share_screen.toggled.connect(self._on_share_screen_toggled)
        row2.addWidget(self.btn_share_screen)

        self.btn_audio_share = PushButton(self.tr("Audio"))
        self.btn_audio_share.setIcon(FluentIcon.MICROPHONE)
        self.btn_audio_share.setIconSize(QSize(16, 16))
        self.btn_audio_share.setEnabled(False)
        self.btn_audio_share.setFixedSize(100, 32)
        self.btn_audio_share.setStyleSheet(
            "QPushButton { "
            "  background-color: rgba(255, 255, 255, 0.08); "
            "  color: #dbdee1; "
            "  border: none; "
            "  border-radius: 6px; "
            "  padding-left: 20px; "
            "} "
            "QPushButton:hover { "
            "  background-color: rgba(255, 255, 255, 0.12); "
            "} "
            "QPushButton:disabled { "
            "  background-color: rgba(255, 255, 255, 0.05); "
            "  color: #6d6f78; "
            "}"
        )
        self.btn_audio_share.clicked.connect(self._on_audio_share_click)
        row2.addWidget(self.btn_audio_share)

        row2.addStretch(1)

        self.btn_admin_login = PushButton(self.tr("Admin"))
        self.btn_admin_login.setIcon(FluentIcon.PEOPLE)
        self.btn_admin_login.setEnabled(False)
        self.btn_admin_login.clicked.connect(self._on_admin_click)
        row2.addWidget(self.btn_admin_login)

        main_layout.addLayout(row2)

    def _on_admin_click(self):
        if not self._admin_authenticated:
            self.admin_action_requested.emit("login")
        else:
            self._show_admin_menu()

    def _show_admin_menu(self):
        menu = RoundMenu(parent=self)

        action_create = Action(FluentIcon.ADD, self.tr("Create Channel"))
        action_create.triggered.connect(
            lambda: self.admin_action_requested.emit("create_channel")
        )
        menu.addAction(action_create)

        action_set_name = Action(FluentIcon.EDIT, self.tr("Set Server Name"))
        action_set_name.triggered.connect(
            lambda: self.admin_action_requested.emit("set_server_name")
        )
        menu.addAction(action_set_name)

        btn_rect = self.btn_admin_login.rect()
        top_left = self.btn_admin_login.mapToGlobal(btn_rect.topLeft())
        menu_height = menu.sizeHint().height()
        pos = self.btn_admin_login.mapToGlobal(QPoint(0, -menu_height))
        menu.exec_(pos)

    def set_admin_authenticated(self, authenticated: bool):
        self._admin_authenticated = authenticated

    def set_connected(self, connected: bool):
        self._connected = connected
        self.btn_connect.setEnabled(not connected)
        self.btn_disconnect.setEnabled(connected)
        self.edit_host.setEnabled(not connected)
        self.spin_port.setEnabled(not connected)
        self.edit_username.setEnabled(not connected)
        self.btn_mute.setEnabled(connected)
        self.btn_deafen.setEnabled(connected)
        self.btn_share_screen.setEnabled(connected)
        self.btn_audio_share.setEnabled(connected)

        if connected:
            self.status_dot.setStyleSheet("color: #2ecc71; font-size: 16px;")
        else:
            self.status_dot.setStyleSheet("color: #e74c3c; font-size: 16px;")
            self.lbl_latency.setText(self.tr("Latency: --"))
            self.btn_share_screen.setChecked(False)
            self.set_audio_sharing(False)
            self._admin_authenticated = False

    def _on_share_screen_toggled(self, checked):
        if checked:
            self.share_screen_requested.emit()
        else:
            self.stop_screen_share_requested.emit()

    def _on_audio_share_click(self):
        if self.btn_audio_share.property("sharing"):
            self.stop_audio_share_requested.emit()
        else:
            self._show_audio_share_menu()

    def _show_audio_share_menu(self):
        menu = RoundMenu(parent=self)

        action_app = Action(FluentIcon.VIDEO, self.tr("Application Audio"))
        action_app.triggered.connect(
            lambda: self.audio_share_requested.emit("app")
        )
        menu.addAction(action_app)

        action_system = Action(FluentIcon.SPEAKERS, self.tr("System Audio"))
        action_system.triggered.connect(
            lambda: self.audio_share_requested.emit("system")
        )
        menu.addAction(action_system)

        menu_height = menu.sizeHint().height()
        pos = self.btn_audio_share.mapToGlobal(QPoint(0, -menu_height))
        menu.exec_(pos)

    @pyqtSlot(bool)
    def set_audio_sharing(self, sharing):
        self.btn_audio_share.setProperty("sharing", sharing)
        if sharing:
            self.btn_audio_share.setText(self.tr("Stop"))
            self.btn_audio_share.setStyleSheet(
                "QPushButton { "
                "  background-color: #e67e22; "
                "  color: #ffffff; "
                "  border: none; "
                "  border-radius: 6px; "
                "  padding-left: 20px; "
                "} "
                "QPushButton:hover { "
                "  background-color: #d35400; "
                "}"
            )
        else:
            self.btn_audio_share.setText(self.tr("Audio"))
            self.btn_audio_share.setStyleSheet(
                "QPushButton { "
                "  background-color: rgba(255, 255, 255, 0.08); "
                "  color: #dbdee1; "
                "  border: none; "
                "  border-radius: 6px; "
                "  padding-left: 20px; "
                "} "
                "QPushButton:hover { "
                "  background-color: rgba(255, 255, 255, 0.12); "
                "} "
                "QPushButton:disabled { "
                "  background-color: rgba(255, 255, 255, 0.05); "
                "  color: #6d6f78; "
                "}"
            )

    @pyqtSlot(bool)
    def set_sharing(self, sharing):
        self.btn_share_screen.blockSignals(True)
        self.btn_share_screen.setChecked(sharing)
        self.btn_share_screen.setText(
            self.tr("Stop") if sharing else self.tr("Share")
        )
        self.btn_share_screen.blockSignals(False)

    def set_latency(self, ms: int):
        self.lbl_latency.setText(self.tr("Latency: {}ms").format(ms))

    def _on_connect(self):
        host = self.edit_host.text().strip() or "127.0.0.1"
        port = self.spin_port.value()
        username = self.edit_username.text().strip()
        if not username:
            username = "User"
        self.connect_requested.emit(host, port, username, "")
