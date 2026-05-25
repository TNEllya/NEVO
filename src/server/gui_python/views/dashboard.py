"""Dashboard view - Server control and status overview with key metrics."""

import os

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtWidgets import QVBoxLayout, QHBoxLayout, QFrame, QFileDialog, QGridLayout
from qfluentwidgets import (
    CardWidget, HeaderCardWidget, CaptionLabel, SubtitleLabel,
    TitleLabel, BodyLabel, IconWidget, FluentIcon, ProgressRing,
    setFont, StrongBodyLabel, PushButton, PrimaryPushButton,
    LineEdit, SpinBox, CompactSpinBox, InfoBar, InfoBarPosition,
)


class StatCard(CardWidget):
    """A card displaying a single statistic with icon, label, and value."""

    def __init__(self, title: str, value: str, icon: FluentIcon = None, parent=None):
        super().__init__(parent)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(6)

        # Top row: icon + title (horizontal)
        top = QHBoxLayout()
        top.setSpacing(6)

        if icon:
            icon_w = IconWidget(icon)
            icon_w.setFixedSize(18, 18)
            top.addWidget(icon_w)

        title_label = CaptionLabel(title)
        title_label.setTextColor("#787878", "#787878")
        top.addWidget(title_label)
        top.addStretch(1)
        layout.addLayout(top)

        # Value
        self._value_label = TitleLabel(value)
        layout.addWidget(self._value_label)

    def set_value(self, value: str):
        self._value_label.setText(value)


