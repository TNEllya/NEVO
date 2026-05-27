"""Main window for the NEVO client GUI."""

import os
import sys
import threading
import traceback

from PyQt5.QtCore import Qt, pyqtSignal, QObject
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QFrame, QSplitter, QLabel,
)
from qfluentwidgets import (
    FluentWindow, NavigationItemPosition, FluentIcon,
    InfoBar, InfoBarPosition, TitleLabel, CaptionLabel,
    StrongBodyLabel, Dialog, LineEdit, TextEdit, PushButton,
    HeaderCardWidget, CardWidget, RoundMenu, Action,
)
from views.server_status import ServerStatusWidget

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from nevo_client import NevoClient, ClientState
from audio_manager import AudioManager, InputMode
from avatar_manager import AvatarManager
from voice_engine import VoiceEngine
from video_engine import VideoEngine
from screen_share_dialog import ScreenShareDialog
from views.channel_tree import ChannelTreeView
from views.chat_widget import ChatWidget
from views.connection_bar import ConnectionBar
from views.settings_page import SettingsPage
from views.screen_share_view import ScreenShareView
from views.voice_waveform import VoiceWaveformPanel
from views.server_quick_access import ServerQuickAccessPanel
from per_user_volume import PerUserVolumeManager, VolumeSliderDialog
from audio_share_engine import AudioShareEngine
from updater import Updater, UpdateState
from theme_manager import ThemeManager, card_stylesheet, scroll_stylesheet
import join_sound
import i18n

APP_NAME = "NEVO"
_MAIN_LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "main_debug.log")


