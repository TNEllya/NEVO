"""Configuration view - Server settings management."""

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QFrame, QGridLayout, QSpacerItem,
    QSizePolicy,
)
from qfluentwidgets import (
    HeaderCardWidget, TitleLabel, CaptionLabel, LineEdit,
    SpinBox, PrimaryPushButton, PushButton, ComboBox,
    SwitchButton, InfoBar, InfoBarPosition, FluentIcon,
    StrongBodyLabel, TextEdit, CardWidget,
)


class ConfigView(QFrame):
    """Server configuration page."""

    def __init__(self, server_proc, parent=None):
        super().__init__(parent)
        self.server = server_proc
        self.setObjectName("configView")

        self._setup_ui()
        self._load_config()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(36, 20, 36, 20)
        layout.setSpacing(16)

        # Title
        title = TitleLabel(self.tr("Configuration"))
        layout.addWidget(title)

        desc = CaptionLabel(self.tr("Manage server settings and SSL configuration"))
        layout.addWidget(desc)
        layout.addSpacing(8)

        # General settings card
        general_card = HeaderCardWidget(self)
        general_card.setTitle(self.tr("General Settings"))

        general_layout = QGridLayout()
        general_layout.setSpacing(12)
        general_layout.setContentsMargins(20, 8, 20, 16)
        general_layout.setColumnStretch(1, 1)  # Let input widgets stretch

        # Max users
        general_layout.addWidget(StrongBodyLabel(self.tr("Max Users")), 0, 0)
        self.spin_max_users = SpinBox()
        self.spin_max_users.setRange(1, 10000)
        self.spin_max_users.setValue(100)
        self.spin_max_users.setMinimumWidth(120)
        general_layout.addWidget(self.spin_max_users, 0, 1)

        # Welcome message
        general_layout.addWidget(StrongBodyLabel(self.tr("Welcome Message")), 1, 0)
        self.edit_welcome = LineEdit()
        self.edit_welcome.setPlaceholderText(self.tr("Welcome to the NEVO server!"))
        self.edit_welcome.setMinimumWidth(200)
        general_layout.addWidget(self.edit_welcome, 1, 1)

        # Server name
        general_layout.addWidget(StrongBodyLabel(self.tr("Server Name")), 2, 0)
        self.edit_server_name = LineEdit()
        self.edit_server_name.setPlaceholderText(self.tr("NEVO Server"))
        self.edit_server_name.setMinimumWidth(200)
        general_layout.addWidget(self.edit_server_name, 2, 1)

        # Log level
        general_layout.addWidget(StrongBodyLabel(self.tr("Log Level")), 3, 0)
        self.combo_log_level = ComboBox()
        self.combo_log_level.addItems(["trace", "debug", "info", "warn", "error"])
        self.combo_log_level.setCurrentIndex(2)
        self.combo_log_level.setMinimumWidth(120)
        general_layout.addWidget(self.combo_log_level, 3, 1)

        # Apply button
        btn_layout = QHBoxLayout()
        btn_layout.addStretch(1)
        self.btn_apply = PrimaryPushButton(self.tr("Apply"))
        self.btn_apply.setIcon(FluentIcon.ACCEPT)
        self.btn_apply.clicked.connect(self._on_apply)
        btn_layout.addWidget(self.btn_apply)
        general_layout.addLayout(btn_layout, 4, 0, 1, 2)

        general_card.viewLayout.addLayout(general_layout)
        layout.addWidget(general_card)

        # SSL/TLS card
        ssl_card = HeaderCardWidget(self)
        ssl_card.setTitle(self.tr("SSL/TLS Configuration"))

        ssl_layout = QGridLayout()
        ssl_layout.setSpacing(12)
        ssl_layout.setContentsMargins(20, 8, 20, 16)
        ssl_layout.setColumnStretch(1, 1)  # Let input widgets stretch

        # SSL enabled switch
        ssl_layout.addWidget(StrongBodyLabel(self.tr("Enable SSL/TLS")), 0, 0)
        self.switch_ssl = SwitchButton()
        ssl_layout.addWidget(self.switch_ssl, 0, 1)

        # Certificate file
        ssl_layout.addWidget(StrongBodyLabel(self.tr("Certificate File")), 1, 0)
        self.edit_cert = LineEdit()
        self.edit_cert.setPlaceholderText(self.tr("Path to PEM certificate file"))
        self.edit_cert.setMinimumWidth(200)
        ssl_layout.addWidget(self.edit_cert, 1, 1)

        # Private key file
        ssl_layout.addWidget(StrongBodyLabel(self.tr("Private Key File")), 2, 0)
        self.edit_key = LineEdit()
        self.edit_key.setPlaceholderText(self.tr("Path to PEM private key file"))
        self.edit_key.setMinimumWidth(200)
        ssl_layout.addWidget(self.edit_key, 2, 1)

        # CA file
        ssl_layout.addWidget(StrongBodyLabel(self.tr("CA File (optional)")), 3, 0)
        self.edit_ca = LineEdit()
        self.edit_ca.setPlaceholderText(self.tr("Path to CA certificate for mTLS"))
        self.edit_ca.setMinimumWidth(200)
        ssl_layout.addWidget(self.edit_ca, 3, 1)

        # Apply SSL button
        ssl_btn_layout = QHBoxLayout()
        ssl_btn_layout.addStretch(1)
        self.btn_apply_ssl = PrimaryPushButton(self.tr("Apply SSL Config"))
        self.btn_apply_ssl.setIcon(FluentIcon.CERTIFICATE)
        self.btn_apply_ssl.clicked.connect(self._on_apply_ssl)
        ssl_btn_layout.addWidget(self.btn_apply_ssl)
        ssl_layout.addLayout(ssl_btn_layout, 4, 0, 1, 2)

        ssl_card.viewLayout.addLayout(ssl_layout)
        layout.addWidget(ssl_card)

        # Admin password card
        admin_card = HeaderCardWidget(self)
        admin_card.setTitle(self.tr("Admin Password"))

        admin_layout = QVBoxLayout()
        admin_layout.setContentsMargins(20, 8, 20, 16)
        admin_layout.setSpacing(8)

        admin_desc = CaptionLabel(
            self.tr("Set the administrator password. "
                     "Clients who enter this password can manage the server (create channels, change server name).")
        )
        admin_desc.setWordWrap(True)
        admin_layout.addWidget(admin_desc)

        pwd_row = QHBoxLayout()
        pwd_row.addWidget(StrongBodyLabel(self.tr("Password:")))
        self.edit_admin_pwd = LineEdit()
        self.edit_admin_pwd.setEchoMode(LineEdit.Password)
        self.edit_admin_pwd.setPlaceholderText(self.tr("Enter admin password"))
        self.edit_admin_pwd.setMinimumWidth(200)
        pwd_row.addWidget(self.edit_admin_pwd)
        admin_layout.addLayout(pwd_row)

        admin_btn_row = QHBoxLayout()
        admin_btn_row.addStretch(1)
        self.btn_set_pwd = PrimaryPushButton(self.tr("Set Password"))
        self.btn_set_pwd.setIcon(FluentIcon.FINGERPRINT)
        self.btn_set_pwd.clicked.connect(self._on_set_admin_password)
        admin_btn_row.addWidget(self.btn_set_pwd)

        self.lbl_pwd_status = CaptionLabel("")
        admin_btn_row.addWidget(self.lbl_pwd_status)

        admin_layout.addLayout(admin_btn_row)
        admin_card.viewLayout.addLayout(admin_layout)
        layout.addWidget(admin_card)

        layout.addStretch(1)

    def _load_config(self):
        """Load current config from server."""
        if not self.server.ipc_connected:
            return

        try:
            config = self.server.get_config()
            self.spin_max_users.setValue(int(config.get("max_users", 100)))
            self.edit_welcome.setText(config.get("welcome_message", ""))
            self.edit_server_name.setText(config.get("server_name", ""))

            log_level = config.get("log_level", "info")
            idx = self.combo_log_level.findText(log_level)
            if idx >= 0:
                self.combo_log_level.setCurrentIndex(idx)

            self.switch_ssl.setChecked(config.get("ssl_enabled", False))
            admin_pwd = config.get("admin_password", "")
            if admin_pwd:
                self.edit_admin_pwd.setText("••••••••")
                self.lbl_pwd_status.setText(self.tr("Password is set"))
        except Exception:
            pass

    def _on_apply(self):
        if not self.server.ipc_connected:
            InfoBar.warning(
                self.tr("Server Not Running"),
                self.tr("Please start the server first."),
                parent=self.window(), position=InfoBarPosition.TOP,
            )
            return

        try:
            result = self.server.set_config(
                max_users=self.spin_max_users.value(),
                welcome_message=self.edit_welcome.text().strip(),
                log_level=self.combo_log_level.currentText(),
                server_name=self.edit_server_name.text().strip(),
            )
            if result.get("updated"):
                InfoBar.success(
                    self.tr("Applied"),
                    self.tr("Configuration has been updated."),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=3000,
                )
            else:
                InfoBar.warning(
                    self.tr("No Changes"),
                    result.get("message", self.tr("No configuration keys were changed.")),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=3000,
                )
        except Exception as e:
            InfoBar.error(
                self.tr("Error"), str(e),
                parent=self.window(), position=InfoBarPosition.TOP,
            )

    def _on_apply_ssl(self):
        if not self.server.ipc_connected:
            InfoBar.warning(
                self.tr("Server Not Running"),
                self.tr("Please start the server first."),
                parent=self.window(), position=InfoBarPosition.TOP,
            )
            return

        try:
            result = self.server.configure_ssl(
                enabled=self.switch_ssl.isChecked(),
                cert_file=self.edit_cert.text().strip(),
                key_file=self.edit_key.text().strip(),
                ca_file=self.edit_ca.text().strip(),
            )
            if result.get("success"):
                InfoBar.success(
                    self.tr("SSL Configured"),
                    result.get("message", self.tr("SSL configuration applied.")),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=5000,
                )
            else:
                InfoBar.warning(
                    self.tr("Failed"),
                    result.get("message", self.tr("SSL configuration failed.")),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=5000,
                )
        except Exception as e:
            InfoBar.error(
                self.tr("Error"), str(e),
                parent=self.window(), position=InfoBarPosition.TOP,
            )

    def _on_set_admin_password(self):
        if not self.server.ipc_connected:
            InfoBar.warning(
                self.tr("Server Not Running"),
                self.tr("Please start the server first."),
                parent=self.window(), position=InfoBarPosition.TOP,
            )
            return

        pwd = self.edit_admin_pwd.text().strip()
        if not pwd:
            InfoBar.warning(
                self.tr("Empty Password"),
                self.tr("Please enter an admin password."),
                parent=self.window(), position=InfoBarPosition.TOP,
                duration=3000,
            )
            return

        try:
            result = self.server.set_config(admin_password=pwd)
            if result.get("updated") or result.get("success"):
                self.edit_admin_pwd.setText("••••••••")
                self.lbl_pwd_status.setText(self.tr("Password is set"))
                InfoBar.success(
                    self.tr("Password Set"),
                    self.tr("Admin password has been updated."),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=3000,
                )
            else:
                InfoBar.warning(
                    self.tr("Failed"),
                    result.get("message", self.tr("Could not set admin password.")),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=5000,
                )
        except Exception as e:
            InfoBar.error(
                self.tr("Error"), str(e),
                parent=self.window(), position=InfoBarPosition.TOP,
            )
