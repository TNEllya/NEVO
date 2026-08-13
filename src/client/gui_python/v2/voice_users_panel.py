"""NEVO v2 voice users panel — right 280px column (design col 3 right).

Shows connected voice users with speaking indicators + voice activity bars,
plus a connection-info footer.
"""

import os
import sys

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtGui import QPixmap
from PyQt5.QtWidgets import QFrame, QVBoxLayout, QHBoxLayout, QLabel, QScrollArea, QWidget

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from v2.theme import palette, render_icon, Avatar, IconButton, VoiceActivityBars, W_VOICE_PANEL


class _VoiceUserCard(QFrame):
    """A user row in the voice users panel: avatar + name + voice bars + mute btn."""

    video_call_requested = pyqtSignal(int, str)
    volume_requested = pyqtSignal(int, str)
    local_mute_requested = pyqtSignal(int, bool)

    def __init__(self, user: dict, local_user_id: int = 0,
                 local_avatar: QPixmap = None, parent=None):
        super().__init__(parent)
        self._user = user
        self._local_user_id = local_user_id
        self._muted = user.get("muted", False)
        self._local_muted = False
        self._speaking = False
        self.setMouseTracking(True)
        self.setCursor(Qt.PointingHandCursor)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(12, 10, 12, 10)
        lay.setSpacing(12)

        # Avatar with speaking ring container
        av_wrap = QFrame()
        av_wrap.setFixedSize(42, 42)
        av_lay = QVBoxLayout(av_wrap)
        av_lay.setContentsMargins(3, 3, 3, 3)
        av_lay.setSpacing(0)
        self._avatar = Avatar(36)
        name = user.get("username", "")
        if user.get("id") == local_user_id and local_avatar is not None:
            self._avatar.set_user(name, local_avatar)
        else:
            self._avatar.set_user(name)
        av_lay.addWidget(self._avatar)
        self._ring = QFrame(av_wrap)
        self._ring.setStyleSheet("border: 2px solid transparent; border-radius: 21px;")
        self._ring.setGeometry(0, 0, 42, 42)
        lay.addWidget(av_wrap)

        # Name + voice bars
        info = QVBoxLayout()
        info.setContentsMargins(0, 0, 0, 0)
        info.setSpacing(4)
        head = QHBoxLayout()
        head.setSpacing(6)
        self._name = QLabel(name)
        self._refresh_name_style()
        head.addWidget(self._name)
        if not self._muted:
            dot = QLabel()
            dot.setFixedSize(6, 6)
            dot.setStyleSheet(f"background: {palette().get('primary','#2DD4A8')}; border-radius: 3px;")
            head.addWidget(dot)
        head.addStretch()
        info.addLayout(head)

        self._bars = VoiceActivityBars(5)
        self._bars.set_muted(self._muted)
        info.addWidget(self._bars)
        lay.addLayout(info, 1)

        # Mute button
        self._btn_mute = IconButton("mic", 16)
        self._btn_mute.setCheckable(False)
        self._btn_mute.clicked.connect(self._on_mute_click)
        lay.addWidget(self._btn_mute)
        self._refresh_mute_icon()

    def _refresh_name_style(self):
        col = palette().get("text_muted", "#6B7280") if (self._muted or self._local_muted) else palette().get("text_primary", "#E8EAF0")
        self._name.setStyleSheet(f"color: {col}; font-size: 14px; font-weight: 500;")

    def _refresh_mute_icon(self):
        uid = self._user.get("id", 0)
        if uid == self._local_user_id:
            self._btn_mute.setVisible(False)
            return
        if self._local_muted:
            self._btn_mute.set_icon("mic-off")
            self._btn_mute.set_color(palette().get("error", "#F87171"))
        else:
            self._btn_mute.set_icon("mic")
            self._btn_mute.set_color(None)

    def _on_mute_click(self):
        uid = self._user.get("id", 0)
        self.local_mute_requested.emit(uid, not self._local_muted)

    def set_speaking(self, speaking: bool):
        uid = self._user.get("id", 0)
        if uid == self._local_user_id:
            return
        self._speaking = speaking
        self._bars.set_speaking(speaking and not self._muted)
        if speaking and not self._muted:
            self._ring.setStyleSheet(f"border: 2px solid {palette().get('primary','#2DD4A8')}; border-radius: 21px; opacity: 0.6;")
            self.setStyleSheet(f"background-color: {palette().get('primary_muted','rgba(45,212,168,0.12)')}; border-radius: 8px;")
        else:
            self._ring.setStyleSheet("border: 2px solid transparent; border-radius: 21px;")
            self.setStyleSheet("")

    def set_local_muted(self, muted: bool):
        self._local_muted = muted
        self._refresh_name_style()
        self._refresh_mute_icon()

    def mousePressEvent(self, e):
        if e.button() == Qt.RightButton:
            self._show_menu(e.globalPos())
        super().mousePressEvent(e)

    def _show_menu(self, pos):
        uid = self._user.get("id", 0)
        uname = self._user.get("username", "")
        if uid == self._local_user_id:
            return
        from qfluentwidgets import RoundMenu, Action
        menu = RoundMenu(parent=self)
        menu.addAction(Action("视频通话", triggered=lambda: self.video_call_requested.emit(uid, uname)))
        menu.addAction(Action("调节音量", triggered=lambda: self.volume_requested.emit(uid, uname)))
        menu.addAction(Action("本地静音" if not self._local_muted else "取消本地静音",
                              triggered=lambda: self.local_mute_requested.emit(uid, not self._local_muted)))
        menu.exec_(pos)

    def refresh_theme(self):
        self._refresh_name_style()
        self._bars.refresh_theme()
        self._refresh_mute_icon()
        self.set_speaking(self._speaking)