class DashboardView(QFrame):
    """Dashboard page showing server control and status overview."""

    def __init__(self, server_proc, parent=None):
        super().__init__(parent)
        self.server = server_proc
        self.setObjectName("dashboardView")

        self._setup_ui()

        # Monitor state changes
        self.server.set_on_server_running_changed(self._on_server_running_changed)
        self.server.set_on_ipc_connected_changed(self._on_ipc_connected_changed)

        # Auto-refresh timer
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh)
        self._timer.start(2000)
        self._refresh()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(36, 20, 36, 20)
        layout.setSpacing(16)

        # Title
        title = TitleLabel(self.tr("Dashboard"))
        layout.addWidget(title)

        desc = CaptionLabel(self.tr("Server control and status overview"))
        layout.addWidget(desc)
        layout.addSpacing(8)

        # Server control card
        ctrl_card = CardWidget(self)
        ctrl_layout = QVBoxLayout(ctrl_card)
        ctrl_layout.setContentsMargins(20, 16, 20, 16)
        ctrl_layout.setSpacing(12)

        # Card title
        ctrl_title = SubtitleLabel(self.tr("Server Control"))
        ctrl_layout.addWidget(ctrl_title)
        ctrl_layout.addSpacing(8)

        # Executable path row
        exe_row = QHBoxLayout()
        exe_row.setSpacing(12)
        exe_row.setContentsMargins(0, 0, 0, 0)
        
        lbl_exe = StrongBodyLabel(self.tr("Server Executable:"))
        lbl_exe.setFixedWidth(130)
        exe_row.addWidget(lbl_exe, 0)
        
        self.edit_exe = LineEdit()
        self.edit_exe.setPlaceholderText(self.tr("Path to nevo_server executable"))
        self.edit_exe.setText(self.server.exe_path)
        self.edit_exe.setClearButtonEnabled(True)
        exe_row.addWidget(self.edit_exe, 1)
        
        self.btn_browse = PushButton(self.tr("Browse"))
        self.btn_browse.setIcon(FluentIcon.FOLDER)
        self.btn_browse.setFixedWidth(100)
        self.btn_browse.clicked.connect(self._on_browse_exe)
        exe_row.addWidget(self.btn_browse, 0)
        ctrl_layout.addLayout(exe_row)

        # Show auto-detect status
        if self.server.exe_path:
            self.lbl_exe_hint = CaptionLabel(
                self.tr("Auto-detected: {}").format(self.server.exe_path)
            )
            self.lbl_exe_hint.setTextColor("#16A34A", "#16A34A")
        else:
            self.lbl_exe_hint = CaptionLabel(
                self.tr("Server executable not found. Please browse to select it.")
            )
            self.lbl_exe_hint.setTextColor("#DC2626", "#DC2626")
        ctrl_layout.addWidget(self.lbl_exe_hint)
        ctrl_layout.addSpacing(8)

        # Port configuration row
        port_row = QHBoxLayout()
        port_row.setSpacing(24)
        port_row.setContentsMargins(0, 0, 0, 0)

        # TCP Port
        tcp_group = QHBoxLayout()
        tcp_group.setSpacing(8)
        tcp_group.setContentsMargins(0, 0, 0, 0)
        tcp_lbl = StrongBodyLabel(self.tr("TCP Port:"))
        tcp_lbl.setFixedWidth(80)
        tcp_group.addWidget(tcp_lbl, 0)
        self.spin_tcp_port = SpinBox()
        self.spin_tcp_port.setRange(1, 65535)
        self.spin_tcp_port.setValue(self.server.tcp_port)
        self.spin_tcp_port.setMinimumWidth(120)
        tcp_group.addWidget(self.spin_tcp_port, 0)
        port_row.addLayout(tcp_group, 0)

        # UDP Port
        udp_group = QHBoxLayout()
        udp_group.setSpacing(8)
        udp_group.setContentsMargins(0, 0, 0, 0)
        udp_lbl = StrongBodyLabel(self.tr("UDP Port:"))
        udp_lbl.setFixedWidth(80)
        udp_group.addWidget(udp_lbl, 0)
        self.spin_udp_port = SpinBox()
        self.spin_udp_port.setRange(1, 65535)
        self.spin_udp_port.setValue(self.server.udp_port)
        self.spin_udp_port.setMinimumWidth(120)
        udp_group.addWidget(self.spin_udp_port, 0)
        port_row.addLayout(udp_group, 0)

        # DB Path
        db_group = QHBoxLayout()
        db_group.setSpacing(8)
        db_group.setContentsMargins(0, 0, 0, 0)
        db_lbl = StrongBodyLabel(self.tr("DB Path:"))
        db_lbl.setFixedWidth(70)
        db_group.addWidget(db_lbl, 0)
        self.edit_db = LineEdit()
        self.edit_db.setText(self.server.db_path)
        self.edit_db.setPlaceholderText("nevo_server.db")
        self.edit_db.setFixedWidth(200)
        db_group.addWidget(self.edit_db, 1)
        port_row.addLayout(db_group, 1)

        port_row.addStretch(1)
        ctrl_layout.addLayout(port_row)
        ctrl_layout.addSpacing(12)

        # Start/Stop/Restart row
        btn_row = QHBoxLayout()
        btn_row.setSpacing(12)
        btn_row.setContentsMargins(0, 0, 0, 0)

        # Status indicator light
        self.lbl_status_light = CaptionLabel("\u25CF")
        self.lbl_status_light.setStyleSheet(
            "font-size: 18px; color: #DC2626; font-weight: bold;"
        )
        self.lbl_status_light.setFixedWidth(20)
        btn_row.addWidget(self.lbl_status_light, 0)

        self.btn_start = PrimaryPushButton(self.tr("Start Server"))
        self.btn_start.setIcon(FluentIcon.PLAY)
        self.btn_start.clicked.connect(self._on_start_server)
        self.btn_start.setFixedWidth(130)
        btn_row.addWidget(self.btn_start, 0)

        self.btn_stop = PushButton(self.tr("Stop Server"))
        self.btn_stop.setIcon(FluentIcon.CANCEL)
        self.btn_stop.clicked.connect(self._on_stop_server)
        self.btn_stop.setEnabled(False)
        self.btn_stop.setFixedWidth(120)
        btn_row.addWidget(self.btn_stop, 0)

        self.btn_restart = PushButton(self.tr("Restart Server"))
        self.btn_restart.setIcon(FluentIcon.SYNC)
        self.btn_restart.clicked.connect(self._on_restart_server)
        self.btn_restart.setEnabled(False)
        self.btn_restart.setFixedWidth(130)
        btn_row.addWidget(self.btn_restart, 0)

        btn_row.addStretch(1)

        self.lbl_server_status = CaptionLabel(self.tr("Server Stopped"))
        self.lbl_server_status.setFixedWidth(100)
        btn_row.addWidget(self.lbl_server_status, 0)

        self.lbl_ipc_status = CaptionLabel(self.tr("IPC: Disconnected"))
        self.lbl_ipc_status.setFixedWidth(130)
        btn_row.addWidget(self.lbl_ipc_status, 0)

        ctrl_layout.addLayout(btn_row)

        layout.addWidget(ctrl_card)

        # Status card row
        status_card = CardWidget(self)
        status_layout_outer = QVBoxLayout(status_card)
        status_layout_outer.setContentsMargins(20, 16, 20, 16)
        status_layout_outer.setSpacing(12)

        status_title = SubtitleLabel(self.tr("Server Status"))
        status_layout_outer.addWidget(status_title)
        status_layout_outer.addSpacing(8)

        status_grid = QGridLayout()
        status_grid.setSpacing(12)
        status_grid.setContentsMargins(0, 0, 0, 0)
        for col in range(4):
            status_grid.setColumnStretch(col, 1)

        self.card_running = StatCard(
            self.tr("Status"), self.tr("Stopped"),
            FluentIcon.POWER_BUTTON
        )
        self.card_uptime = StatCard(
            self.tr("Uptime"), "00:00:00",
            FluentIcon.STOP_WATCH
        )
        self.card_clients = StatCard(
            self.tr("Clients"), "0",
            FluentIcon.PEOPLE
        )
        self.card_channels = StatCard(
            self.tr("Channels"), "0",
            FluentIcon.FOLDER
        )

        status_grid.addWidget(self.card_running, 0, 0)
        status_grid.addWidget(self.card_uptime, 0, 1)
        status_grid.addWidget(self.card_clients, 0, 2)
        status_grid.addWidget(self.card_channels, 0, 3)

        status_layout_outer.addLayout(status_grid)
        layout.addWidget(status_card)

        # Traffic card row
        traffic_card = CardWidget(self)
        traffic_layout_outer = QVBoxLayout(traffic_card)
        traffic_layout_outer.setContentsMargins(20, 16, 20, 16)
        traffic_layout_outer.setSpacing(12)

        traffic_title = SubtitleLabel(self.tr("Traffic"))
        traffic_layout_outer.addWidget(traffic_title)
        traffic_layout_outer.addSpacing(8)

        traffic_grid = QGridLayout()
        traffic_grid.setSpacing(12)
        traffic_grid.setContentsMargins(0, 0, 0, 0)
        traffic_grid.setColumnStretch(0, 1)
        traffic_grid.setColumnStretch(1, 1)

        self.card_relayed = StatCard(
            self.tr("Packets Relayed"), "0",
            FluentIcon.SEND
        )
        self.card_dropped = StatCard(
            self.tr("Packets Dropped"), "0",
            FluentIcon.CANCEL
        )

        traffic_grid.addWidget(self.card_relayed, 0, 0)
        traffic_grid.addWidget(self.card_dropped, 0, 1)

        traffic_layout_outer.addLayout(traffic_grid)
        layout.addWidget(traffic_card)

        # Connection info card
        info_card = CardWidget(self)
        info_layout_outer = QVBoxLayout(info_card)
        info_layout_outer.setContentsMargins(20, 16, 20, 16)
        info_layout_outer.setSpacing(12)

        info_title = SubtitleLabel(self.tr("Connection Info"))
        info_layout_outer.addWidget(info_title)
        info_layout_outer.addSpacing(8)

        self.lbl_tcp_port = StrongBodyLabel(self.tr("TCP Port: --"))
        self.lbl_udp_port = StrongBodyLabel(self.tr("UDP Port: --"))
        self.lbl_ssl = StrongBodyLabel(self.tr("SSL/TLS: --"))

        info_layout_outer.addWidget(self.lbl_tcp_port)
        info_layout_outer.addWidget(self.lbl_udp_port)
        info_layout_outer.addWidget(self.lbl_ssl)

        layout.addWidget(info_card)

        layout.addStretch(1)

    def _on_browse_exe(self):
        path, _ = QFileDialog.getOpenFileName(
            self, self.tr("Select Server Executable"), "",
            self.tr("Executables (*.exe);;All Files (*)")
        )
        if path:
            self.edit_exe.setText(path)

    def _on_start_server(self):
        # Save settings to server process
        exe_path = self.edit_exe.text().strip()
        if not exe_path:
            InfoBar.warning(
                self.tr("No Executable"),
                self.tr("Please specify the server executable path."),
                parent=self.window(),
                position=InfoBarPosition.TOP,
                duration=3000,
            )
            return

        if not os.path.isfile(exe_path):
            InfoBar.error(
                self.tr("File Not Found"),
                self.tr("Server executable does not exist: {}").format(exe_path),
                parent=self.window(),
                position=InfoBarPosition.TOP,
                duration=5000,
            )
            return

        self.server.exe_path = exe_path
        self.server.tcp_port = self.spin_tcp_port.value()
        self.server.udp_port = self.spin_udp_port.value()
        self.server.db_path = self.edit_db.text().strip() or "nevo_server.db"

        self.btn_start.setEnabled(False)
        self.lbl_server_status.setText(self.tr("Starting..."))

        if self.server.start_server():
            pass  # callbacks will update UI
        else:
            self.btn_start.setEnabled(True)
            self.lbl_server_status.setText(self.tr("Start Failed"))
            InfoBar.error(
                self.tr("Start Failed"),
                self.tr("Could not start the server. Check the executable path."),
                parent=self.window(),
                position=InfoBarPosition.TOP,
                duration=5000,
            )

    def _on_stop_server(self):
        self.btn_stop.setEnabled(False)
        self.btn_restart.setEnabled(False)
        self.lbl_server_status.setText(self.tr("Stopping..."))
        self.server.stop_server()

    def _on_restart_server(self):
        """Stop and then start the server again."""
        self.btn_start.setEnabled(False)
        self.btn_stop.setEnabled(False)
        self.btn_restart.setEnabled(False)
        self.lbl_server_status.setText(self.tr("Restarting..."))
        self._pending_restart = True
        self.server.stop_server()

    def _on_server_running_changed(self, running: bool):
        self.btn_start.setEnabled(not running)
        self.btn_stop.setEnabled(running)
        self.btn_restart.setEnabled(running)
        # Disable config edits while running
        self.edit_exe.setEnabled(not running)
        self.btn_browse.setEnabled(not running)
        self.spin_tcp_port.setEnabled(not running)
        self.spin_udp_port.setEnabled(not running)
        self.edit_db.setEnabled(not running)

        if running:
            self.lbl_server_status.setText(self.tr("Server Running"))
            self.card_running.set_value(self.tr("Running"))
            self.lbl_status_light.setStyleSheet(
                "font-size: 18px; color: #16A34A; font-weight: bold;"
            )
        else:
            self.lbl_server_status.setText(self.tr("Server Stopped"))
            self.card_running.set_value(self.tr("Stopped"))
            self.lbl_status_light.setStyleSheet(
                "font-size: 18px; color: #DC2626; font-weight: bold;"
            )
            # If a restart was pending, start the server again
            if getattr(self, "_pending_restart", False):
                self._pending_restart = False
                QTimer.singleShot(500, self._do_restart_start)

    def _do_restart_start(self):
        """Actually start the server after a restart delay."""
        if not self.server.start_server():
            self.btn_start.setEnabled(True)
            self.lbl_server_status.setText(self.tr("Start Failed"))
            InfoBar.error(
                self.tr("Start Failed"),
                self.tr("Could not start the server. Check the executable path."),
                parent=self.window(),
                position=InfoBarPosition.TOP,
                duration=5000,
            )

    def _on_ipc_connected_changed(self, connected: bool):
        if connected:
            self.lbl_ipc_status.setText(self.tr("IPC: Connected"))
        else:
            self.lbl_ipc_status.setText(self.tr("IPC: Disconnected"))

    def _refresh(self):
        """Refresh data from the server via IPC."""
        if not self.server.ipc_connected:
            if self.server.server_running:
                self.card_running.set_value(self.tr("Starting"))
            else:
                self.card_running.set_value(self.tr("Stopped"))
            return

        try:
            status = self.server.get_status()
            config = self.server.get_config()

            # Status
            is_running = status.get("running", False)
            if is_running:
                self.card_running.set_value(self.tr("Running"))
            else:
                self.card_running.set_value(self.tr("Stopped"))

            # Uptime
            uptime_ms = status.get("uptime_ms", 0)
            uptime_s = int(uptime_ms / 1000)
            hours, remainder = divmod(uptime_s, 3600)
            minutes, seconds = divmod(remainder, 60)
            self.card_uptime.set_value(f"{hours:02d}:{minutes:02d}:{seconds:02d}")

            # Clients & channels
            self.card_clients.set_value(str(status.get("clients", 0)))
            self.card_channels.set_value(str(status.get("channels", 0)))

            # Traffic
            self.card_relayed.set_value(self._format_number(status.get("packets_relayed", 0)))

            # Connection info
            self.lbl_tcp_port.setText(self.tr("TCP Port: {}").format(int(config.get("tcp_port", 0))))
            self.lbl_udp_port.setText(self.tr("UDP Port: {}").format(int(config.get("udp_port", 0))))
            ssl_on = config.get("ssl_enabled", False)
            self.lbl_ssl.setText(self.tr("SSL/TLS: {}").format(
                self.tr("Enabled") if ssl_on else self.tr("Disabled")
            ))

        except Exception:
            self.card_running.set_value(self.tr("Error"))

    @staticmethod
    def _format_number(n) -> str:
        n = int(n)
        if n >= 1_000_000:
            return f"{n / 1_000_000:.1f}M"
        if n >= 1_000:
            return f"{n / 1_000:.1f}K"
        return str(n)

    def stop(self):
        self._timer.stop()
