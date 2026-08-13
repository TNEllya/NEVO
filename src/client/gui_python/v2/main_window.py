"""NEVO v2 main window — Discord-style 3-column layout from design/pages/主界面 - 语音频道.html.

Assembles ServerSidebar (72px) + ChannelSidebar (240px) + main area (header +
ChatPanel + VoiceUsersPanel 280px), and wires every signal to the existing
backend engines (NevoClient / VoiceEngine / VideoEngine / VideoCallEngine /
AudioManager / AvatarManager) using the same thread-safe signal-bridge pattern
as the original main_window.py.
"""

import os
import sys
import threading
import traceback
from typing import Optional

from PyQt5.QtCore import Qt, pyqtSignal, QObject, QSize
from PyQt5.QtGui import QPixmap, QIcon
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QFrame, QLabel, QPushButton,
    QDialog, QInputDialog, QLineEdit, QSizePolicy,
)
from qfluentwidgets import InfoBar, InfoBarPosition, RoundMenu, Action

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from nevo_client import NevoClient, ClientState, VideoCallState
from nevo_wire import VideoProfile
from audio_manager import AudioManager, InputMode
from avatar_manager import AvatarManager
from voice_engine import VoiceEngine
from video_engine import VideoEngine
from video_call_engine import VideoCallEngine
from camera_capture import CameraCapture
from per_user_volume import PerUserVolumeManager, VolumeSliderDialog
from theme_manager import ThemeManager
import join_sound

from v2.theme import palette, render_icon, render_icon_qicon, v2_qss, Avatar, IconButton, W_SERVER_BAR, W_CHANNEL_BAR, W_VOICE_PANEL
from v2.sidebar import ServerSidebar, ChannelSidebar, ConnectDialog
from v2.chat_panel import ChatPanel
from v2.voice_users_panel import VoiceUsersPanel
from v2.video_call_window import VideoCallWindow
from v2.settings_window import SettingsWindow


_MAIN_LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "v2_main_debug.log")