class VoiceUsersPanel(QFrame):
    """Right-side 280px panel showing voice users + connection footer."""

    video_call_requested = pyqtSignal(int, str)
    user_volume_requested = pyqtSignal(int, str)
    local_mute_requested = pyqtSignal(int, bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("VoicePanel")
        self.setFixedWidth(W_VOICE_PANEL)
        self._users = []
        self._local_user_id = 0
        self._local_avatar = None
        self._cards = {}   # uid -> _VoiceUserCard
        self._setup_ui()

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

        # Header
        header = QFrame()
        header.setFixedHeight(48)
        hlay = QHBoxLayout(header)
        hlay.setContentsMargins(16, 0, 16, 0)
        title = QLabel("语音连接")
        title.setStyleSheet(f"color: {palette().get('text_secondary','')}; font-size: 13px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.03em;")
        hlay.addWidget(title)
        hlay.addStretch()
        self._count_lbl = QLabel("0 人在线")
        self._count_lbl.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")
        hlay.addWidget(self._count_lbl)
        header.setStyleSheet(f"border-bottom: 1px solid {palette().get('bg_hover','')};")
        lay.addWidget(header)

        # Users scroll
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        host = QWidget()
        self._users_lay = QVBoxLayout(host)
        self._users_lay.setContentsMargins(8, 8, 8, 8)
        self._users_lay.setSpacing(2)
        self._users_lay.setAlignment(Qt.AlignTop)
        scroll.setWidget(host)
        lay.addWidget(scroll, 1)

        # Footer: connection info
        footer = QFrame()
        footer.setStyleSheet(f"background-color: {palette().get('bg_primary','')}; border-top: 1px solid {palette().get('bg_hover','')};")
        flay = QVBoxLayout(footer)
        flay.setContentsMargins(16, 12, 16, 12)
        flay.setSpacing(4)
        row1 = QHBoxLayout()
        self._dot = QLabel()
        self._dot.setFixedSize(8, 8)
        self._dot.setStyleSheet(f"background: {palette().get('text_muted','')}; border-radius: 4px;")
        row1.addWidget(self._dot)
        self._conn_lbl = QLabel("未连接")
        self._conn_lbl.setStyleSheet(f"color: {palette().get('text_secondary','')}; font-size: 11px;")
        row1.addWidget(self._conn_lbl)
        row1.addStretch()
        self._latency_lbl = QLabel("延迟: --")
        self._latency_lbl.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")
        row1.addWidget(self._latency_lbl)
        flay.addLayout(row1)
        row2 = QHBoxLayout()
        enc_icon = QLabel()
        enc_icon.setPixmap(render_icon("shield", 12, palette().get("text_muted", "#6B7280")))
        row2.addWidget(enc_icon)
        self._enc_lbl = QLabel("加密: XChaCha20")
        self._enc_lbl.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")
        row2.addWidget(self._enc_lbl)
        row2.addStretch()
        flay.addLayout(row2)
        lay.addWidget(footer)

    # ---- Public API ----
    def set_channel_users(self, users: list, local_user_id: int, channel_name: str = ""):
        self._users = users or []
        self._local_user_id = local_user_id
        # Clear
        while self._users_lay.count():
            it = self._users_lay.takeAt(0)
            w = it.widget()
            if w:
                w.deleteLater()
        self._cards = {}
        for u in self._users:
            card = _VoiceUserCard(u, local_user_id, self._local_avatar)
            card.video_call_requested.connect(self.video_call_requested.emit)
            card.volume_requested.connect(self.user_volume_requested.emit)
            card.local_mute_requested.connect(self.local_mute_requested.emit)
            self._users_lay.addWidget(card)
            self._cards[u.get("id", 0)] = card
        self._users_lay.addStretch()
        self._count_lbl.setText(f"{len(self._users)} 人在线")

    def set_local_avatar(self, pixmap: QPixmap):
        self._local_avatar = pixmap

    def set_speaking(self, user_id: int, speaking: bool):
        card = self._cards.get(user_id)
        if card:
            card.set_speaking(speaking)

    def set_local_muted(self, user_id: int, muted: bool):
        card = self._cards.get(user_id)
        if card:
            card.set_local_muted(muted)

    def set_connected(self, connected: bool):
        col = palette().get("primary", "#2DD4A8") if connected else palette().get("text_muted", "#6B7280")
        glow = f"box-shadow: 0 0 6px {col};" if connected else ""
        self._dot.setStyleSheet(f"background: {col}; border-radius: 4px; {glow}")
        self._conn_lbl.setText("已连接" if connected else "未连接")

    def set_latency(self, ms: int):
        self._latency_lbl.setText(f"延迟: {ms}ms" if ms >= 0 else "延迟: --")

    def refresh_theme(self):
        self.set_connected(bool(self._cards))
        for card in self._cards.values():
            card.refresh_theme()
