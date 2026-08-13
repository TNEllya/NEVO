"""NEVO v2 sidebar — server bar (72px) + channel bar (240px) + connection bar.

Implements columns 1 & 2 of design/pages/主界面 - 语音频道.html.
"""

import os
import sys

from PyQt5.QtCore import Qt, pyqtSignal, QSize, QPoint
from PyQt5.QtGui import QPixmap
from PyQt5.QtWidgets import (
    QFrame, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QScrollArea,
    QWidget, QSizePolicy, QSpacerItem, QLineEdit, QDialog, QDialogButtonBox,
    QMessageBox,
)
from qfluentwidgets import RoundMenu, Action, LineEdit, SpinBox

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from v2.theme import (
    palette, color, render_icon, render_icon_qicon, v2_qss, W_SERVER_BAR, W_CHANNEL_BAR,
    Avatar, IconButton, VoiceActivityBars, PRIMARY, RADIUS_MD, RADIUS_LG,
)


# ───────────────────────── Connect dialog ─────────────────────────
class ConnectDialog(QDialog):
    """Modal dialog for entering host/port/username — shown when disconnected."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("连接到 NEVO 服务器")
        self.setFixedSize(360, 240)
        self.setStyleSheet(v2_qss())
        lay = QVBoxLayout(self)
        lay.setContentsMargins(24, 24, 24, 24)
        lay.setSpacing(12)

        title = QLabel("连接到服务器")
        title.setStyleSheet(f"font-size: 18px; font-weight: 600; color: {palette()['text_primary']};")
        lay.addWidget(title)

        self.host = LineEdit()
        self.host.setText("127.0.0.1")
        self.host.setPlaceholderText("服务器地址")
        lay.addWidget(self.host)

        row = QHBoxLayout()
        row.setSpacing(8)
        self.port = SpinBox()
        self.port.setRange(1, 65535)
        self.port.setValue(24430)
        self.port.setFixedWidth(120)
        row.addWidget(QLabel("端口"))
        row.addWidget(self.port)
        row.addStretch()
        lay.addLayout(row)

        self.username = LineEdit()
        self.username.setPlaceholderText("用户名")
        lay.addWidget(self.username)

        btns = QHBoxLayout()
        self.btn_connect = QPushButton("连接")
        self.btn_connect.setObjectName("PrimaryBtn")
        self.btn_connect.setCursor(Qt.PointingHandCursor)
        self.btn_connect.clicked.connect(self.accept)
        self.btn_cancel = QPushButton("取消")
        self.btn_cancel.setObjectName("GhostBtn")
        self.btn_cancel.setCursor(Qt.PointingHandCursor)
        self.btn_cancel.clicked.connect(self.reject)
        btns.addStretch()
        btns.addWidget(self.btn_cancel)
        btns.addWidget(self.btn_connect)
        lay.addLayout(btns)

    def get_values(self):
        return self.host.text().strip(), int(self.port.value()), self.username.text().strip()


# ───────────────────────── Server sidebar (col 1, 72px) ─────────────────────────
class ServerSidebar(QFrame):
    """Discord-style 72px server icon rail."""

    server_selected = pyqtSignal(int)   # server index
    add_server = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("ServerBar")
        self.setFixedWidth(W_SERVER_BAR)
        self._servers = []  # list of {"name", "host", "port"}
        self._active = -1
        self._logo_btn = None
        self._icon_btns = []
        self._setup_ui()

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 12, 0, 12)
        lay.setSpacing(6)
        lay.setAlignment(Qt.AlignTop)

        # NEVO logo
        self._logo_btn = QPushButton("N")
        self._logo_btn.setObjectName("ServerLogo")
        self._logo_btn.setFixedSize(48, 48)
        self._logo_btn.setCursor(Qt.PointingHandCursor)
        self._logo_btn.setToolTip("NEVO")
        self._logo_btn.clicked.connect(lambda: self.server_selected.emit(-1))
        # Wrap in centered layout
        logo_wrap = QHBoxLayout()
        logo_wrap.setAlignment(Qt.AlignCenter)
        logo_wrap.addWidget(self._logo_btn)
        lay.addLayout(logo_wrap)

        # Divider
        div = QFrame()
        div.setObjectName("VDivider")
        div.setFixedHeight(2)
        div.setFixedWidth(32)
        div_wrap = QHBoxLayout()
        div_wrap.setAlignment(Qt.AlignCenter)
        div_wrap.addWidget(div)
        lay.addLayout(div_wrap)

        # Server icons container
        self._icons_host = QWidget()
        self._icons_lay = QVBoxLayout(self._icons_host)
        self._icons_lay.setContentsMargins(0, 0, 0, 0)
        self._icons_lay.setSpacing(6)
        self._icons_lay.setAlignment(Qt.AlignTop)
        lay.addWidget(self._icons_host)

        lay.addStretch()

        # Add-server button
        add_btn = QPushButton()
        add_btn.setObjectName("AddServer")
        add_btn.setFixedSize(48, 48)
        add_btn.setCursor(Qt.PointingHandCursor)
        add_btn.setToolTip("添加服务器")
        add_btn.setIcon(render_icon_qicon("plus", 20, palette().get("text_secondary", "#9CA3B4")))
        add_btn.setIconSize(QSize(20, 20))
        add_btn.clicked.connect(self.add_server.emit)
        add_wrap = QHBoxLayout()
        add_wrap.setAlignment(Qt.AlignCenter)
        add_wrap.addWidget(add_btn)
        lay.addLayout(add_wrap)

    def set_servers(self, servers: list, active_index: int = -1):
        self._servers = servers
        self._active = active_index
        # Clear existing
        while self._icons_lay.count():
            it = self._icons_lay.takeAt(0)
            w = it.widget()
            if w:
                w.deleteLater()
        self._icon_btns = []
        for i, srv in enumerate(servers):
            btn = self._make_server_btn(srv, i)
            wrap = QHBoxLayout()
            wrap.setAlignment(Qt.AlignCenter)
            wrap.addWidget(btn)
            self._icons_lay.addLayout(wrap)
            self._icon_btns.append(btn)

    def _make_server_btn(self, srv: dict, index: int) -> QPushButton:
        name = srv.get("name") or srv.get("host", "S")
        label = self._short_label(name)
        btn = QPushButton(label)
        btn.setObjectName("ServerIcon")
        btn.setFixedSize(48, 48)
        btn.setCursor(Qt.PointingHandCursor)
        btn.setCheckable(True)
        btn.setChecked(index == self._active)
        btn.setToolTip(f"{name}\n{srv.get('host','')}:{srv.get('port','')}")
        btn.clicked.connect(lambda checked, i=index: self.server_selected.emit(i))
        return btn

    @staticmethod
    def _short_label(name: str) -> str:
        # Take first 3 uppercase letters / alnum chars
        alnum = "".join(c for c in name if c.isalnum())
        return (alnum[:3] or name[:1]).upper()

    def refresh_theme(self):
        for btn in self._icon_btns:
            pass
        if self._logo_btn:
            self._logo_btn.setStyleSheet(self._logo_btn.styleSheet())


# ───────────────────────── Channel rows ─────────────────────────
class _ChannelRow(QFrame):
    """A single channel entry in the channel sidebar."""

    clicked = pyqtSignal(int)   # channel_id

    def __init__(self, channel: dict, parent=None):
        super().__init__(parent)
        self.setObjectName("ChannelRow")
        self._channel = channel
        self._active = False
        self.setMouseTracking(True)
        self.setCursor(Qt.PointingHandCursor)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(12, 6, 8, 6)
        lay.setSpacing(8)
        self._icon = QLabel()
        self._icon.setFixedSize(18, 18)
        self._refresh_icon()
        lay.addWidget(self._icon)
        self._name = QLabel(channel.get("name", ""))
        self._name.setObjectName("ChannelLabel")
        lay.addWidget(self._name)
        lay.addStretch()
        self._count = QLabel()
        self._count.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")
        lay.addWidget(self._count)
        self.set_user_count(len(channel.get("users", [])))

    def _refresh_icon(self):
        self._icon.setPixmap(render_icon("volume", 18, palette().get("text_muted", "#6B7280")))

    def set_active(self, active: bool):
        self._active = active
        self.setProperty("active", "true" if active else "false")
        self.style().unpolish(self)
        self.style().polish(self)

    def set_user_count(self, count: int):
        self._count.setText(str(count) if count > 0 else "")

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            self.clicked.emit(self._channel.get("id", 0))
        super().mousePressEvent(e)

    def refresh_theme(self):
        self._refresh_icon()
        self._count.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")


class _UserRow(QFrame):
    """A connected user inside a voice channel (small avatar + name)."""

    video_call_requested = pyqtSignal(int, str)
    volume_requested = pyqtSignal(int, str)
    local_mute_requested = pyqtSignal(int, bool)

    def __init__(self, user: dict, local_user_id: int = 0, is_admin: bool = False,
                 local_avatar: QPixmap = None, parent=None):
        super().__init__(parent)
        self.setObjectName("UserRow")
        self._user = user
        self._local_user_id = local_user_id
        self._is_admin = is_admin
        self._muted = user.get("muted", False)
        self.setMouseTracking(True)
        self.setCursor(Qt.PointingHandCursor)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(20, 4, 8, 4)
        lay.setSpacing(8)

        self._avatar = Avatar(24)
        name = user.get("username", "")
        if user.get("id") == local_user_id and local_avatar is not None:
            self._avatar.set_user(name, local_avatar)
        else:
            self._avatar.set_user(name)
        lay.addWidget(self._avatar)
        self._name = QLabel(name)
        self._name.setStyleSheet(self._name_style())
        lay.addWidget(self._name)
        lay.addStretch()

    def _name_style(self):
        col = palette().get("text_muted", "#6B7280") if self._muted else palette().get("text_primary", "#E8EAF0")
        return f"color: {col}; font-size: 13px; font-weight: 500;"

    def set_speaking(self, speaking: bool):
        uid = self._user.get("id", 0)
        if uid == self._local_user_id:
            return
        col = palette().get("text_primary", "#E8EAF0") if speaking else self._name_color_base()
        self._name.setStyleSheet(f"color: {col}; font-size: 13px; font-weight: 500;")

    def _name_color_base(self):
        return palette().get("text_muted", "#6B7280") if self._muted else palette().get("text_primary", "#E8EAF0")

    def set_local_muted(self, muted: bool):
        self._muted = muted
        self._name.setStyleSheet(self._name_style())

    def mousePressEvent(self, e):
        if e.button() == Qt.RightButton:
            self._show_context_menu(e.globalPos())
        super().mousePressEvent(e)

    def _show_context_menu(self, pos: QPoint):
        uid = self._user.get("id", 0)
        uname = self._user.get("username", "")
        if uid == self._local_user_id:
            return
        menu = RoundMenu(parent=self)
        menu.addAction(Action("视频通话", triggered=lambda: self.video_call_requested.emit(uid, uname)))
        menu.addAction(Action("调节音量", triggered=lambda: self.volume_requested.emit(uid, uname)))
        menu.addAction(Action("本地静音", triggered=lambda: self.local_mute_requested.emit(uid, True)))
        menu.exec_(pos)

    def refresh_theme(self):
        self._name.setStyleSheet(self._name_style())


# ───────────────────────── Connection bar (bottom of col 2) ─────────────────────────
class _ConnectionBar(QFrame):
    """Self-status bar at the bottom of the channel sidebar."""

    connect_requested = pyqtSignal()
    disconnect_requested = pyqtSignal()
    mute_toggled = pyqtSignal(bool)
    deafen_toggled = pyqtSignal(bool)
    settings_requested = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("ConnectionBar")
        self.setFixedHeight(52)
        self._connected = False
        self._setup_ui()

    def _setup_ui(self):
        lay = QHBoxLayout(self)
        lay.setContentsMargins(8, 0, 8, 0)
        lay.setSpacing(6)

        self._avatar = Avatar(32)
        lay.addWidget(self._avatar)

        info = QVBoxLayout()
        info.setContentsMargins(0, 0, 0, 0)
        info.setSpacing(0)
        self._name_lbl = QLabel("未连接")
        self._name_lbl.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 13px; font-weight: 600;")
        self._status_lbl = QLabel("点击连接")
        self._status_lbl.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")
        info.addWidget(self._name_lbl)
        info.addWidget(self._status_lbl)
        lay.addLayout(info)
        lay.addStretch()

        self._btn_mic = IconButton("mic", 16)
        self._btn_mic.setCheckable(False)
        self._btn_mic.setToolTip("麦克风")
        self._btn_mic.clicked.connect(lambda: self.mute_toggled.emit(self._muted))
        lay.addWidget(self._btn_mic)

        self._btn_deafen = IconButton("headphones", 16)
        self._btn_deafen.setCheckable(False)
        self._btn_deafen.setToolTip("耳机")
        self._btn_deafen.clicked.connect(lambda: self.deafen_toggled.emit(self._deafened))
        lay.addWidget(self._btn_deafen)

        btn_settings = IconButton("settings", 16)
        btn_settings.setCheckable(False)
        btn_settings.setToolTip("设置")
        btn_settings.clicked.connect(self.settings_requested.emit)
        lay.addWidget(btn_settings)

        self._muted = False
        self._deafened = False
        self.set_connected(False)

    def set_connected(self, connected: bool):
        self._connected = connected
        if connected:
            self._status_lbl.setText("已连接")
        else:
            self._status_lbl.setText("点击连接")
            self._name_lbl.setText("未连接")

    def set_self_user(self, name: str, avatar: QPixmap = None):
        self._avatar.set_user(name or "?", avatar)
        self._name_lbl.setText(name or "用户")

    def set_muted(self, muted: bool):
        self._muted = muted
        self._btn_mic.set_icon("mic-off" if muted else "mic")
        if muted:
            self._btn_mic.set_color(palette().get("error", "#F87171"))
        else:
            self._btn_mic.set_color(None)

    def set_deafened(self, deafened: bool):
        self._deafened = deafened
        self._btn_deafen.set_icon("deafen" if deafened else "headphones")
        if deafened:
            self._btn_deafen.set_color(palette().get("error", "#F87171"))
        else:
            self._btn_deafen.set_color(None)

    def mousePressEvent(self, e):
        if not self._connected and e.button() == Qt.LeftButton:
            # Only trigger when clicking the avatar/name area
            self.connect_requested.emit()
        super().mousePressEvent(e)

    def refresh_theme(self):
        self._name_lbl.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 13px; font-weight: 600;")
        self._status_lbl.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")
        self._btn_mic.refresh_theme()
        self._btn_deafen.refresh_theme()


# ───────────────────────── Channel sidebar (col 2, 240px) ─────────────────────────
class ChannelSidebar(QFrame):
    """240px channel list with server header + channel tree + connection bar."""

    join_channel = pyqtSignal(int)
    leave_channel = pyqtSignal()
    video_call_requested = pyqtSignal(int, str)
    user_volume_requested = pyqtSignal(int, str)
    local_mute_requested = pyqtSignal(int, bool)
    connect_requested = pyqtSignal()
    disconnect_requested = pyqtSignal()
    mute_toggled = pyqtSignal(bool)
    deafen_toggled = pyqtSignal(bool)
    settings_requested = pyqtSignal()
    create_channel_requested = pyqtSignal()
    admin_action_requested = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("ChannelBar")
        self.setFixedWidth(W_CHANNEL_BAR)
        self._channels = []
        self._current_channel = 0
        self._local_user_id = 0
        self._is_admin = False
        self._local_avatar = None
        self._channel_rows = []      # list of (_ChannelRow, channel)
        self._user_rows = {}         # uid -> _UserRow
        self._setup_ui()

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

        # Server name header
        header = QFrame()
        header.setObjectName("ChannelHeader")
        header.setFixedHeight(48)
        hlay = QHBoxLayout(header)
        hlay.setContentsMargins(16, 0, 16, 0)
        self._server_name = QLabel("NEVO 服务器")
        self._server_name.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 15px; font-weight: 600;")
        hlay.addWidget(self._server_name)
        hlay.addStretch()
        hlay.addWidget(self._make_header_icon("chevron-down"))
        header.setCursor(Qt.PointingHandCursor)
        header.mousePressEvent = self._on_header_click
        lay.addWidget(header)

        # Scrollable channel tree
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        host = QWidget()
        self._tree_lay = QVBoxLayout(host)
        self._tree_lay.setContentsMargins(0, 8, 0, 8)
        self._tree_lay.setSpacing(1)
        self._tree_lay.setAlignment(Qt.AlignTop)
        scroll.setWidget(host)
        lay.addWidget(scroll, 1)

        # Connection bar
        self._conn_bar = _ConnectionBar()
        self._conn_bar.connect_requested.connect(self.connect_requested.emit)
        self._conn_bar.disconnect_requested.connect(self.disconnect_requested.emit)
        self._conn_bar.mute_toggled.connect(self.mute_toggled.emit)
        self._conn_bar.deafen_toggled.connect(self.deafen_toggled.emit)
        self._conn_bar.settings_requested.connect(self.settings_requested.emit)
        lay.addWidget(self._conn_bar)

    def _make_header_icon(self, name: str) -> QLabel:
        lbl = QLabel()
        lbl.setPixmap(render_icon(name, 14, palette().get("text_secondary", "#9CA3B4")))
        return lbl

    def _on_header_click(self, e):
        if e.button() == Qt.RightButton and self._is_admin:
            menu = RoundMenu(parent=self)
            menu.addAction(Action("创建频道", triggered=self.create_channel_requested.emit))
            menu.addAction(Action("设置服务器名称", triggered=lambda: self.admin_action_requested.emit("set_server_name")))
            menu.addAction(Action("管理员登录", triggered=lambda: self.admin_action_requested.emit("login")))
            menu.exec_(e.globalPos())

    # ---- Public API ----
    def set_server_name(self, name: str):
        self._server_name.setText(name or "NEVO 服务器")

    def set_user_info(self, user_id: int, is_admin: bool):
        self._local_user_id = user_id
        self._is_admin = is_admin

    def set_self_user(self, name: str, avatar: QPixmap = None):
        self._conn_bar.set_self_user(name, avatar)

    def set_connected(self, connected: bool):
        self._conn_bar.set_connected(connected)

    def set_muted(self, muted: bool):
        self._conn_bar.set_muted(muted)

    def set_deafened(self, deafened: bool):
        self._conn_bar.set_deafened(deafened)

    def set_latency(self, ms: int):
        # Not shown in col2 design (shown in voice panel footer); keep for API compat.
        pass

    def update_channels(self, channels: list, current_channel_id: int = 0,
                        local_avatar: QPixmap = None):
        self._channels = channels or []
        self._current_channel = current_channel_id
        if local_avatar is not None:
            self._local_avatar = local_avatar
        self._rebuild_tree()

    def set_current_channel(self, channel_id: int):
        self._current_channel = channel_id
        for row, ch in self._channel_rows:
            row.set_active(ch.get("id") == channel_id)

    def set_speaking(self, user_id: int, speaking: bool):
        row = self._user_rows.get(user_id)
        if row:
            row.set_speaking(speaking)

    def set_local_muted(self, user_id: int, muted: bool):
        row = self._user_rows.get(user_id)
        if row:
            row.set_local_muted(muted)

    def _rebuild_tree(self):
        # Clear
        while self._tree_lay.count():
            it = self._tree_lay.takeAt(0)
            w = it.widget()
            if w:
                w.deleteLater()
        self._channel_rows = []
        self._user_rows = {}

        # Category header — NEVO channels are voice channels
        cat = QLabel("语音频道")
        cat.setObjectName("CategoryLabel")
        cat_lay = QHBoxLayout(cat)
        cat_lay.setContentsMargins(8, 8, 8, 4)
        cat_lay.addWidget(self._make_header_icon("chevron-down"))
        cat_lay.addWidget(cat)
        # Build category row properly
        cat_row = QWidget()
        cat_row_lay = QHBoxLayout(cat_row)
        cat_row_lay.setContentsMargins(8, 8, 8, 4)
        cat_row_lay.setSpacing(4)
        ic = QLabel()
        ic.setPixmap(render_icon("chevron-down", 10, palette().get("text_secondary", "#9CA3B4")))
        cat_row_lay.addWidget(ic)
        lbl = QLabel("语音频道")
        lbl.setObjectName("CategoryLabel")
        cat_row_lay.addWidget(lbl)
        cat_row_lay.addStretch()
        self._tree_lay.addWidget(cat_row)
        cat.deleteLater()

        # Top-level channels (parent_id == 0)
        top = [c for c in self._channels if not c.get("parent_id")]
        for ch in top:
            row = _ChannelRow(ch)
            row.set_active(ch.get("id") == self._current_channel)
            row.clicked.connect(self._on_channel_click)
            self._tree_lay.addWidget(row)
            self._channel_rows.append((row, ch))

            # Show connected users for the current channel
            if ch.get("id") == self._current_channel and ch.get("users"):
                for u in ch["users"]:
                    urow = _UserRow(u, self._local_user_id, self._is_admin, self._local_avatar)
                    urow.video_call_requested.connect(self.video_call_requested.emit)
                    urow.volume_requested.connect(self.user_volume_requested.emit)
                    urow.local_mute_requested.connect(self.local_mute_requested.emit)
                    self._tree_lay.addWidget(urow)
                    self._user_rows[u.get("id", 0)] = urow

            # Sub-channels
            children = [c for c in self._channels if c.get("parent_id") == ch.get("id")]
            for sub in children:
                sub_row = _ChannelRow(sub)
                sub_row.set_active(sub.get("id") == self._current_channel)
                sub_row.clicked.connect(self._on_channel_click)
                self._tree_lay.addWidget(sub_row)
                self._channel_rows.append((sub_row, sub))
                if sub.get("id") == self._current_channel and sub.get("users"):
                    for u in sub["users"]:
                        urow = _UserRow(u, self._local_user_id, self._is_admin, self._local_avatar)
                        urow.video_call_requested.connect(self.video_call_requested.emit)
                        urow.volume_requested.connect(self.user_volume_requested.emit)
                        urow.local_mute_requested.connect(self.local_mute_requested.emit)
                        self._tree_lay.addWidget(urow)
                        self._user_rows[u.get("id", 0)] = urow

        self._tree_lay.addStretch()

    def _on_channel_click(self, channel_id: int):
        if channel_id == self._current_channel:
            self.leave_channel.emit()
        else:
            self.join_channel.emit(channel_id)

    def refresh_theme(self):
        self._server_name.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 15px; font-weight: 600;")
        self._conn_bar.refresh_theme()
        for row, _ in self._channel_rows:
            row.refresh_theme()
        for row in self._user_rows.values():
            row.refresh_theme()
        # Rebuild tree to refresh category icons
        self._rebuild_tree()