def _log_main(msg: str):
    from datetime import datetime
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    line = f"[{ts}] [V2-MAIN] {msg}"
    print(line)
    try:
        with open(_MAIN_LOG, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass


# ───────────────────────── Thread-safe signal bridge ─────────────────────────
class _CallbackSignalHelper(QObject):
    """Marshals callbacks from the network/audio thread to the UI thread."""
    state_changed = pyqtSignal(int, int)
    channel_list = pyqtSignal(list)
    user_joined = pyqtSignal(dict)
    user_left = pyqtSignal(int)
    user_speaking = pyqtSignal(int, bool)
    chat_message = pyqtSignal(int, str, int, str, int)
    server_message = pyqtSignal(str)
    error = pyqtSignal(int, str)
    admin_auth_result = pyqtSignal(bool, str)
    admin_action_result = pyqtSignal(bool, str)
    file_upload_response = pyqtSignal(int, bool, str)
    vad_speaking_changed = pyqtSignal(bool)
    latency_update = pyqtSignal(int)

    # 视频通话信号
    video_call_incoming = pyqtSignal(int, int, str, object)
    video_call_established = pyqtSignal(int, int, object)
    video_call_ended = pyqtSignal(int, int)
    video_call_error = pyqtSignal(int, str)
    video_call_frame = pyqtSignal(int, object, int, int)
    file_received = pyqtSignal(int, str, str)    # file_id, cached_path, filename
    file_error = pyqtSignal(int, str)            # file_id, message


# ───────────────────────── v2-styled incoming call dialog ─────────────────────────
class _IncomingCallDialog(QDialog):
    """Incoming video-call dialog styled with the v2 palette."""

    accepted_call = pyqtSignal()
    rejected_call = pyqtSignal()

    def __init__(self, caller_name: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle("来电")
        self.setFixedSize(340, 200)
        self.setStyleSheet(v2_qss())
        p = palette()
        lay = QVBoxLayout(self)
        lay.setContentsMargins(24, 24, 24, 24)
        lay.setSpacing(12)
        lay.setAlignment(Qt.AlignCenter)

        av = Avatar(56)
        av.set_user(caller_name)
        lay.addWidget(av, alignment=Qt.AlignCenter)

        name = QLabel(caller_name)
        name.setStyleSheet(f"color: {p['text_primary']}; font-size: 16px; font-weight: 600;")
        lay.addWidget(name, alignment=Qt.AlignCenter)

        sub = QLabel("邀请你进行视频通话…")
        sub.setStyleSheet(f"color: {p['text_muted']}; font-size: 12px;")
        lay.addWidget(sub, alignment=Qt.AlignCenter)

        btns = QHBoxLayout()
        btns.setSpacing(12)
        btn_w = 110
        btn_accept = QPushButton("接听")
        btn_accept.setObjectName("PrimaryBtn")
        btn_accept.setCursor(Qt.PointingHandCursor)
        btn_accept.setFixedWidth(btn_w)
        btn_accept.setIcon(render_icon_qicon("phone", 16, p.get("bg_primary", "#0A1A14")))
        btn_accept.setIconSize(QSize(16, 16))
        btn_accept.clicked.connect(self._on_accept)
        btns.addWidget(btn_accept)

        btn_reject = QPushButton("拒绝")
        btn_reject.setObjectName("GhostBtn")
        btn_reject.setCursor(Qt.PointingHandCursor)
        btn_reject.setFixedWidth(btn_w)
        btn_reject.setStyleSheet(
            f"QPushButton {{ background-color: {p.get('error','#F87171')}; color: white; "
            f"border: none; border-radius: 4px; padding: 8px 14px; font-weight: 600; }}"
            f"QPushButton:hover {{ background-color: {p.get('error_hover','#D63B3B')}; }}"
        )
        btn_reject.clicked.connect(self._on_reject)
        btns.addWidget(btn_reject)
        lay.addLayout(btns)

    def _on_accept(self):
        self.accepted_call.emit()
        self.accept()

    def _on_reject(self):
        self.rejected_call.emit()
        self.reject()

    def closeEvent(self, e):
        self.rejected_call.emit()
        super().closeEvent(e)


# ───────────────────────── Header bar (top of main area) ─────────────────────────
class _HeaderBar(QFrame):
    """Top header of the main content area: channel name + action icons."""

    create_channel_requested = pyqtSignal()
    admin_action_requested = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("HeaderBar")
        self.setFixedHeight(48)
        self._is_admin = False
        self._setup_ui()

    def _setup_ui(self):
        lay = QHBoxLayout(self)
        lay.setContentsMargins(16, 0, 16, 0)
        lay.setSpacing(10)

        self._hash_icon = QLabel()
        self._hash_icon.setPixmap(render_icon("hash", 20, palette().get("text_muted", "#6B7280")))
        lay.addWidget(self._hash_icon)

        self._title = QLabel("未加入频道")
        self._title.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 15px; font-weight: 600;")
        lay.addWidget(self._title)

        self._topic = QLabel("")
        self._topic.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 12px; padding-left: 8px; border-left: 1px solid {palette().get('bg_hover','')}; margin-left: 4px;")
        lay.addWidget(self._topic)
        lay.addStretch()

        # Action icons
        self._btn_inbox = IconButton("inbox", 18)
        self._btn_inbox.setCheckable(False)
        self._btn_inbox.setToolTip("通知")
        lay.addWidget(self._btn_inbox)

        self._btn_members = IconButton("users", 18)
        self._btn_members.setCheckable(False)
        self._btn_members.setToolTip("成员")
        lay.addWidget(self._btn_members)

        self._btn_search = IconButton("search", 18)
        self._btn_search.setCheckable(False)
        self._btn_search.setToolTip("搜索")
        lay.addWidget(self._btn_search)

    def set_channel(self, name: str, topic: str = ""):
        self._title.setText(f"#{name}" if name and not name.startswith("#") else (name or "未加入频道"))
        self._topic.setText(topic)

    def set_admin(self, is_admin: bool):
        self._is_admin = is_admin

    def mousePressEvent(self, e):
        if e.button() == Qt.RightButton and self._is_admin:
            menu = RoundMenu(parent=self)
            menu.addAction(Action("创建频道", triggered=self.create_channel_requested.emit))
            menu.addAction(Action("设置服务器名称", triggered=lambda: self.admin_action_requested.emit("set_server_name")))
            menu.addAction(Action("管理员登录", triggered=lambda: self.admin_action_requested.emit("login")))
            menu.exec_(e.globalPos())
        super().mousePressEvent(e)

    def refresh_theme(self):
        self._hash_icon.setPixmap(render_icon("hash", 20, palette().get("text_muted", "#6B7280")))
        self._title.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 15px; font-weight: 600;")
        self._topic.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 12px; padding-left: 8px; border-left: 1px solid {palette().get('bg_hover','')}; margin-left: 4px;")


# ───────────────────────── Main window ─────────────────────────
class _TitleBar(QFrame):
    """Custom frameless window title bar (dark, no OS white border)."""

    def __init__(self, parent: QWidget):
        super().__init__(parent)
        self._parent = parent
        self.setObjectName("TitleBar")
        self.setFixedHeight(32)
        self._drag_pos = None
        self._setup_ui()
        self.refresh_theme()

    def _setup_ui(self):
        p = palette()
        lay = QHBoxLayout(self)
        lay.setContentsMargins(12, 0, 0, 0)
        lay.setSpacing(0)

        # Left: small icon + title
        self._icon = QLabel()
        self._icon.setFixedSize(18, 18)
        self._icon.setPixmap(render_icon("message", 18, p.get("primary", "#2DD4A8")))
        lay.addWidget(self._icon)

        self._title = QLabel("NEVO — v2")
        self._title.setStyleSheet(f"color: {p.get('text_primary')}; font-size: 12px; font-weight: 600; padding-left: 6px;")
        lay.addWidget(self._title)
        lay.addStretch()

        # Window control buttons
        btn_size = 46
        self._btn_min = QPushButton("—")
        self._btn_min.setObjectName("TitleBarBtn")
        self._btn_min.setFixedSize(btn_size, 32)
        self._btn_min.setCursor(Qt.PointingHandCursor)
        self._btn_min.clicked.connect(self._parent.showMinimized)
        lay.addWidget(self._btn_min)

        self._btn_max = QPushButton("□")
        self._btn_max.setObjectName("TitleBarBtn")
        self._btn_max.setFixedSize(btn_size, 32)
        self._btn_max.setCursor(Qt.PointingHandCursor)
        self._btn_max.clicked.connect(self._toggle_maximize)
        lay.addWidget(self._btn_max)

        self._btn_close = QPushButton("×")
        self._btn_close.setObjectName("TitleBarCloseBtn")
        self._btn_close.setFixedSize(btn_size, 32)
        self._btn_close.setCursor(Qt.PointingHandCursor)
        self._btn_close.clicked.connect(self._parent.close)
        lay.addWidget(self._btn_close)

    def _toggle_maximize(self):
        if self._parent.isMaximized():
            self._parent.showNormal()
        else:
            self._parent.showMaximized()

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            self._drag_pos = e.globalPos() - self._parent.frameGeometry().topLeft()
            e.accept()

    def mouseMoveEvent(self, e):
        if e.buttons() == Qt.LeftButton and self._drag_pos is not None:
            if self._parent.isMaximized():
                # Restore first, then drag from the clicked x position.
                ratio = e.pos().x() / max(1, self.width())
                self._parent.showNormal()
                new_w = self._parent.width()
                self._parent.move(int(e.globalPos().x() - new_w * ratio), 0)
                self._drag_pos = e.globalPos() - self._parent.frameGeometry().topLeft()
            else:
                self._parent.move(e.globalPos() - self._drag_pos)
            e.accept()

    def mouseReleaseEvent(self, e):
        self._drag_pos = None

    def mouseDoubleClickEvent(self, e):
        if e.button() == Qt.LeftButton:
            self._toggle_maximize()

    def refresh_theme(self):
        p = palette()
        self.setStyleSheet(f"""
            QFrame#TitleBar {{
                background-color: {p.get('bg_base', '#1A1B1E')};
                border-top-left-radius: 0px;
                border-top-right-radius: 0px;
            }}
            QPushButton#TitleBarBtn {{
                background: transparent;
                color: {p.get('text_secondary', '#9CA3B4')};
                border: none;
                font-size: 14px;
                font-weight: 400;
            }}
            QPushButton#TitleBarBtn:hover {{
                background-color: {p.get('bg_hover', '#2A2D35')};
                color: {p.get('text_primary', '#FFFFFF')};
            }}
            QPushButton#TitleBarCloseBtn {{
                background: transparent;
                color: {p.get('text_secondary', '#9CA3B4')};
                border: none;
                font-size: 16px;
                font-weight: 400;
            }}
            QPushButton#TitleBarCloseBtn:hover {{
                background-color: {p.get('error', '#F87171')};
                color: #FFFFFF;
            }}
        """)


class MainWindow(QWidget):
    """NEVO v2 main window — Discord-style 3-column layout."""

    def __init__(self):
        super().__init__()
        _log_main("=" * 60)
        _log_main("[V2 MAIN WINDOW] INITIALIZED")
        _log_main("=" * 60)

        self.setWindowTitle("NEVO — v2")
        self.resize(1280, 800)
        self.setMinimumSize(1000, 600)
        # 移除系统标题栏/白边，使用自定义标题栏
        self.setWindowFlags(Qt.FramelessWindowHint | Qt.Window)
        self.setAttribute(Qt.WA_TranslucentBackground, False)
        self.setStyleSheet(v2_qss())

        # ---- Engines ----
        self.client = NevoClient()
        self._signals = _CallbackSignalHelper()

        self.audio_manager = AudioManager()
        self.avatar_manager = AvatarManager()
        self.voice_engine = VoiceEngine()
        self.per_user_volume = PerUserVolumeManager(self.voice_engine)
        self.video_engine = VideoEngine()
        self.video_engine.on_video_frame = self._on_share_video_frame

        self.video_call_engine = VideoCallEngine()
        self.video_call_engine.on_video_frame = self._on_video_call_frame
        self.video_call_engine.on_error = self._on_video_call_error

        self._video_call_window: Optional[VideoCallWindow] = None
        self._incoming_call_dialog: Optional[_IncomingCallDialog] = None
        self._settings_window: Optional[SettingsWindow] = None
        self._pending_file_upload = None
        self._last_connect_params = ("127.0.0.1", 24430, "")

        # ---- UI ----
        self._setup_ui()
        self._setup_callbacks()
        self._setup_ptt()

        # Theme
        tm = ThemeManager.instance()
        tm.theme_changed.connect(self._on_theme_changed)

        # Window icon
        icon_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                 "resources", "nevo_icon.ico")
        if not os.path.exists(icon_path) and getattr(sys, "frozen", False):
            icon_path = os.path.join(sys._MEIPASS, "resources", "nevo_icon.ico")
        if os.path.exists(icon_path):
            self.setWindowIcon(QIcon(icon_path))

    # ───────────────────────── UI setup ─────────────────────────
    def _setup_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # Custom title bar (replaces OS title bar to remove white border)
        self.title_bar = _TitleBar(self)
        root.addWidget(self.title_bar)

        # Content area below title bar
        content_wrap = QHBoxLayout()
        content_wrap.setContentsMargins(0, 0, 0, 0)
        content_wrap.setSpacing(0)

        # Column 1: server rail (72px)
        self.server_sidebar = ServerSidebar()
        self.server_sidebar.server_selected.connect(self._on_server_selected)
        self.server_sidebar.add_server.connect(self._on_add_server)
        content_wrap.addWidget(self.server_sidebar)

        # Column 2: channel sidebar (240px)
        self.channel_sidebar = ChannelSidebar()
        self.channel_sidebar.join_channel.connect(self._on_join_channel)
        self.channel_sidebar.leave_channel.connect(self._on_leave_channel)
        self.channel_sidebar.connect_requested.connect(self._on_connect_clicked)
        self.channel_sidebar.disconnect_requested.connect(self._on_disconnect)
        self.channel_sidebar.mute_toggled.connect(self._on_mute_toggled)
        self.channel_sidebar.deafen_toggled.connect(self._on_deafen_toggled)
        self.channel_sidebar.settings_requested.connect(self._on_open_settings)
        self.channel_sidebar.create_channel_requested.connect(self._on_create_channel)
        self.channel_sidebar.admin_action_requested.connect(self._on_admin_action)
        self.channel_sidebar.video_call_requested.connect(self._on_video_call_requested)
        self.channel_sidebar.user_volume_requested.connect(self._on_user_volume_requested)
        self.channel_sidebar.local_mute_requested.connect(self._on_user_local_mute_requested)
        content_wrap.addWidget(self.channel_sidebar)

        # Column 3: main area (header + chat + voice panel)
        main = QFrame()
        main.setObjectName("MainArea")
        main_lay = QVBoxLayout(main)
        main_lay.setContentsMargins(0, 0, 0, 0)
        main_lay.setSpacing(0)

        # Header
        self.header = _HeaderBar()
        self.header.create_channel_requested.connect(self._on_create_channel)
        self.header.admin_action_requested.connect(self._on_admin_action)
        main_lay.addWidget(self.header)

        # Chat + voice panel (horizontal)
        content = QHBoxLayout()
        content.setContentsMargins(0, 0, 0, 0)
        content.setSpacing(0)

        self.chat_panel = ChatPanel()
        self.chat_panel.chat_message_sent.connect(self._on_chat_send)
        self.chat_panel.file_upload_requested.connect(self._on_file_upload)
        self.chat_panel.image_upload_requested.connect(self._on_image_upload)
        self.chat_panel.file_download_requested.connect(self._on_file_download)
        content.addWidget(self.chat_panel, 1)

        self.voice_panel = VoiceUsersPanel()
        self.voice_panel.video_call_requested.connect(self._on_video_call_requested)
        self.voice_panel.user_volume_requested.connect(self._on_user_volume_requested)
        self.voice_panel.local_mute_requested.connect(self._on_user_local_mute_requested)
        content.addWidget(self.voice_panel)

        main_lay.addLayout(content, 1)
        content_wrap.addWidget(main, 1)
        root.addLayout(content_wrap, 1)

        self._refresh_self_avatar()

    # ───────────────────────── Callback wiring ─────────────────────────
    def _setup_callbacks(self):
        # Signals -> handlers (UI thread)
        self._signals.state_changed.connect(self._handle_state_changed)
        self._signals.channel_list.connect(self._handle_channel_list)
        self._signals.user_joined.connect(self._handle_user_joined)
        self._signals.user_left.connect(self._handle_user_left)
        self._signals.user_speaking.connect(self._handle_user_speaking)
        self._signals.chat_message.connect(self._handle_chat_message)
        self._signals.server_message.connect(self._handle_server_message)
        self._signals.error.connect(self._handle_error)
        self._signals.admin_auth_result.connect(self._handle_admin_auth_result)
        self._signals.admin_action_result.connect(self._handle_admin_action_result)
        self._signals.file_upload_response.connect(self._handle_file_upload_response)
        self._signals.file_received.connect(self._handle_file_received)
        self._signals.file_error.connect(self._handle_file_error)
        self._signals.vad_speaking_changed.connect(self._handle_vad_speaking)
        self._signals.latency_update.connect(self._handle_latency_update)

        self._signals.video_call_incoming.connect(self._handle_video_call_incoming)
        self._signals.video_call_established.connect(self._handle_video_call_established)
        self._signals.video_call_ended.connect(self._handle_video_call_ended)
        self._signals.video_call_error.connect(self._handle_video_call_error)
        self._signals.video_call_frame.connect(self._handle_video_call_frame)

        # Client callbacks -> emit signals (network thread -> UI thread)
        self.client.on_state_changed = lambda new, old: self._signals.state_changed.emit(int(new), int(old))
        self.client.on_channel_list = lambda ch: self._signals.channel_list.emit(ch)
        self.client.on_user_joined = lambda u: self._signals.user_joined.emit(u)
        self.client.on_user_left = lambda u: self._signals.user_left.emit(u)
        self.client.on_user_speaking = lambda uid, spk: self._signals.user_speaking.emit(uid, spk)
        self.client.on_chat_message = lambda sid, sn, cid, txt, ts: self._signals.chat_message.emit(sid, sn, cid, txt, ts)
        self.client.on_server_message = lambda txt: self._signals.server_message.emit(txt)
        self.client.on_error = lambda code, msg: self._signals.error.emit(code, msg)
        self.client.on_admin_auth_result = lambda ok, msg: self._signals.admin_auth_result.emit(ok, msg)
        self.client.on_admin_action_result = lambda ok, msg: self._signals.admin_action_result.emit(ok, msg)
        self.client.on_file_upload_response = lambda fid, ok, msg: self._signals.file_upload_response.emit(fid, ok, msg)
        self.client.on_file_received = lambda fid, path, name: self._signals.file_received.emit(int(fid), path, name)
        self.client.on_file_error = lambda fid, msg: self._signals.file_error.emit(int(fid), msg)
        self.client.on_latency_update = lambda ms: self._signals.latency_update.emit(ms)

        self.client.on_video_call_incoming = \
            lambda cid, uid, name, prof: self._signals.video_call_incoming.emit(cid, uid, name, prof)
        self.client.on_video_call_established = \
            lambda cid, pid, prof: self._signals.video_call_established.emit(cid, pid, prof)
        self.client.on_video_call_ended = \
            lambda cid, reason: self._signals.video_call_ended.emit(cid, reason)
        self.client.on_video_call_error = \
            lambda cid, msg: self._signals.video_call_error.emit(cid, msg)

        # VAD (audio thread -> UI thread)
        self.audio_manager.on_vad_changed = lambda speaking: self._signals.vad_speaking_changed.emit(speaking)

        # Avatar change
        self.avatar_manager.avatar_changed.connect(self._on_avatar_changed)

    # ───────────────────────── Connection ─────────────────────────
    def _on_connect_clicked(self):
        dlg = ConnectDialog(self)
        if dlg.exec_():
            host, port, username = dlg.get_values()
            if not username:
                self._show_info("请输入用户名", "warning")
                return
            self._on_connect(host, port, username, "")

    def _on_connect(self, host: str, port: int, username: str, password: str):
        self._last_connect_params = (host, port, username)

        def connect_thread():
            success = self.client.connect(host, port, username, password,
                                          voice_engine=self.voice_engine,
                                          video_engine=self.video_engine)
            if not success:
                self._signals.error.emit(7, "连接失败")

        t = threading.Thread(target=connect_thread, daemon=True)
        t.start()

    def _on_disconnect(self):
        self.client.disconnect()

    def _on_server_selected(self, index: int):
        # Single-server v2 build: index -1 == NEVO home (no-op for now).
        pass

    def _on_add_server(self):
        dlg = ConnectDialog(self)
        if dlg.exec_():
            host, port, username = dlg.get_values()
            if username:
                self._on_connect(host, port, username, "")

    # ───────────────────────── Channel actions ─────────────────────────
    def _on_join_channel(self, channel_id: int):
        if not self.client.join_channel(channel_id):
            self._show_info("加入频道失败", "warning")

    def _on_leave_channel(self):
        self.client.leave_channel()

    def _on_create_channel(self):
        name, ok = QInputDialog.getText(self, "创建频道", "频道名称:", QLineEdit.Normal, "")
        if ok and name.strip():
            self.client.send_create_channel(name.strip())

    # ───────────────────────── Mute / deafen ─────────────────────────
    def _on_mute_toggled(self, current_muted: bool):
        new_muted = not current_muted
        self.client.set_muted(new_muted)
        try:
            self.voice_engine.set_muted(new_muted)
        except Exception:
            pass
        self.channel_sidebar.set_muted(new_muted)

    def _on_deafen_toggled(self, current_deafened: bool):
        new_deafened = not current_deafened
        self.client.set_deafened(new_deafened)
        try:
            self.voice_engine.set_deafened(new_deafened)
        except Exception:
            pass
        self.channel_sidebar.set_deafened(new_deafened)
        if new_deafened and not self.client.is_muted:
            self.client.set_muted(True)
            try:
                self.voice_engine.set_muted(True)
            except Exception:
                pass
            self.channel_sidebar.set_muted(True)

    # ───────────────────────── Chat ─────────────────────────
    def _on_chat_send(self, text: str):
        self.client.send_chat(text)

    def _on_file_upload(self, file_path: str, file_size: int):
        if not self.client.connected or self.client.current_channel_id == 0:
            self.chat_panel.add_system_message("请先加入频道。")
            return
        self.client.send_file_upload_request(
            self.client.current_channel_id,
            os.path.basename(file_path),
            file_size,
        )
        self._pending_file_upload = (file_path, os.path.basename(file_path), False)

    def _on_image_upload(self, file_path: str, file_size: int):
        if not self.client.connected or self.client.current_channel_id == 0:
            self.chat_panel.add_system_message("请先加入频道。")
            return
        self.client.send_file_upload_request(
            self.client.current_channel_id,
            os.path.basename(file_path),
            file_size,
        )
        self._pending_file_upload = (file_path, os.path.basename(file_path), True)

    # ───────────────────────── Per-user volume / mute ─────────────────────────
    def _on_user_volume_requested(self, user_id: int, username: str):
        current_vol = self.voice_engine.get_user_volume(user_id)
        dialog = VolumeSliderDialog(username, current_vol, self)
        dialog.volume_changed.connect(lambda vol: self.voice_engine.set_user_volume(user_id, vol))
        if dialog.exec_():
            final_vol = dialog.get_volume()
            self.voice_engine.set_user_volume(user_id, final_vol)
            self._save_per_user_settings(user_id)

    def _on_user_local_mute_requested(self, user_id: int, muted: bool):
        self.voice_engine.set_user_local_mute(user_id, muted)
        self.channel_sidebar.set_local_muted(user_id, muted)
        self.voice_panel.set_local_muted(user_id, muted)
        self._save_per_user_settings(user_id)

    # ───────────────────────── Admin ─────────────────────────
    def _on_admin_action(self, action: str):
        try:
            if action == "login":
                pwd, ok = QInputDialog.getText(self, "管理员登录", "管理员密码:", QLineEdit.Password, "")
                if ok and pwd:
                    self.client.send_admin_auth(pwd)
            elif action == "set_server_name":
                name, ok = QInputDialog.getText(self, "设置服务器名称", "新名称:", QLineEdit.Normal, "")
                if ok and name.strip():
                    self.client.send_set_server_name(name.strip())
        except Exception as e:
            _log_main(f"[ADMIN] action {action} failed: {e}")

    # ───────────────────────── Video call lifecycle ─────────────────────────
    def _on_video_call_requested(self, user_id: int, username: str):
        _log_main(f"[VIDEO_CALL] requested to user_id={user_id} name={username}")
        if not CameraCapture.is_available():
            self._show_info("OpenCV 未安装，视频通话不可用。", "warning")
            return
        if not self.client.connected:
            self._show_info("请先连接服务器。", "warning")
            return
        if self.client.video_call_state != VideoCallState.Idle:
            self._show_info("已在通话中。", "warning")
            return
        profile = VideoProfile(width=640, height=480, fps=30, target_bitrate_kbps=1000)
        if not self.client.send_video_call_request(user_id, profile):
            self._show_info("发送视频通话请求失败。", "warning")
        else:
            self._show_info(f"正在呼叫 {username}…", "info")

    def _on_video_call_frame(self, sender_id: int, frame_bgr, width: int, height: int):
        """Media engine callback (non-UI thread): forward via signal."""
        try:
            self._signals.video_call_frame.emit(sender_id, frame_bgr.copy(), width, height)
        except Exception as e:
            _log_main(f"[VIDEO_CALL] frame emit failed: {e}")

    def _on_video_call_error(self, msg: str):
        _log_main(f"[VIDEO_CALL] engine error: {msg}")
        self._signals.video_call_error.emit(0, msg)

    def _handle_video_call_incoming(self, call_id: int, caller_id: int, caller_name: str, profile):
        _log_main(f"[VIDEO_CALL] incoming call_id={call_id} from {caller_name}({caller_id})")
        if self._incoming_call_dialog is not None:
            self._incoming_call_dialog.close()
            self._incoming_call_dialog = None
        dlg = _IncomingCallDialog(caller_name, parent=self)
        dlg.accepted_call.connect(lambda: self._accept_incoming_video_call(call_id, profile))
        dlg.rejected_call.connect(lambda: self._reject_incoming_video_call(call_id))
        self._incoming_call_dialog = dlg
        dlg.show()

    def _accept_incoming_video_call(self, call_id: int, profile):
        self._incoming_call_dialog = None
        if not self.client.connected:
            return
        local_profile = VideoProfile(width=640, height=480, fps=30, target_bitrate_kbps=1000)
        if not self.client.send_video_call_response(call_id, True, local_profile):
            self._show_info("接听失败。", "warning")

    def _reject_incoming_video_call(self, call_id: int):
        self._incoming_call_dialog = None
        if self.client.connected:
            self.client.send_video_call_response(call_id, False, reason="declined")

    def _handle_video_call_established(self, call_id: int, peer_id: int, profile):
        _log_main(f"[VIDEO_CALL] established call_id={call_id} peer={peer_id}")
        if self._incoming_call_dialog is not None:
            self._incoming_call_dialog.close()
            self._incoming_call_dialog = None
        if self._video_call_window is not None:
            self._video_call_window.close()
            self._video_call_window = None

        try:
            host = self.client._sock.getpeername()[0] if self.client._sock else "127.0.0.1"
        except Exception:
            host = "127.0.0.1"
        video_port = self.client.server_video_udp_port or 5174
        session_key = self.client.session_key or b""
        user_id = self.client.user_id or 0

        ok, err = self.video_call_engine.start_call(
            call_id=call_id,
            server_addr=(host, video_port),
            user_id=user_id,
            session_key=session_key,
            profile=profile,
            camera_index=0,
        )
        if not ok:
            self._show_info(f"启动视频通话引擎失败: {err}", "warning")
            self.client.send_video_call_hangup(call_id, reason=1)
            return

        peer_name = f"User {peer_id}"
        for u in self.client.channel_users:
            if u.get("id") == peer_id:
                peer_name = u.get("username", peer_name)
                break

        win = VideoCallWindow(peer_name, parent=self)
        win.set_local_user_id(user_id)
        win.set_camera_devices(CameraCapture.enumerate_devices())
        win.hangup_requested.connect(lambda: self._hangup_video_call(call_id))
        win.video_mute_toggled.connect(lambda muted: self.video_call_engine.set_muted_video(muted))
        win.camera_changed.connect(lambda idx: self.video_call_engine.set_camera_device(idx))
        win.back_requested.connect(lambda: self._hangup_video_call(call_id))
        self._video_call_window = win
        win.show()

    def _hangup_video_call(self, call_id: int):
        _log_main(f"[VIDEO_CALL] hangup call_id={call_id}")
        self.client.send_video_call_hangup(call_id)
        self._cleanup_video_call_ui()

    def _handle_video_call_ended(self, call_id: int, reason: int):
        _log_main(f"[VIDEO_CALL] ended call_id={call_id} reason={reason}")
        self._cleanup_video_call_ui()
        self._show_info("视频通话已结束。", "info")

    def _handle_video_call_error(self, call_id: int, msg: str):
        _log_main(f"[VIDEO_CALL] error call_id={call_id}: {msg}")
        self._cleanup_video_call_ui()
        self._show_info(f"视频通话错误: {msg}", "warning")

    def _cleanup_video_call_ui(self):
        try:
            self.video_call_engine.stop_call()
        except Exception as e:
            _log_main(f"[VIDEO_CALL] stop_call error: {e}")
        if self._video_call_window is not None:
            try:
                self._video_call_window.close()
            except Exception:
                pass
            self._video_call_window = None
        if self._incoming_call_dialog is not None:
            try:
                self._incoming_call_dialog.close()
            except Exception:
                pass
            self._incoming_call_dialog = None

    def _handle_video_call_frame(self, sender_id: int, frame_bgr, width: int, height: int):
        if self._video_call_window is not None:
            self._video_call_window.on_video_frame(sender_id, frame_bgr, width, height)

    # Screen-share video frame (separate from video call)
    def _on_share_video_frame(self, sender_id, frame_bgr, width, height):
        # v2 does not render shared screen inline; ignored for now.
        pass

    # ───────────────────────── Settings ─────────────────────────
    def _on_open_settings(self):
        if self._settings_window is None:
            self._settings_window = SettingsWindow(self.audio_manager, parent=self)
            self._settings_window.back_requested.connect(self._settings_window.hide)
            self._settings_window.input_mode_changed.connect(self._on_input_mode_changed)
        self._settings_window.show()
        self._settings_window.raise_()
        self._settings_window.activateWindow()

    # ───────────────────────── Callback handlers ─────────────────────────
    def _handle_state_changed(self, new_state: int, old_state: int):
        state = ClientState(new_state)
        self.channel_sidebar.set_connected(state >= ClientState.Connected)
        self.voice_panel.set_connected(state >= ClientState.InChannel)

        if state == ClientState.Connected:
            self.chat_panel.set_local_user(self.client.user_id, self.client.username)
            self.chat_panel.add_system_message("已连接到服务器。")
            self.chat_panel.set_input_enabled(False)
            self.channel_sidebar.set_user_info(self.client.user_id, self.client.is_admin)
            self.header.set_admin(self.client.is_admin)
            self._refresh_self_avatar()
            try:
                udp_host = self.client._sock.getpeername()[0] if self.client._sock else "127.0.0.1"
                udp_port = self.client.server_udp_port or 5173
                video_udp_port = self.client.server_video_udp_port or (udp_port + 1)
                self.voice_engine.set_server_udp(udp_host, udp_port)
                self.voice_engine.set_user_info(self.client.user_id, 0)
                self.video_engine.set_server_udp(udp_host, video_udp_port)
                self.video_engine.set_user_info(self.client.user_id, 0)
                if self.client.session_key:
                    self.voice_engine.set_session_key(self.client.session_key)
                    self.video_engine.set_session_key(self.client.session_key)
                self.voice_engine.on_voice_received = lambda uid: self._signals.user_speaking.emit(uid, True)
                _log_main("Connected: starting voice_engine...")
                self.voice_engine.start()
                _log_main("Connected: voice_engine started OK")
                _log_main(f"Connected: video_engine server={udp_host}:{video_udp_port}")
                self.video_engine.start_receive()
            except Exception as e:
                _log_main(f"Connected: engines start FAILED: {e}")
                traceback.print_exc()
        elif state == ClientState.InChannel:
            self.chat_panel.set_input_enabled(True)
            self.chat_panel.add_system_message(f"已加入频道: {self.client.current_channel_name}")
            self.channel_sidebar.set_current_channel(self.client.current_channel_id)
            self.header.set_channel(self.client.current_channel_name)
            try:
                join_sound.play_join_sound()
            except Exception:
                pass
            try:
                self.voice_engine.set_user_info(self.client.user_id, self.client.current_channel_id)
                self.video_engine.set_user_info(self.client.user_id, self.client.current_channel_id)
                if hasattr(self.voice_engine, "_send_registration_packet"):
                    self.voice_engine._send_registration_packet()
                if hasattr(self.video_engine, "_send_registration_packet"):
                    self.video_engine._send_registration_packet()
                for u in self.client.channel_users:
                    uid = u.get("id", 0)
                    if uid != self.client.user_id:
                        self.voice_engine.add_remote_user(uid)
                self._refresh_voice_users()
                self._restore_per_user_settings()
            except Exception as e:
                _log_main(f"InChannel: engines failed: {e}")
                traceback.print_exc()
        elif state == ClientState.Disconnected:
            try:
                self._save_per_user_settings()
                join_sound.play_disconnect_sound()
            except Exception:
                pass
            self.chat_panel.set_input_enabled(False)
            self.chat_panel.add_system_message("已断开连接。")
            self.chat_panel.clear_messages()
            self.channel_sidebar.update_channels([], 0)
            self.channel_sidebar.set_current_channel(0)
            self.channel_sidebar.set_muted(False)
            self.channel_sidebar.set_deafened(False)
            self.voice_panel.set_channel_users([], self.client.user_id)
            self.voice_panel.set_connected(False)
            self.header.set_channel("未加入频道")
            try:
                self.video_engine.stop_receive()
                self.voice_engine.stop()
            except Exception:
                pass

    def _handle_channel_list(self, channels: list):
        av = None
        if self.avatar_manager.has_avatar:
            av = self.avatar_manager.get_pixmap(28)
        self.channel_sidebar.update_channels(channels, self.client.current_channel_id, local_avatar=av)
        self._restore_per_user_settings()
        self._refresh_voice_users()

    def _refresh_voice_users(self):
        users = []
        for ch in (self.client.channels or []):
            if ch.get("id") == self.client.current_channel_id:
                users = ch.get("users", [])
                break
        if not users:
            users = self.client.channel_users
        av = None
        if self.avatar_manager.has_avatar:
            av = self.avatar_manager.get_pixmap(36)
        self.voice_panel.set_local_avatar(av)
        self.voice_panel.set_channel_users(users, self.client.user_id, self.client.current_channel_name)

    def _handle_user_joined(self, user: dict):
        self.chat_panel.add_system_message(f"{user.get('username', '')} 加入了频道。")
        if user.get("id", 0) != self.client.user_id:
            try:
                join_sound.play_join_sound()
                uid = user.get("id", 0)
                if uid:
                    self.voice_engine.add_remote_user(uid)
                self._restore_per_user_settings()
            except Exception:
                pass
        self._refresh_voice_users()

    def _handle_user_left(self, user_id: int):
        username = ""
        for u in self.client.channel_users:
            if u.get("id") == user_id:
                username = u.get("username", "")
                break
        if username:
            self.chat_panel.add_system_message(f"{username} 离开了频道。")
        try:
            self._save_per_user_settings(user_id)
            self.voice_engine.remove_remote_user(user_id)
        except Exception:
            pass
        self._refresh_voice_users()

    def _handle_user_speaking(self, user_id: int, speaking: bool):
        if user_id == self.client.user_id:
            return
        self.channel_sidebar.set_speaking(user_id, speaking)
        self.voice_panel.set_speaking(user_id, speaking)

    def _handle_chat_message(self, sender_id: int, sender_name: str,
                             channel_id: int, text: str, timestamp: int):
        is_self = sender_id == self.client.user_id
        avatar_pixmap = None
        if is_self and self.avatar_manager.has_avatar:
            avatar_pixmap = self.avatar_manager.get_pixmap(40)
        self.chat_panel.add_message(sender_id, sender_name, text, timestamp, is_self,
                                    avatar_pixmap=avatar_pixmap)

    def _handle_server_message(self, text: str):
        self._show_info(text, "info")

    def _handle_error(self, code: int, message: str):
        self._show_info(message, "error")

    def _handle_admin_auth_result(self, success: bool, message: str):
        if success:
            self.channel_sidebar.set_user_info(self.client.user_id, True)
            self.header.set_admin(True)
            self._show_info(message or "管理员认证成功。", "success")
        else:
            self._show_info(message or "管理员密码错误。", "warning")

    def _handle_admin_action_result(self, success: bool, message: str):
        kind = "success" if success else "warning"
        self._show_info(message or ("操作成功。" if success else "操作失败。"), kind)

    def _handle_file_upload_response(self, file_id: int, success: bool, message: str):
        if success and self._pending_file_upload is not None:
            src_path, filename, is_image = self._pending_file_upload
            try:
                # 本地即时显示（上传者本机可见）
                import nevo_client
                nevo_client.cache_source_file(str(file_id), src_path)
            except Exception:
                pass
            # 登记为文件所有者：缓存 + 响应频道内其他客户端的取回请求
            self.client.register_owned_file(file_id, src_path, filename)
            # 发送 [IMG:id] / [FILE:id:name] 聊天标记（与 v1 语义一致）
            self.chat_panel.handle_upload_response(str(file_id), filename, is_image)
            # 真实数据通道：向频道广播文件字节分片（后台线程）
            self.client.upload_file_data(file_id, src_path, filename)
            self.chat_panel.add_system_message(f"文件已上传: {filename}")
            self._pending_file_upload = None
        elif not success:
            self.chat_panel.add_system_message(f"上传失败: {message}")

    def _on_file_download(self, file_id: str):
        """点击图片/文件卡片且本地无数据时：请求取回真实字节流。"""
        try:
            self.chat_panel.add_system_message("正在取回文件...")
            self.client.download_file(int(file_id))
        except Exception:
            pass

    def _handle_file_received(self, file_id: int, path: str, filename: str):
        """文件字节流重组完成并写盘：刷新聊天区中的图片/文件卡片。"""
        self.chat_panel.on_file_cached(str(file_id))

    def _handle_file_error(self, file_id: int, message: str):
        self.chat_panel.add_system_message(f"文件不可用: {message}")

    def _handle_latency_update(self, latency_ms: int):
        self.voice_panel.set_latency(latency_ms)

    # ───────────────────────── PTT / VAD ─────────────────────────
    def _setup_ptt(self):
        self._ptt_key_str = self.audio_manager.ptt_key
        self._ptt_key_codes = self._parse_ptt_key(self._ptt_key_str)
        self._ptt_held_keys = set()
        try:
            from pynput import keyboard
            self._ptt_listener = keyboard.Listener(
                on_press=self._on_ptt_press,
                on_release=self._on_ptt_release,
            )
            self._ptt_listener.daemon = True
            self._ptt_listener.start()
        except ImportError:
            self._ptt_listener = None

    @staticmethod
    def _parse_ptt_key(key_str: str) -> set:
        _KEY_MAP = {
            "ctrl": "ctrl_l", "alt": "alt_l", "shift": "shift_l",
            "space": "space", "enter": "enter", "tab": "tab",
            "esc": "esc", "backspace": "backspace",
        }
        parts = [p.strip().lower() for p in (key_str or "").split("+") if p.strip()]
        return set(_KEY_MAP.get(p, p) for p in parts)

    def _key_name(self, key) -> str:
        try:
            from pynput import keyboard
            if isinstance(key, keyboard.Key):
                return key.name
            elif hasattr(key, "char") and key.char:
                return key.char.lower()
            elif hasattr(key, "vk") and key.vk:
                return str(key.vk)
        except Exception:
            pass
        return ""

    def _on_ptt_press(self, key):
        name = self._key_name(key)
        if name:
            self._ptt_held_keys.add(name)
        if self.audio_manager.input_mode == InputMode.PTT:
            if self._ptt_key_codes and self._ptt_key_codes.issubset(self._ptt_held_keys):
                if not self.audio_manager.ptt_active:
                    self.audio_manager.set_ptt_active(True)
                    self._update_ptt_ui(True)

    def _on_ptt_release(self, key):
        name = self._key_name(key)
        self._ptt_held_keys.discard(name)
        if self.audio_manager.input_mode == InputMode.PTT:
            if self.audio_manager.ptt_active and not self._ptt_key_codes.issubset(self._ptt_held_keys):
                self.audio_manager.set_ptt_active(False)
                self._update_ptt_ui(False)

    def _update_ptt_ui(self, active: bool):
        self.channel_sidebar.set_speaking(self.client.user_id, active)
        self.voice_panel.set_speaking(self.client.user_id, active)
        try:
            self.client.send_speaking_state(active)
        except Exception:
            pass

    def _handle_vad_speaking(self, speaking: bool):
        if self.audio_manager.input_mode != InputMode.VAD:
            return
        self.channel_sidebar.set_speaking(self.client.user_id, speaking)
        self.voice_panel.set_speaking(self.client.user_id, speaking)
        try:
            self.client.send_speaking_state(speaking)
        except Exception:
            pass

    def _on_input_mode_changed(self, mode: str):
        if mode == InputMode.PTT:
            self._ptt_key_str = self.audio_manager.ptt_key
            self._ptt_key_codes = self._parse_ptt_key(self._ptt_key_str)

    # ───────────────────────── Per-user settings persistence ─────────────────────────
    def _save_per_user_settings(self, user_id=None):
        try:
            host, port, _ = self._last_connect_params
            target_uid = user_id or self.client.user_id
            self.per_user_volume.save_settings(host, port, target_uid)
        except Exception:
            pass

    def _restore_per_user_settings(self):
        try:
            host, port, _ = self._last_connect_params
            user_ids = [u.get("id", 0) for u in self.client.channel_users
                        if u.get("id") != self.client.user_id]
            self.per_user_volume.restore_settings(host, port, user_ids)
            for uid in user_ids:
                muted = self.voice_engine.is_user_local_muted(uid)
                self.channel_sidebar.set_local_muted(uid, muted)
                self.voice_panel.set_local_muted(uid, muted)
        except Exception:
            pass

    # ───────────────────────── Avatar / theme ─────────────────────────
    def _refresh_self_avatar(self):
        pix = None
        if self.avatar_manager.has_avatar:
            pix = self.avatar_manager.get_pixmap(32)
        self.channel_sidebar.set_self_user(self.client.username or "用户", pix)

    def _on_avatar_changed(self):
        self._refresh_self_avatar()
        self._refresh_voice_users()
        if self.avatar_manager.has_avatar:
            pix = self.avatar_manager.get_pixmap(40)
            self.chat_panel.refresh_avatars(self.client.user_id, pix)

    def _on_theme_changed(self, is_dark: bool):
        self.setStyleSheet(v2_qss())
        self._refresh_theme_styles()

    def _refresh_theme_styles(self):
        self.title_bar.refresh_theme()
        self.header.refresh_theme()
        self.server_sidebar.refresh_theme()
        self.channel_sidebar.refresh_theme()
        self.chat_panel.refresh_theme()
        self.voice_panel.refresh_theme()
        if self._video_call_window is not None:
            self._video_call_window.refresh_theme()
        if self._settings_window is not None:
            self._settings_window.refresh_theme()

    # ───────────────────────── Helpers ─────────────────────────
    def _show_info(self, message: str, kind: str = "info"):
        method = {
            "info": InfoBar.info,
            "success": InfoBar.success,
            "warning": InfoBar.warning,
            "error": InfoBar.error,
        }.get(kind, InfoBar.info)
        try:
            method(self, "NEVO", message, parent=self,
                   position=InfoBarPosition.TOP, duration=3000)
        except Exception:
            pass

    # ───────────────────────── Close ─────────────────────────
    def closeEvent(self, event):
        _log_main("[V2 MAIN] closeEvent — cleaning up")
        try:
            if self._settings_window is not None:
                self._settings_window.cleanup()
        except Exception:
            pass
        try:
            self._cleanup_video_call_ui()
        except Exception:
            pass
        try:
            self.video_engine.stop_receive()
        except Exception:
            pass
        try:
            self.voice_engine.stop()
        except Exception:
            pass
        try:
            self.client.disconnect()
        except Exception:
            pass
        super().closeEvent(event)
