import hashlib
import os
from datetime import datetime

from PyQt5.QtCore import Qt, pyqtSignal, QSize, QEvent
from PyQt5.QtGui import QPixmap, QPainter, QPainterPath, QColor, QFont
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QFrame, QLabel,
    QWidget, QScrollArea, QSizePolicy, QMessageBox,
)
from qfluentwidgets import (
    FluentIcon, PushButton, RoundMenu, Action,
)
from theme_manager import ThemeManager, channel_container_stylesheet, inner_card_stylesheet

_DEBUG_LOG = os.path.join(os.path.dirname(__file__), "..", "debug.log")


def _log(msg: str):
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    line = f"[{ts}] {msg}"
    print(line)
    try:
        with open(_DEBUG_LOG, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass


def _log_popup(msg: str):
    _log(msg)
    try:
        QMessageBox.information(None, "DEBUG", msg)
    except Exception:
        pass


_USER_COLORS = [
    "#5865F2", "#EB459E", "#57F287", "#FEE75C",
    "#ED4245", "#9B59B6", "#3498DB", "#1ABC9C",
    "#E67E22", "#95A5A6",
]


def _user_color(name: str) -> str:
    h = int(hashlib.md5(name.encode()).hexdigest()[:8], 16)
    return _USER_COLORS[h % len(_USER_COLORS)]


def _make_default_avatar(size=28):
    pix = QPixmap(size, size)
    pix.fill(QColor("#3a3a50"))
    p = QPainter(pix)
    p.setRenderHint(QPainter.Antialiasing)
    path = QPainterPath()
    path.addEllipse(0, 0, size, size)
    p.setClipPath(path)
    p.setBrush(QColor("#808080"))
    p.setPen(Qt.NoPen)
    p.drawEllipse(2, 2, size - 4, size - 4)
    p.setBrush(Qt.white)
    head_r = size // 5
    p.drawEllipse(
        size // 2 - head_r, size // 3 - head_r,
        head_r * 2, head_r * 2
    )
    body = QPainterPath()
    body.moveTo(size // 2 - size // 3, size - 4)
    body.quadTo(
        size // 2, size // 2.2,
        size // 2 + size // 3, size - 4
    )
    p.drawPath(body)
    p.end()
    return pix


class _UserItem(QFrame):
    user_context_menu = pyqtSignal(int, str)
    volume_requested = pyqtSignal(int, str)
    local_mute_requested = pyqtSignal(int, bool)

    def __init__(self, user: dict, local_user_id: int = 0, is_admin: bool = False,
                 avatar_pixmap=None, parent=None):
        super().__init__(parent)
        self._user = user
        self._local_user_id = local_user_id
        self._is_admin = is_admin
        self._speaking = False
        self._local_muted = False
        uid = user.get("id", 0)
        uname = user.get("username", "")
        self._username = uname if uname else f"User {uid}"
        self.setFixedHeight(40)
        self.setMinimumWidth(200)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.setCursor(Qt.PointingHandCursor)
        self.setStyleSheet("QFrame { border: none; border-radius: 6px; }"
                          "QFrame:hover { background-color: rgba(79, 84, 92, 0.4); }")
        self.setContextMenuPolicy(Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(self._show_context_menu)
        if avatar_pixmap and not avatar_pixmap.isNull():
            self._avatar_pixmap = avatar_pixmap.scaled(28, 28,
                Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
        else:
            self._avatar_pixmap = _make_default_avatar(28)

    @property
    def user_id(self) -> int:
        return self._user.get("id", 0)

    def set_speaking(self, speaking: bool):
        self._speaking = speaking
        self.update()

    def set_local_muted(self, muted: bool):
        self._local_muted = muted
        self.update()

    def is_local_muted(self):
        return self._local_muted

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w = self.width()
        h = self.height()

        ax = 12
        ay = (h - 28) // 2

        path = QPainterPath()
        path.addEllipse(ax, ay, 28, 28)
        p.setClipPath(path)
        p.drawPixmap(ax, ay, self._avatar_pixmap)
        p.setClipping(False)

        if self._local_muted:
            mute_x = ax
            mute_y = ay
            mute_w = 28
            mute_h = 28
            p.setBrush(QColor(237, 66, 69, 150))
            p.setPen(Qt.NoPen)
            p.drawEllipse(mute_x, mute_y, mute_w, mute_h)
            p.setPen(QColor(255, 255, 255))
            font = QFont("Segoe UI", 10, QFont.Bold)
            p.setFont(font)
            p.drawText(mute_x, mute_y, mute_w, mute_h, Qt.AlignCenter, "\U0001f507")

        dot_x = ax + 28 + 1
        dot_y = ay + 28 - 5
        dot_r = 4
        if self._speaking:
            p.setBrush(QColor(255, 193, 7))
        else:
            p.setBrush(QColor(60, 60, 60))
        p.setPen(Qt.NoPen)
        p.drawEllipse(dot_x - dot_r, dot_y - dot_r, dot_r * 2, dot_r * 2)

        nx = ax + 28 + 8
        ny_top = ay + 2

        p.setFont(QFont("Segoe UI", 9))
        p.setPen(QColor("#FFFFFF"))
        p.drawText(nx, ny_top + 12, self._username)

        if self._local_muted:
            mute_text_x = nx + p.fontMetrics().horizontalAdvance(self._username) + 6
            p.setPen(QColor(237, 66, 69))
            p.drawText(mute_text_x, ny_top + 12, "\U0001f507")
        elif self._user.get("group_id") == 1:
            p.drawText(nx + p.fontMetrics().horizontalAdvance(self._username) + 6,
                       ny_top + 12, "\U0001f451")

    def _show_context_menu(self, pos):
        if self._user.get("id", 0) == self._local_user_id:
            return
        menu = RoundMenu(parent=self)

        adjust_vol_action = Action(FluentIcon.VOLUME, self.tr("Adjust Volume"))
        adjust_vol_action.triggered.connect(
            lambda: self.volume_requested.emit(self._user.get("id", 0), self._username)
        )
        menu.addAction(adjust_vol_action)

        if self._local_muted:
            mute_action = Action(FluentIcon.MUTE, self.tr("Unmute User"))
            mute_action.triggered.connect(
                lambda: self.local_mute_requested.emit(self._user.get("id", 0), False)
            )
        else:
            mute_action = Action(FluentIcon.MUTE, self.tr("Local Mute"))
            mute_action.triggered.connect(
                lambda: self.local_mute_requested.emit(self._user.get("id", 0), True)
            )
        menu.addAction(mute_action)

        menu.exec_(self.mapToGlobal(pos))


class _ChannelUsersPanel(QFrame):
    volume_requested = pyqtSignal(int, str)
    local_mute_requested = pyqtSignal(int, bool)

    def __init__(self, users: list[dict] = None, local_user_id: int = 0,
                 is_admin: bool = False, local_avatar=None, parent=None):
        super().__init__(parent)
        self._local_user_id = local_user_id
        self._is_admin = is_admin
        self._local_avatar = local_avatar
        self._user_items: dict[int, _UserItem] = {}
        self._setup_ui(users or [])

    def _setup_ui(self, users: list[dict]):
        self.setMinimumWidth(180)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 4)
        layout.setSpacing(2)
        for u in users:
            av = None
            if self._local_avatar and u.get("id") == self._local_user_id:
                av = self._local_avatar
            item = _UserItem(u, self._local_user_id, self._is_admin,
                             avatar_pixmap=av, parent=self)
            item.volume_requested.connect(self.volume_requested.emit)
            item.local_mute_requested.connect(self.local_mute_requested.emit)
            self._user_items[u.get("id", 0)] = item
            layout.addWidget(item)
        if not users:
            empty = QLabel("  ")
            layout.addWidget(empty)

    def set_speaking(self, user_id: int, speaking: bool):
        item = self._user_items.get(user_id)
        if item:
            item.set_speaking(speaking)

    def set_local_muted(self, user_id: int, muted: bool):
        item = self._user_items.get(user_id)
        if item:
            item.set_local_muted(muted)


class _ChannelCard(QFrame):
    clicked = pyqtSignal(int)
    rename_requested = pyqtSignal(int, str)
    add_subchannel_requested = pyqtSignal(int)
    delete_requested = pyqtSignal(int)
    volume_requested = pyqtSignal(int, str)
    local_mute_requested = pyqtSignal(int, bool)

    def __init__(self, channel: dict, is_current: bool = False, show_users: bool = False,
                 users: list[dict] = None, local_avatar=None, local_user_id: int = 0,
                 is_admin: bool = False, parent=None, is_sub=False):
        super().__init__(parent)
        self._channel = channel
        self._is_current = is_current
        self._show_users = show_users
        self._users_panel = None
        self._is_sub = is_sub
        self._setup_ui(channel, is_current, show_users, users or [],
                       local_avatar, local_user_id, is_admin, is_sub)

    def _setup_ui(self, ch: dict, is_current: bool, show_users: bool,
                  users: list[dict], local_avatar=None, local_user_id: int = 0,
                  is_admin: bool = False, is_sub=False):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        card = QFrame()
        if is_current:
            card.setStyleSheet(inner_card_stylesheet())
        else:
            card.setStyleSheet(
                "QFrame { background-color: #2b2d35; border-radius: 8px; }"
                "QFrame:hover { background-color: #33354a; }"
            )
        card.setFixedHeight(42)
        card.setCursor(Qt.PointingHandCursor)

        row = QHBoxLayout(card)
        if is_sub:
            row.setContentsMargins(28, 0, 8, 0)
        else:
            row.setContentsMargins(10, 0, 8, 0)
        row.setSpacing(8)

        icon_lbl = QLabel()
        icon_lbl.setFixedSize(20, 20)
        icon_lbl.setAlignment(Qt.AlignCenter)
        if is_sub:
            icon_lbl.setText("#")
            icon_lbl.setStyleSheet(
                "background-color: transparent;"
                "color: #6d6f78; font-size: 14px; font-weight: bold;"
            )
        else:
            icon_lbl.setText("\u2588")
            icon_lbl.setStyleSheet(
                "background-color: #5865f2; border-radius: 4px;"
                "color: white; font-size: 11px; font-weight: bold;"
            )
        row.addWidget(icon_lbl)

        name_lbl = QLabel(ch.get("name", ""))
        name_lbl.setFont(QFont("Segoe UI", 9))
        name_lbl.setStyleSheet("color: #dbdee1; background: transparent;")
        name_lbl.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        row.addWidget(name_lbl, 1)

        counter = QLabel()
        counter.setFixedWidth(40)
        counter.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        counter.setFont(QFont("Segoe UI", 8))
        counter.setStyleSheet("color: #6d6f78; background: transparent;")
        if is_sub:
            u_count = len(ch.get("users", []))
            max_u = 50
            counter.setText(f"{u_count:02d}/{max_u:02d}")
        row.addWidget(counter)

        outer.addWidget(card)

        if show_users and users:
            self._users_panel = _ChannelUsersPanel(
                users, local_user_id=local_user_id,
                is_admin=is_admin,
                local_avatar=local_avatar,
                parent=self)
            self._users_panel.volume_requested.connect(self.volume_requested.emit)
            self._users_panel.local_mute_requested.connect(self.local_mute_requested.emit)
            outer.addWidget(self._users_panel)

        self._inner_card = card
        card.installEventFilter(self)
        icon_lbl.installEventFilter(self)
        name_lbl.installEventFilter(self)
        row.installEventFilter(self)
        outer.installEventFilter(self)

    def eventFilter(self, obj, event):
        if event.type() == QEvent.MouseButtonDblClick:
            ch = self._channel
            if event.button() == Qt.LeftButton:
                _log(f"ChannelCard double-click: id={ch.get('id',0)}, name={ch.get('name','')}")
                self.clicked.emit(ch.get("id", 0))
                return True
        elif event.type() == QEvent.MouseButtonPress:
            ch = self._channel
            if event.button() == Qt.RightButton:
                _log(f"ChannelCard right-click: id={ch.get('id',0)}, name={ch.get('name','')}")
                self._show_context_menu(event.globalPos(), ch)
                return True
        return super().eventFilter(obj, event)

    def _show_context_menu(self, pos, ch):
        _log(f"Context menu: id={ch.get('id',0)}, name={ch.get('name','')}")
        menu = RoundMenu(parent=self)
        ch_id = ch.get("id", 0)
        ch_name = ch.get("name", "")

        rename_action = Action(FluentIcon.EDIT, self.tr("Rename"))
        rename_action.triggered.connect(
            lambda checked=False, cid=ch_id, cname=ch_name: (
                _log(f"RENAME: Action triggered, cid={cid} name='{cname}'"),
                _log(f"RENAME: before rename_requested.emit"),
                self.rename_requested.emit(cid, cname),
                _log(f"RENAME: after rename_requested.emit"),
            )[1]
        )
        menu.addAction(rename_action)

        if not self._is_sub:
            add_action = Action(FluentIcon.ADD, self.tr("Add Sub-channel"))
            add_action.triggered.connect(
                lambda checked=False, pid=ch_id: (
                    _log(f"ADD_SUB: emitting signal parent_id={pid}"),
                    self.add_subchannel_requested.emit(pid),
                )[1]
            )
            menu.addAction(add_action)

        delete_action = Action(FluentIcon.DELETE, self.tr("Delete"))
        delete_action.triggered.connect(
            lambda checked=False, cid=ch_id: self.delete_requested.emit(cid)
        )
        menu.addAction(delete_action)

        menu.exec_(pos)

    def set_current(self, current: bool):
        self._is_current = current
        if current:
            self._inner_card.setStyleSheet(inner_card_stylesheet())
        else:
            self._inner_card.setStyleSheet(
                "QFrame { background-color: #2b2d35; border-radius: 8px; }"
                "QFrame:hover { background-color: #33354a; }"
            )

    def set_speaking(self, user_id: int, speaking: bool):
        if self._users_panel:
            self._users_panel.set_speaking(user_id, speaking)

    def set_local_muted(self, user_id: int, muted: bool):
        if self._users_panel:
            self._users_panel.set_local_muted(user_id, muted)


class ChannelTreeView(QFrame):
    join_channel_requested = pyqtSignal(int)
    leave_channel_requested = pyqtSignal(int)
    rename_channel_requested = pyqtSignal(int, str)
    add_subchannel_requested = pyqtSignal(int)
    delete_channel_requested = pyqtSignal(int)
    volume_requested = pyqtSignal(int, str)
    local_mute_requested = pyqtSignal(int, bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._current_channel_id = 0
        self._local_user_id = 0
        self._is_admin = False
        self._channels: list[dict] = []
        self._channel_cards: list[_ChannelCard] = []
        self._scroll: QScrollArea = None
        self._container: QWidget = None
        self._layout: QVBoxLayout = None
        self._setup_ui()

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)

        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self._scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self._scroll.setStyleSheet(
            "QScrollArea { border: none; background-color: #2b2d31; }"
            "QScrollBar:vertical {"
            "  width: 6px; background: transparent; border-radius: 3px;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: #4f545c; border-radius: 3px; min-height: 16px;"
            "}"
            "QScrollBar::handle:vertical:hover { background: #686d75; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        )

        self._container = QWidget()
        self._container.setStyleSheet(channel_container_stylesheet())
        self._layout = QVBoxLayout(self._container)
        self._layout.setContentsMargins(6, 8, 6, 8)
        self._layout.setSpacing(4)
        self._layout.addStretch()

        self._scroll.setWidget(self._container)
        main_layout.addWidget(self._scroll, 1)

    def set_user_info(self, user_id: int, is_admin: bool):
        self._local_user_id = user_id
        self._is_admin = is_admin

    def update_channels(self, channels: list, current_channel_id: int = 0,
                        local_avatar=None):
        self._channels = channels
        self._current_channel_id = current_channel_id
        self._channel_cards.clear()

        while self._layout.count() > 1:
            item = self._layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

        if not channels:
            _log("ChannelTree: update_channels called with empty channels")
            return

        _log(f"ChannelTree: rebuilding {len(channels)} channels, current={current_channel_id}")

        children_map: dict[int, list] = {}
        roots = []
        for ch in channels:
            pid = ch.get("parent_id", 0)
            if pid == 0:
                roots.append(ch)
            else:
                children_map.setdefault(pid, []).append(ch)

        for ch in roots:
            is_current = ch.get("id") == current_channel_id
            show_users = is_current and ch.get("users")
            _log(f"ChannelTree: root ch={ch.get('name','')} id={ch.get('id',0)} is_current={is_current} show_users={show_users} users={len(ch.get('users',[]))}")
            card = _ChannelCard(ch, is_current=is_current, show_users=show_users,
                                users=ch.get("users", []), local_avatar=local_avatar,
                                local_user_id=self._local_user_id,
                                is_admin=self._is_admin,
                                parent=self._container)
            card.clicked.connect(self._on_channel_clicked)
            card.rename_requested.connect(self._forward_rename)
            card.add_subchannel_requested.connect(self._forward_add_sub)
            card.delete_requested.connect(self._forward_delete)
            card.volume_requested.connect(self.volume_requested.emit)
            card.local_mute_requested.connect(self.local_mute_requested.emit)
            self._layout.insertWidget(self._layout.count() - 1, card)
            self._channel_cards.append(card)

            for sub_ch in children_map.get(ch.get("id", 0), []):
                sub_is_current = sub_ch.get("id") == current_channel_id
                sub_show = sub_is_current and sub_ch.get("users")
                _log(f"ChannelTree: sub ch={sub_ch.get('name','')} id={sub_ch.get('id',0)} is_current={sub_is_current} show_users={sub_show} users={len(sub_ch.get('users',[]))}")
                sub_card = _ChannelCard(sub_ch, is_current=sub_is_current,
                                        show_users=sub_show,
                                        users=sub_ch.get("users", []),
                                        local_avatar=local_avatar,
                                        local_user_id=self._local_user_id,
                                        is_admin=self._is_admin,
                                        parent=self._container,
                                        is_sub=True)
                sub_card.clicked.connect(self._on_channel_clicked)
                sub_card.rename_requested.connect(self.rename_channel_requested.emit)
                sub_card.add_subchannel_requested.connect(self.add_subchannel_requested.emit)
                sub_card.delete_requested.connect(self.delete_channel_requested.emit)
                sub_card.volume_requested.connect(self.volume_requested.emit)
                sub_card.local_mute_requested.connect(self.local_mute_requested.emit)
                self._layout.insertWidget(self._layout.count() - 1, sub_card)
                self._channel_cards.append(sub_card)

    def set_current_channel(self, channel_id: int):
        self._current_channel_id = channel_id
        for card in self._channel_cards:
            ch = card._channel
            is_current = ch.get("id") == channel_id
            card.set_current(is_current)

    def _on_channel_clicked(self, channel_id: int):
        _log(f"ChannelTreeView._on_channel_clicked: id={channel_id}")
        self.join_channel_requested.emit(channel_id)

    def _forward_rename(self, channel_id: int, name: str):
        _log(f"ChannelTreeView._forward_rename: id={channel_id} name={name}")
        self.rename_channel_requested.emit(channel_id, name)

    def _forward_add_sub(self, parent_id: int):
        _log(f"ChannelTreeView._forward_add_sub: parent_id={parent_id}")
        self.add_subchannel_requested.emit(parent_id)

    def _forward_delete(self, channel_id: int):
        _log(f"ChannelTreeView._forward_delete: id={channel_id}")
        self.delete_channel_requested.emit(channel_id)

    def set_speaking(self, user_id: int, speaking: bool):
        for card in self._channel_cards:
            card.set_speaking(user_id, speaking)

    def set_local_muted(self, user_id: int, muted: bool):
        for card in self._channel_cards:
            card.set_local_muted(user_id, muted)

    def refresh_theme(self):
        self._container.setStyleSheet(channel_container_stylesheet())
        for card in self._channel_cards:
            card._inner_card.setStyleSheet(inner_card_stylesheet())