def _log_main(msg: str):
    from datetime import datetime
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    line = f"[{ts}] [MAIN] {msg}"
    print(line)
    try:
        with open(_MAIN_LOG, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass


class _CallbackSignalHelper(QObject):
    """Helper to marshal callbacks from the network thread to the UI thread."""
    state_changed = pyqtSignal(int, int)       # new_state, old_state
    channel_list = pyqtSignal(list)             # channels
    user_joined = pyqtSignal(dict)              # user info
    user_left = pyqtSignal(int)                 # user_id
    user_speaking = pyqtSignal(int, bool)       # user_id, speaking
    chat_message = pyqtSignal(int, str, int, str, int)  # sender_id, name, ch_id, text, ts
    server_message = pyqtSignal(str)            # text
    error = pyqtSignal(int, str)                # code, message
    admin_auth_result = pyqtSignal(bool, str)   # success, message
    admin_action_result = pyqtSignal(bool, str)  # success, message
    file_upload_response = pyqtSignal(int, bool, str)  # file_id, success, message
    vad_speaking_changed = pyqtSignal(bool)     # speaking
    latency_update = pyqtSignal(int)            # latency_ms
    video_frame = pyqtSignal(int, object, int, int)  # sender_id, frame, width, height


class AdminPasswordDialog(Dialog):
    """Dialog for entering admin password."""

    def __init__(self, parent=None):
        super().__init__(parent.tr("Admin Authentication"), "", parent)
        self.yesButton.setText(self.tr("Authenticate"))
        self.cancelButton.setText(self.tr("Cancel"))

        layout = QVBoxLayout()
        layout.setSpacing(8)

        layout.addWidget(StrongBodyLabel(
            self.tr("Enter the admin password to access server management:")
        ))

        self.pwd_edit = LineEdit()
        self.pwd_edit.setEchoMode(LineEdit.Password)
        self.pwd_edit.setPlaceholderText(self.tr("Admin password"))
        self.pwd_edit.setMinimumWidth(280)
        layout.addWidget(self.pwd_edit)

        self.contentLabel.hide()
        self.textLayout.insertLayout(self.textLayout.count(), layout)
        self.setFixedSize(360, 280)

    def get_password(self) -> str:
        return self.pwd_edit.text().strip()


class CreateChannelDialog(Dialog):
    """Dialog for creating a new channel."""

    def __init__(self, parent=None):
        super().__init__(parent.tr("Create Channel"), "", parent)
        self.yesButton.setText(self.tr("Create"))
        self.cancelButton.setText(self.tr("Cancel"))

        layout = QVBoxLayout()
        layout.setSpacing(8)

        layout.addWidget(StrongBodyLabel(
            self.tr("Enter the new channel name:")
        ))

        self.name_edit = LineEdit()
        self.name_edit.setPlaceholderText(self.tr("Channel name"))
        self.name_edit.setMinimumWidth(280)
        layout.addWidget(self.name_edit)

        self.contentLabel.hide()
        self.textLayout.insertLayout(self.textLayout.count(), layout)
        self.setFixedSize(360, 280)

    def get_name(self) -> str:
        return self.name_edit.text().strip()


class SetServerNameDialog(Dialog):
    """Dialog for setting server name."""

    def __init__(self, current_name: str = "", parent=None):
        super().__init__(parent.tr("Set Server Name"), "", parent)
        self.yesButton.setText(self.tr("Apply"))
        self.cancelButton.setText(self.tr("Cancel"))

        layout = QVBoxLayout()
        layout.setSpacing(8)

        layout.addWidget(StrongBodyLabel(
            self.tr("Enter the new server name:")
        ))

        self.name_edit = LineEdit()
        self.name_edit.setPlaceholderText(self.tr("Server name"))
        self.name_edit.setText(current_name)
        self.name_edit.setMinimumWidth(280)
        layout.addWidget(self.name_edit)

        self.contentLabel.hide()
        self.textLayout.insertLayout(self.textLayout.count(), layout)
        self.setFixedSize(360, 280)

    def get_name(self) -> str:
        return self.name_edit.text().strip()


class MainWindow(FluentWindow):
    """NEVO client main window with Fluent Design."""

    def __init__(self):
        super().__init__()
        _log_main("=" * 60)
        _log_main("[MAIN WINDOW] INITIALIZED - v2025.05 (cross-device screen sharing fix)")
        _log_main("=" * 60)

        self.client = NevoClient()
        self._signals = _CallbackSignalHelper()

        # Initialize translations BEFORE creating any widgets that use self.tr()
        if getattr(sys, 'frozen', False):
            base_dir = sys._MEIPASS
        else:
            base_dir = os.path.dirname(os.path.abspath(__file__))
        translations_dir = os.path.join(base_dir, "translations")
        i18n.init_translations(translations_dir)
        saved_lang = i18n.load_language_preference(APP_NAME)
        i18n.load_language(saved_lang)

        self.audio_manager = AudioManager()
        self.avatar_manager = AvatarManager()
        self.voice_engine = VoiceEngine()
        self.per_user_volume = PerUserVolumeManager(self.voice_engine)
        self.video_engine = VideoEngine()
        self.video_engine.on_video_frame = self._on_video_frame
        self.video_engine.on_share_state_changed = self._on_share_state_changed
        self.audio_share_engine = AudioShareEngine()
        self.audio_share_engine.on_share_state_changed = self._on_audio_share_state_changed
        self.screen_share_view = ScreenShareView()
        self.screen_share_view.setObjectName("screenSharePage")

        self.updater = Updater()
        self.updater.set_callbacks(on_state_changed=self._on_updater_state_changed)

        self._setup_ui()
        self._setup_callbacks()
        self._setup_navigation()
        self._setup_ptt()

        tm = ThemeManager.instance()
        tm.theme_changed.connect(self._on_theme_changed)

        self.resize(1100, 750)
        self.setMinimumSize(800, 550)

        from PyQt5.QtCore import QTimer
        QTimer.singleShot(3000, self._start_update_check)

    def _setup_ui(self):
        self.setWindowTitle("NEVO")

        # Central content widget
        central = QFrame()
        central.setObjectName("nevoCentralWidget")
        central_layout = QVBoxLayout(central)
        central_layout.setContentsMargins(0, 0, 0, 0)
        central_layout.setSpacing(0)

        # Main splitter: left (channels + users) | right (chat)
        splitter = QSplitter(Qt.Horizontal)

        # Channel tree card
        channel_card = HeaderCardWidget(self)
        channel_card.setTitle(self.tr("Channels"))
        channel_card.setStyleSheet(card_stylesheet())
        self.channel_tree = ChannelTreeView()
        self.channel_tree.join_channel_requested.connect(self._on_join_channel)
        self.channel_tree.leave_channel_requested.connect(self._on_leave_channel)
        self.channel_tree.rename_channel_requested.connect(
            lambda cid, cname: (_log_main(f"RENAME signal received: cid={cid} name='{cname}'"),
                                self._on_rename_channel(cid, cname))[1]
        )
        self.channel_tree.add_subchannel_requested.connect(
            lambda pid: (_log_main(f"ADD_SUB signal received: pid={pid}"),
                         self._on_add_subchannel(pid))[1]
        )
        self.channel_tree.delete_channel_requested.connect(
            lambda did: (_log_main(f"DELETE signal received: did={did}"),
                         self._on_delete_channel(did))[1]
        )
        self.channel_tree.volume_requested.connect(self._on_user_volume_requested)
        self.channel_tree.local_mute_requested.connect(self._on_user_local_mute_requested)
        channel_card.viewLayout.addWidget(self.channel_tree)

        # Voice waveform panel
        self.waveform_panel = VoiceWaveformPanel(self.voice_engine)
        self.waveform_panel.setMinimumHeight(80)

        # Left panel vertical splitter: channels | waveform
        left_splitter = QSplitter(Qt.Vertical)
        left_splitter.addWidget(channel_card)
        left_splitter.addWidget(self.waveform_panel)
        left_splitter.setSizes([320, 200])
        left_splitter.setChildrenCollapsible(False)

        splitter.addWidget(left_splitter)

        # Right panel: chat
        chat_card = HeaderCardWidget(self)
        chat_card.setTitle(self.tr("Chat"))
        chat_card.setStyleSheet(card_stylesheet())
        self.chat_widget = ChatWidget()
        self.chat_widget.chat_message_sent.connect(self._on_chat_send)
        self.chat_widget.file_upload_requested.connect(self._on_file_upload)
        self.chat_widget.image_upload_requested.connect(self._on_image_upload)
        self.chat_widget.set_input_enabled(False)
        chat_card.viewLayout.addWidget(self.chat_widget)
        splitter.addWidget(chat_card)

        splitter.setSizes([400, 600])

        central_layout.addWidget(splitter, 1)

        # Connection bar at bottom
        self.connection_bar = ConnectionBar()
        self.connection_bar.connect_requested.connect(self._on_connect)
        self.connection_bar.disconnect_requested.connect(self._on_disconnect)
        self.connection_bar.btn_mute.clicked.connect(self._on_mute_toggled)
        self.connection_bar.btn_deafen.clicked.connect(self._on_deafen_toggled)
        self.connection_bar.admin_action_requested.connect(self._on_admin_action)
        self.connection_bar.share_screen_requested.connect(self._on_share_screen)
        self.connection_bar.stop_screen_share_requested.connect(self._on_stop_share_screen)
        self.connection_bar.audio_share_requested.connect(self._on_audio_share)
        self.connection_bar.stop_audio_share_requested.connect(self._on_stop_audio_share)
        central_layout.addWidget(self.connection_bar)

        # Use a "home" view that shows the central content
        self._central_widget = central

        # Settings page
        self.settings_page = SettingsPage(self.audio_manager, self.avatar_manager,
                                          updater=self.updater)
        self.settings_page.setObjectName("settingsPage")



    def _setup_navigation(self):
        self.addSubInterface(
            self._central_widget,
            FluentIcon.HOME,
            self.tr("NEVO"),
            NavigationItemPosition.TOP,
        )

        self.addSubInterface(
            self.screen_share_view,
            FluentIcon.VIDEO,
            self.tr("Screen Share"),
            NavigationItemPosition.TOP,
        )

        # Quick access panel in navigation sidebar
        self.quick_access = ServerQuickAccessPanel()
        self.quick_access.connect_requested.connect(self._on_quick_access_connect)
        panel = self.navigationInterface.panel
        panel.vBoxLayout.insertWidget(2, self.quick_access, 1)
        panel.vBoxLayout.setStretchFactor(panel.scrollArea, 0)

        self.addSubInterface(
            self.settings_page,
            FluentIcon.SETTING,
            self.tr("Settings"),
            NavigationItemPosition.BOTTOM,
        )

        self.navigationInterface.addSeparator()
        self.navigationInterface.addItem(
            "language",
            FluentIcon.LANGUAGE,
            self.tr("Language"),
            onClick=self._show_language_menu,
            position=NavigationItemPosition.BOTTOM,
            selectable=False,
        )

    def _show_language_menu(self):
        """Show language selection menu."""
        menu = RoundMenu(parent=self)

        for lang in i18n.available_languages():
            display = i18n.language_display_name(lang)
            action = Action(display, triggered=lambda checked, l=lang: self._change_language(l))
            if lang == i18n.current_language():
                action.setEnabled(False)
            menu.addAction(action)

        menu.exec_(self.mapToGlobal(self.rect().bottomLeft()))

    def _change_language(self, lang: str):
        """Switch language and restart the UI."""
        i18n.load_language(lang)
        i18n.save_language_preference(lang, APP_NAME)

        InfoBar.info(
            self.tr("Language"),
            self.tr("Language changed. Please restart the application for full effect."),
            parent=self,
            position=InfoBarPosition.TOP,
            duration=3000,
        )

    def _setup_callbacks(self):
        """Wire client callbacks to UI signals."""
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
        self._signals.vad_speaking_changed.connect(self._handle_vad_speaking)
        self._signals.latency_update.connect(self._handle_latency_update)
        self._signals.video_frame.connect(self._handle_video_frame)

        # Client callbacks -> emit signals (thread-safe bridge)
        self.client.on_latency_update = lambda ms: self._signals.latency_update.emit(ms)

        # Client callbacks -> emit signals (thread-safe bridge)
        self.client.on_state_changed = lambda new, old: self._signals.state_changed.emit(int(new), int(old))
        self.client.on_channel_list = lambda ch: self._signals.channel_list.emit(ch)
        self.client.on_user_joined = lambda u: self._signals.user_joined.emit(u)
        self.client.on_user_left = lambda uid: self._signals.user_left.emit(uid)
        self.client.on_user_speaking = lambda uid, s: self._signals.user_speaking.emit(uid, s)
        self.client.on_chat_message = lambda sid, sn, cid, txt, ts: self._signals.chat_message.emit(sid, sn, cid, txt, ts)
        self.client.on_server_message = lambda txt: self._signals.server_message.emit(txt)
        self.client.on_error = lambda code, msg: self._signals.error.emit(code, msg)
        self.client.on_admin_auth_result = lambda ok, msg: self._signals.admin_auth_result.emit(ok, msg)
        self.client.on_admin_action_result = lambda ok, msg: self._signals.admin_action_result.emit(ok, msg)
        self.client.on_file_upload_response = lambda fid, ok, msg: self._signals.file_upload_response.emit(fid, ok, msg)

        # VAD callback -> signal (audio thread -> UI thread)
        self.audio_manager.on_vad_changed = lambda speaking: self._signals.vad_speaking_changed.emit(speaking)

        # Settings page input mode change
        self.settings_page.input_mode_changed.connect(self._on_input_mode_changed)

        # Avatar change -> refresh UI
        self.avatar_manager.avatar_changed.connect(self._on_avatar_changed)

    # ---- Connection actions ----

    def _on_connect(self, host: str, port: int, username: str, password: str):
        self._last_connect_params = (host, port, username)
        def connect_thread():
            success = self.client.connect(host, port, username, password,
                                          voice_engine=self.voice_engine,
                                          video_engine=self.video_engine)
            if not success:
                self._signals.error.emit(7, "Connection failed")

        thread = threading.Thread(target=connect_thread, daemon=True)
        thread.start()

    def _on_disconnect(self):
        self.client.disconnect()

    def _on_quick_access_connect(self, host: str, port: int, username: str):
        self.connection_bar.edit_host.setText(host)
        self.connection_bar.spin_port.setValue(port)
        self.connection_bar.edit_username.setText(username)
        self._on_connect(host, port, username, "")

    def _on_join_channel(self, channel_id: int):
        if not self.client.join_channel(channel_id):
            InfoBar.warning(
                self.tr("Cannot Join"),
                self.tr("Failed to join channel."),
                parent=self,
                position=InfoBarPosition.TOP,
                duration=3000,
            )

    def _on_leave_channel(self):
        self.client.leave_channel()

    def _on_mute_toggled(self):
        muted = self.connection_bar.btn_mute.isChecked()
        self.client.set_muted(muted)
        try:
            self.voice_engine.set_muted(muted)
        except Exception:
            pass
        if muted:
            self.connection_bar.btn_mute.setText(self.tr("开麦"))
        else:
            self.connection_bar.btn_mute.setText(self.tr("闭麦"))

    def _on_deafen_toggled(self):
        deafened = self.connection_bar.btn_deafen.isChecked()
        self.client.set_deafened(deafened)
        try:
            self.voice_engine.set_deafened(deafened)
        except Exception:
            pass
        if deafened:
            self.connection_bar.btn_deafen.setText(self.tr("取消禁言"))
        else:
            self.connection_bar.btn_deafen.setText(self.tr("禁言"))

    def _on_chat_send(self, text: str):
        self.client.send_chat(text)

    def _on_share_screen(self):
        _log_main("[SHARE_SCREEN] === _on_share_screen ENTERED ===")
        
        # 防御性检查：在开始屏幕共享前，重新确认并设置正确的服务器地址
        # 这是解决跨设备屏幕共享失败的关键修复点
        try:
            tcp_peer_host = None
            if self.client and self.client._sock:
                try:
                    tcp_peer_host = self.client._sock.getpeername()[0]
                    _log_main(f"[SHARE_SCREEN] TCP peer address: {tcp_peer_host}")
                except Exception as e:
                    _log_main(f"[SHARE_SCREEN] WARNING: getpeername failed: {e}")
            
            current_video_addr = getattr(self.video_engine, '_server_udp_addr', None)
            _log_main(f"[SHARE_SCREEN] Current video_engine._server_udp_addr = {current_video_addr}")
            
            # 如果当前地址是 127.0.0.1 但 TCP 连接指向其他地址，强制修正
            if current_video_addr and current_video_addr[0] in ('127.0.0.1', 'localhost') and tcp_peer_host and tcp_peer_host not in ('127.0.0.1', 'localhost'):
                _log_main(f"[SHARE_SCREEN] *** DETECTED WRONG ADDRESS ***: video_engine has {current_video_addr} but TCP peer is {tcp_peer_host}, FIXING...")
                udp_port = self.client.server_udp_port or 5173
                video_udp_port = getattr(self.client, 'server_video_udp_port', 0) or (udp_port + 1)
                self.video_engine.set_server_udp(tcp_peer_host, video_udp_port)
                _log_main(f"[SHARE_SCREEN] Fixed video_engine server address to ({tcp_peer_host}, {video_udp_port})")
            
            # 无论何种情况，都重新从 TCP 连接获取最新地址并设置（防御性双重确认）
            if tcp_peer_host:
                udp_port = self.client.server_udp_port or 5173
                video_udp_port = getattr(self.client, 'server_video_udp_port', 0) or (udp_port + 1)
                self.video_engine.set_server_udp(tcp_peer_host, video_udp_port)
                
                # 确保加密密钥也已设置
                if self.client.session_key:
                    self.video_engine.set_session_key(self.client.session_key)
                
                # 确保接收线程已启动
                if self.video_engine._recv_thread is None or not self.video_engine._recv_thread.is_alive():
                    _log_main("[SHARE_SCREEN] Starting video receive thread (was not running)")
                    self.video_engine.start_receive()
                
                _log_main(f"[SHARE_SCREEN] Re-confirmed server address: ({tcp_peer_host}, {video_udp_port})")
        except Exception as e:
            _log_main(f"[SHARE_SCREEN] ERROR during pre-share setup: {e}")
            import traceback
            traceback.print_exc()

        dialog = ScreenShareDialog(self)
        if dialog.exec_():
            config = dialog.get_config()
            if config:
                _log_main(f"[SHARE_SCREEN] Dialog config: {config}")
                final_addr = getattr(self.video_engine, '_server_udp_addr', None)
                _log_main(f"[SHARE_SCREEN] Final server_udp_addr before start_share: {final_addr}")
                
                success, error_msg = self.video_engine.start_share(
                    source_type=config["source_type"],
                    source_index=config["source_index"],
                    fps=config["fps"],
                )
                if not success:
                    _log_main(f"[SHARE_SCREEN] start_share FAILED: {error_msg}")
                    InfoBar.warning(
                        self.tr("Screen Share Failed"),
                        self.tr("Could not start screen sharing: {}").format(error_msg),
                        parent=self,
                        position=InfoBarPosition.TOP,
                        duration=5000,
                    )
                    self.connection_bar.set_sharing(False)
                else:
                    _log_main("[SHARE_SCREEN] start_share SUCCESS")

    def _on_stop_share_screen(self):
        self.video_engine.stop_share()

    def _on_share_state_changed(self, sharing):
        from PyQt5.QtCore import QMetaObject, Qt, Q_ARG
        QMetaObject.invokeMethod(
            self.connection_bar, "set_sharing",
            Qt.QueuedConnection,
            Q_ARG(bool, sharing)
        )

    def _on_audio_share(self, source_type):
        _log_main(f"[AUDIO_SHARE] source_type={source_type}")

        if not self.voice_engine._udp_sock or not self.voice_engine._server_udp_addr:
            InfoBar.warning(
                self.tr("Audio Share"),
                self.tr("Not connected to voice server"),
                parent=self,
                position=InfoBarPosition.TOP,
                duration=3000,
            )
            return

        self.audio_share_engine.set_udp_socket(self.voice_engine._udp_sock)
        self.audio_share_engine.set_server_addr(self.voice_engine._server_udp_addr)
        if hasattr(self.voice_engine, '_crypto') and self.voice_engine._crypto:
            self.audio_share_engine.set_crypto(self.voice_engine._crypto)
        self.audio_share_engine.set_user_info(self.client.user_id, 0)

        success, error_msg = self.audio_share_engine.start_share(source_type)
        if not success:
            InfoBar.warning(
                self.tr("Audio Share"),
                error_msg or self.tr("Failed to start audio sharing"),
                parent=self,
                position=InfoBarPosition.TOP,
                duration=4000,
            )
        else:
            source_label = self.tr("Application Audio") if source_type == "app" else self.tr("System Audio")
            InfoBar.success(
                self.tr("Audio Share"),
                self.tr("Sharing {}").format(source_label),
                parent=self,
                position=InfoBarPosition.TOP,
                duration=2000,
            )

    def _on_stop_audio_share(self):
        self.audio_share_engine.stop_share()
        InfoBar.info(
            self.tr("Audio Share"),
            self.tr("Audio sharing stopped"),
            parent=self,
            position=InfoBarPosition.TOP,
            duration=2000,
        )

    def _on_audio_share_state_changed(self, sharing):
        from PyQt5.QtCore import QMetaObject, Qt, Q_ARG
        QMetaObject.invokeMethod(
            self.connection_bar, "set_audio_sharing",
            Qt.QueuedConnection,
            Q_ARG(bool, sharing)
        )

    def _on_video_frame(self, sender_id, frame_bgr, width, height):
        # Emit signal to safely transfer frame data from capture thread to UI thread
        try:
            self._signals.video_frame.emit(sender_id, frame_bgr.copy(), width, height)
        except Exception as e:
            _log_main(f"[VIDEO_FRAME] emit failed: {e}")

    def _handle_video_frame(self, sender_id, frame_bgr, width, height):
        # This runs on UI thread, safe to update UI components
        self.screen_share_view.on_video_frame(sender_id, frame_bgr, width, height)

    # ---- Callback handlers (called on UI thread via signals) ----

    def _handle_state_changed(self, new_state: int, old_state: int):
        state = ClientState(new_state)
        self.connection_bar.set_connected(state >= ClientState.Connected)

        if state == ClientState.Connected:
            self.chat_widget.set_local_user(self.client.user_id, self.client.username)
            self.chat_widget.add_system_message(self.tr("Connected to server."))
            self.chat_widget.set_input_enabled(False)
            self.connection_bar.btn_admin_login.setEnabled(True)
            self.channel_tree.set_user_info(self.client.user_id, self.client.is_admin)
            self.server_status.set_connected(True)
            self.server_status.start_monitoring()
            if hasattr(self, '_last_connect_params'):
                self.quick_access.add_recent(*self._last_connect_params)
            try:
                udp_host = self.client._sock.getpeername()[0] if self.client._sock else "127.0.0.1"
                udp_port = self.client.server_udp_port or 5173
                video_udp_port = getattr(self.client, 'server_video_udp_port', 0) or (5174 if udp_port == 5173 else udp_port + 1)
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
                self.waveform_panel.set_connected(True)
                _log_main(f"Connected: video_engine server={udp_host}:{video_udp_port}, starting receive...")
                self.video_engine.start_receive()
                _log_main("Connected: video_engine receive started OK")
            except Exception as e:
                _log_main(f"Connected: voice_engine or video_engine start FAILED: {e}")
                traceback.print_exc()
        elif state == ClientState.InChannel:
            self.chat_widget.set_input_enabled(True)
            self.chat_widget.add_system_message(
                self.tr("Joined channel: {}").format(self.client.current_channel_name)
            )
            self.channel_tree.set_current_channel(self.client.current_channel_id)
            join_sound.play_join_sound()
            try:
                self.voice_engine.set_user_info(self.client.user_id, self.client.current_channel_id)
                self.video_engine.set_user_info(self.client.user_id, self.client.current_channel_id)
                # 确保发送注册包
                if hasattr(self.voice_engine, "_send_registration_packet"):
                    self.voice_engine._send_registration_packet()
                if hasattr(self.video_engine, "_send_registration_packet"):
                    self.video_engine._send_registration_packet()
                for u in self.client.channel_users:
                    uid = u.get("id", 0)
                    if uid != self.client.user_id:
                        self.voice_engine.add_remote_user(uid)
                self.waveform_panel.set_channel_users(
                    self.client.channel_users,
                    self.client.user_id,
                    self.client.current_channel_name,
                )
                self._restore_per_user_settings()
            except Exception as e:
                _log_main(f"InChannel: engines failed: {e}")
                traceback.print_exc()
        elif state == ClientState.Disconnected:
            self._save_per_user_settings()
            join_sound.play_disconnect_sound()
            self.waveform_panel.set_connected(False)
            self.chat_widget.set_input_enabled(False)
            self.chat_widget.add_system_message(self.tr("Disconnected."))
            self.channel_tree.update_channels([])
            self.channel_tree.set_current_channel(0)
            self.connection_bar.btn_mute.setChecked(False)
            self.connection_bar.btn_deafen.setChecked(False)
            self.connection_bar.btn_mute.setText(self.tr("闭麦"))
            self.connection_bar.btn_deafen.setText(self.tr("禁言"))
            self.connection_bar.btn_admin_login.setEnabled(False)
            self.server_status.set_connected(False)
            self.server_status.stop_monitoring()
            try:
                self.video_engine.stop_share()
                self.video_engine.stop_receive()
            except Exception:
                pass
            try:
                self.audio_share_engine.stop_share()
            except Exception:
                pass
            self.screen_share_view.clear()
            try:
                self.voice_engine.stop()
            except Exception:
                pass

    def _handle_channel_list(self, channels: list):
        av = None
        if self.avatar_manager.has_avatar:
            av = self.avatar_manager.get_pixmap(28)
        self.channel_tree.update_channels(channels, self.client.current_channel_id,
                                          local_avatar=av)
        self._restore_per_user_settings()
        self._refresh_waveform_users(channels)

    def _refresh_waveform_users(self, channels: list):
        users = []
        for ch in (channels or []):
            if ch.get("id") == self.client.current_channel_id:
                users = ch.get("users", [])
                break
        if not users:
            users = self.client.channel_users
        self.waveform_panel.set_channel_users(
            users,
            self.client.user_id,
            self.client.current_channel_name,
        )

    def _handle_user_joined(self, user: dict):
        self.chat_widget.add_system_message(
            self.tr("{} joined the channel.").format(user.get("username", ""))
        )
        if user.get("id", 0) != self.client.user_id:
            join_sound.play_join_sound()
        try:
            uid = user.get("id", 0)
            if uid and uid != self.client.user_id:
                self.voice_engine.add_remote_user(uid)
                self._restore_per_user_settings()
        except Exception:
            pass
        self.waveform_panel.set_channel_users(
            self.client.channel_users,
            self.client.user_id,
            self.client.current_channel_name,
        )

    def _handle_user_left(self, user_id: int):
        username = ""
        for u in self.client.channel_users:
            if u["id"] == user_id:
                username = u.get("username", "")
                break
        if username:
            self.chat_widget.add_system_message(
                self.tr("{} left the channel.").format(username)
            )
        try:
            self._save_per_user_settings(user_id)
            self.voice_engine.remove_remote_user(user_id)
        except Exception:
            pass
        remaining = [u for u in self.client.channel_users if u.get("id") != user_id]
        self.waveform_panel.set_channel_users(
            remaining,
            self.client.user_id,
            self.client.current_channel_name,
        )

    def _handle_user_speaking(self, user_id: int, speaking: bool):
        if user_id == self.client.user_id:
            return
        self.channel_tree.set_speaking(user_id, speaking)

    def _handle_chat_message(self, sender_id: int, sender_name: str,
                              channel_id: int, text: str, timestamp: int):
        is_self = sender_id == self.client.user_id
        avatar_pixmap = None
        if is_self and self.avatar_manager.has_avatar:
            avatar_pixmap = self.avatar_manager.get_pixmap(36)
        self.chat_widget.add_message(sender_id, sender_name, text, timestamp, is_self,
                                     avatar_pixmap=avatar_pixmap)

    def _handle_server_message(self, text: str):
        InfoBar.info(
            self.tr("Server Message"), text,
            parent=self, position=InfoBarPosition.TOP,
            duration=5000,
        )

    def _handle_error(self, code: int, message: str):
        InfoBar.error(
            self.tr("Error"), message,
            parent=self, position=InfoBarPosition.TOP,
            duration=5000,
        )

    def _handle_admin_auth_result(self, success: bool, message: str):
        print(f"[DEBUG] _handle_admin_auth_result: success={success}, message={message}")
        try:
            if success:
                self.connection_bar.set_admin_authenticated(True)
                InfoBar.success(
                    self.tr("Admin Authenticated"),
                    message or self.tr("You now have admin privileges."),
                    parent=self,
                    position=InfoBarPosition.TOP,
                    duration=3000,
                )
            else:
                InfoBar.warning(
                    self.tr("Auth Failed"),
                    message or self.tr("Incorrect admin password."),
                    parent=self,
                    position=InfoBarPosition.TOP,
                    duration=3000,
                )
        except Exception as e:
            print(f"[ERROR] _handle_admin_auth_result exception: {e}")
            import traceback
            traceback.print_exc()

    def _handle_admin_action_result(self, success: bool, message: str):
        if success:
            InfoBar.success(
                self.tr("Success"),
                message or self.tr("Action completed."),
                parent=self,
                position=InfoBarPosition.TOP,
                duration=3000,
            )
        else:
            InfoBar.warning(
                self.tr("Failed"),
                message or self.tr("Action failed."),
                parent=self,
                position=InfoBarPosition.TOP,
                duration=3000,
            )

    def _handle_latency_update(self, latency_ms: int):
        try:
            self.connection_bar.set_latency(latency_ms)
        except Exception:
            pass

    def _on_user_context_action(self, user_id: int, action: str):
        if action == "set_admin":
            self.client.send_set_admin(user_id, True)
        elif action == "remove_admin":
            self.client.send_set_admin(user_id, False)
        elif action == "kick":
            self.client.send_kick_user(user_id)
        elif action == "ban":
            self.client.send_ban_user(user_id)

    def _on_user_volume_requested(self, user_id: int, username: str):
        current_vol = self.voice_engine.get_user_volume(user_id)
        dialog = VolumeSliderDialog(username, current_vol, self)
        dialog.volume_changed.connect(
            lambda vol: self.voice_engine.set_user_volume(user_id, vol)
        )
        if dialog.exec_():
            final_vol = dialog.get_volume()
            self.voice_engine.set_user_volume(user_id, final_vol)
            self._save_per_user_settings(user_id)
            InfoBar.info(
                self.tr("Volume"),
                self.tr("Volume for {} set to {}%").format(username, int(final_vol * 100)),
                parent=self,
                position=InfoBarPosition.TOP,
                duration=2000,
            )

    def _on_user_local_mute_requested(self, user_id: int, muted: bool):
        self.voice_engine.set_user_local_mute(user_id, muted)
        self.channel_tree.set_local_muted(user_id, muted)
        self._save_per_user_settings(user_id)

        username = ""
        for u in self.client.channel_users:
            if u.get("id") == user_id:
                username = u.get("username", "")
                break
        if muted:
            InfoBar.info(
                self.tr("Mute"),
                self.tr("{} has been locally muted").format(username),
                parent=self,
                position=InfoBarPosition.TOP,
                duration=2000,
            )
        else:
            InfoBar.info(
                self.tr("Mute"),
                self.tr("{} has been unmuted").format(username),
                parent=self,
                position=InfoBarPosition.TOP,
                duration=2000,
            )

    def _save_per_user_settings(self, user_id=None):
        try:
            host = self.connection_bar.edit_host.text().strip()
            port = self.connection_bar.spin_port.value()
            target_uid = user_id or self.client.user_id
            self.per_user_volume.save_settings(host, port, target_uid)
        except Exception:
            pass

    def _restore_per_user_settings(self):
        try:
            host = self.connection_bar.edit_host.text().strip()
            port = self.connection_bar.spin_port.value()
            user_ids = [u.get("id", 0) for u in self.client.channel_users
                       if u.get("id") != self.client.user_id]
            self.per_user_volume.restore_settings(host, port, user_ids)
            for uid in user_ids:
                muted = self.voice_engine.is_user_local_muted(uid)
                self.channel_tree.set_local_muted(uid, muted)
        except Exception:
            pass

    def _on_file_upload(self, file_path: str, file_size: int):
        if not self.client.connected:
            return
        if self.client.current_channel_id == 0:
            self.chat_widget.add_system_message(self.tr("Please join a channel first."))
            return
        self.client.send_file_upload_request(
            self.client.current_channel_id,
            os.path.basename(file_path),
            file_size,
        )
        self._pending_file_upload = (file_path, os.path.basename(file_path), False)

    def _on_image_upload(self, file_path: str, file_size: int):
        if not self.client.connected:
            return
        if self.client.current_channel_id == 0:
            self.chat_widget.add_system_message(self.tr("Please join a channel first."))
            return
        self.client.send_file_upload_request(
            self.client.current_channel_id,
            os.path.basename(file_path),
            file_size,
        )
        self._pending_file_upload = (file_path, os.path.basename(file_path), True)

    def _handle_file_upload_response(self, file_id: int, success: bool, message: str):
        if success and hasattr(self, "_pending_file_upload"):
            src_path, filename, is_image = self._pending_file_upload
            from views.chat_widget import _ImageLabel
            try:
                _ImageLabel.cache_image(str(file_id), src_path)
            except Exception:
                pass
            self.chat_widget.handle_upload_response(str(file_id), filename, is_image)
            del self._pending_file_upload
        elif not success:
            self.chat_widget.add_system_message(self.tr("Upload failed: {}").format(message))

    def _on_rename_channel(self, channel_id: int, old_name: str):
        _logf = r"C:\Users\yzd20\Desktop\NEVO\rename_debug.log"
        try:
            with open(_logf, "a", encoding="utf-8") as _f:
                _f.write(f"[RENAME_SLOT] ENTER cid={channel_id} name='{old_name}'\n")
            print(f"[DEBUG] _on_rename_channel called: channel_id={channel_id}, old_name='{old_name}'")
            from PyQt5.QtWidgets import QInputDialog, QLineEdit
            new_name, ok = QInputDialog.getText(
                self,
                self.tr("Rename Channel"),
                self.tr(f'Enter new name for "{old_name}":'),
                QLineEdit.Normal,
                old_name,
            )
            with open(_logf, "a", encoding="utf-8") as _f:
                _f.write(f"[RENAME_SLOT] QInputDialog returned ok={ok} name='{new_name}'\n")
            print(f"[DEBUG] _on_rename_channel: ok={ok}, new_name='{new_name}'")
            if ok and new_name.strip() and new_name.strip() != old_name:
                print(f"[DEBUG] _on_rename_channel: calling send_rename_channel")
                self.client.send_rename_channel(channel_id, new_name.strip())
                with open(_logf, "a", encoding="utf-8") as _f:
                    _f.write("[RENAME_SLOT] send_rename_channel called\n")
        except Exception as e:
            with open(_logf, "a", encoding="utf-8") as _f:
                _f.write(f"[RENAME_SLOT] EXCEPTION: {e}\n")
            print(f"[ERROR] _on_rename_channel exception: {e}")
            import traceback
            traceback.print_exc()

    def _on_add_subchannel(self, parent_id: int):
        _logf = r"C:\Users\yzd20\Desktop\NEVO\rename_debug.log"
        try:
            with open(_logf, "a", encoding="utf-8") as _f:
                _f.write(f"[ADD_SUB_SLOT] ENTER pid={parent_id}\n")
            from PyQt5.QtWidgets import QInputDialog, QLineEdit
            name, ok = QInputDialog.getText(
                self,
                self.tr("Add Sub-channel"),
                self.tr("Enter the new sub-channel name:"),
                QLineEdit.Normal,
                "",
            )
            with open(_logf, "a", encoding="utf-8") as _f:
                _f.write(f"[ADD_SUB_SLOT] QInputDialog returned ok={ok} name='{name}'\n")
            if ok and name.strip():
                self.client.send_create_channel(name.strip(), parent_id=parent_id)
                with open(_logf, "a", encoding="utf-8") as _f:
                    _f.write("[ADD_SUB_SLOT] send_create_channel called\n")
        except Exception as e:
            with open(_logf, "a", encoding="utf-8") as _f:
                _f.write(f"[ADD_SUB_SLOT] EXCEPTION: {e}\n")
            import traceback
            traceback.print_exc()

    def _on_delete_channel(self, channel_id: int):
        dialog = Dialog(
            self.tr("Delete Channel"),
            self.tr("Are you sure you want to delete this channel? This action cannot be undone."),
            self
        )
        dialog.yesButton.setText(self.tr("Delete"))
        dialog.cancelButton.setText(self.tr("Cancel"))
        if dialog.exec_():
            self.client.send_delete_channel(channel_id)

    def _on_admin_action(self, action: str):
        print(f"[DEBUG] _on_admin_action called: action={action}")
        try:
            if action == "login":
                print(f"[DEBUG] Creating AdminPasswordDialog...")
                dialog = AdminPasswordDialog(self)
                print(f"[DEBUG] Showing dialog (exec_)...")
                result = dialog.exec_()
                print(f"[DEBUG] Dialog closed with result={result}")
                if result:
                    pwd = dialog.get_password()
                    print(f"[DEBUG] Password entered, length={len(pwd) if pwd else 0}")
                    if pwd:
                        print(f"[DEBUG] Calling send_admin_auth...")
                        self.client.send_admin_auth(pwd)
                        print(f"[DEBUG] send_admin_auth returned")
            elif action == "create_channel":
                dialog = Dialog(
                    self.tr("Create Channel"),
                    self.tr("Enter the new channel name:"),
                    self,
                )
                dialog.yesButton.setText(self.tr("Create"))
                dialog.cancelButton.setText(self.tr("Cancel"))
                name_edit = LineEdit()
                name_edit.setPlaceholderText(self.tr("Channel name"))
                name_edit.setMinimumWidth(280)
                dialog.contentLabel.hide()
                dialog.textLayout.insertLayout(dialog.textLayout.count(), QVBoxLayout())
                dialog.textLayout.itemAt(dialog.textLayout.count() - 1).layout().addWidget(name_edit)
                if dialog.exec_():
                    name = name_edit.text().strip()
                    if name:
                        self.client.send_create_channel(name)
            elif action == "set_server_name":
                dialog = Dialog(
                    self.tr("Set Server Name"),
                    self.tr("Enter the new server name:"),
                    self,
                )
                dialog.yesButton.setText(self.tr("Apply"))
                dialog.cancelButton.setText(self.tr("Cancel"))
                name_edit = LineEdit()
                name_edit.setPlaceholderText(self.tr("Server name"))
                name_edit.setMinimumWidth(280)
                dialog.contentLabel.hide()
                dialog.textLayout.insertLayout(dialog.textLayout.count(), QVBoxLayout())
                dialog.textLayout.itemAt(dialog.textLayout.count() - 1).layout().addWidget(name_edit)
                if dialog.exec_():
                    name = name_edit.text().strip()
                    if name:
                        self.client.send_set_server_name(name)
        except Exception as e:
            print(f"[ERROR] _on_admin_action exception: {e}")
            import traceback
            traceback.print_exc()

    # ---- PTT / VAD ----

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
        parts = [p.strip().lower() for p in key_str.split("+")]
        return set(_KEY_MAP.get(p, p) for p in parts)

    def _key_name(self, key) -> str:
        try:
            from pynput import keyboard
            if isinstance(key, keyboard.Key):
                return key.name
            elif hasattr(key, 'char') and key.char:
                return key.char.lower()
            elif hasattr(key, 'vk') and key.vk:
                return str(key.vk)
        except Exception:
            pass
        return ""

    def _on_ptt_press(self, key):
        name = self._key_name(key)
        if name:
            self._ptt_held_keys.add(name)
        if self.audio_manager.input_mode == InputMode.PTT:
            if self._ptt_key_codes.issubset(self._ptt_held_keys):
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
        if active:
            self.connection_bar.btn_mute.setText(self.tr("PTT: ON"))
            self.channel_tree.set_speaking(self.client.user_id, True)
            self.client.send_speaking_state(True)
        else:
            self.connection_bar.btn_mute.setText(self.tr("PTT: OFF"))
            self.channel_tree.set_speaking(self.client.user_id, False)
            self.client.send_speaking_state(False)

    def _handle_vad_speaking(self, speaking: bool):
        if self.audio_manager.input_mode != InputMode.VAD:
            return
        self.channel_tree.set_speaking(self.client.user_id, speaking)
        self.client.send_speaking_state(speaking)

    def _on_input_mode_changed(self, mode: str):
        if mode == InputMode.CONTINUOUS:
            self.connection_bar.btn_mute.setText(self.tr("闭麦"))
            self.connection_bar.btn_mute.setChecked(False)
        elif mode == InputMode.PTT:
            self.connection_bar.btn_mute.setText(self.tr("PTT: OFF"))
            self._ptt_key_str = self.audio_manager.ptt_key
            self._ptt_key_codes = self._parse_ptt_key(self._ptt_key_str)
        elif mode == InputMode.VAD:
            self.connection_bar.btn_mute.setText(self.tr("闭麦"))
            self.connection_bar.btn_mute.setChecked(False)

    def _on_avatar_changed(self):
        pix = None
        if self.avatar_manager.has_avatar:
            pix = self.avatar_manager.get_pixmap(36)
        self.chat_widget.refresh_avatars(self.client.user_id, pix)

    # ---- Update ----

    def _on_updater_state_changed(self, old_state, new_state):
        if new_state == UpdateState.DOWNLOAD_AVAILABLE:
            info = self.updater.latest_info
            if info:
                InfoBar.info(
                    self.tr("Update Available"),
                    self.tr("NEVO v{} is available. Go to Settings → Software Update to download.").format(info.version),
                    parent=self,
                    position=InfoBarPosition.TOP,
                    duration=5000,
                )

    def _start_update_check(self):
        self.updater.start_periodic_check()

    def _on_theme_changed(self, is_dark: bool):
        self._refresh_theme_styles()

    def _refresh_theme_styles(self):
        ss = card_stylesheet()
        for card in self.findChildren(HeaderCardWidget):
            card.setStyleSheet(ss)
        self.channel_tree.refresh_theme()
        self.connection_bar.refresh_theme()
        self.chat_widget.refresh_theme()
        self.screen_share_view.refresh_theme()
        self.server_status.refresh_theme()
        self.waveform_panel.refresh_theme()

    def closeEvent(self, event):
        try:
            self.updater.cleanup()
        except Exception:
            pass
        try:
            self.settings_page.cleanup()
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
