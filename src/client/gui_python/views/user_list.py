"""User list view with voice activity indicator for the NEVO client."""

from PyQt5.QtCore import Qt, QTimer, pyqtSignal, QPropertyAnimation, QEasingCurve, QSize
from PyQt5.QtGui import QPixmap, QPainter, QPainterPath, QColor, QFont
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QFrame, QLabel, QWidget,
    QScrollArea, QSizePolicy,
)

_COLOR_SPEAKING = QColor(255, 193, 7)
_COLOR_OFF = QColor(60, 60, 60)
_AVATAR_SIZE = 36
_LIGHT_BAR_WIDTH = 4
_ROW_HEIGHT = 52
_SPEAK_TIMEOUT_MS = 600


class _VoiceLightBar(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedWidth(_LIGHT_BAR_WIDTH)
        self.setFixedHeight(_AVATAR_SIZE - 6)
        self._color = QColor(_COLOR_OFF)
        self._anim = None
        self.setAttribute(Qt.WA_OpaquePaintEvent)

    def set_speaking(self, on=True):
        target = QColor(_COLOR_SPEAKING) if on else QColor(_COLOR_OFF)
        if self._anim and self._anim.state() != QPropertyAnimation.Stopped:
            self._anim.stop()
        self._anim = QPropertyAnimation(self, b"_prop_color")
        self._anim.setDuration(120)
        self._anim.setStartValue(QColor(self._color))
        self._anim.setEndValue(target)
        self._anim.setEasingCurve(QEasingCurve.OutCubic)
        self._anim.start()

    def stop_anim(self):
        if self._anim and self._anim.state() != QPropertyAnimation.Stopped:
            self._anim.stop()

    def _get_color(self):
        return self._color

    def _set_color(self, c):
        self._color = QColor(c)
        self.update()

    _prop_color = property(_get_color, _set_color)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        r = self.rect().adjusted(1, 3, -1, -3)
        p.setBrush(self._color)
        p.setPen(Qt.NoPen)
        p.drawRoundedRect(r, 2, 2)
        p.end()


class _UserRow(QWidget):
    def __init__(self, user_id, username, is_admin=False,
                 muted=False, deafened=False, parent=None):
        super().__init__(parent)
        self._user_id = user_id
        self._username = username
        self._is_admin = is_admin
        self._muted = muted
        self._deafened = deafened
        self._speak_timer = QTimer(self)
        self._speak_timer.setSingleShot(True)
        self._speak_timer.timeout.connect(self._on_speak_timeout)
        self.setFixedHeight(_ROW_HEIGHT)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self._avatar_pixmap = self._default_avatar()
        self._build_layout()

    def _build_layout(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(8, 6, 12, 6)
        layout.setSpacing(8)

        self._avatar_label = QLabel()
        self._avatar_label.setFixedSize(_AVATAR_SIZE, _AVATAR_SIZE)
        self._avatar_label.setPixmap(self._avatar_pixmap)
        self._avatar_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self._avatar_label)

        self._light_bar = _VoiceLightBar()
        layout.addWidget(self._light_bar)

        self._name_label = QLabel(self._username)
        self._name_label.setFont(QFont("Segoe UI", 9))
        self._name_label.setStyleSheet("color: white; background: transparent;")
        self._name_label.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        layout.addWidget(self._name_label, 1)

        status_parts = []
        if self._deafened:
            status_parts.append("\U0001f507")
        elif self._muted:
            status_parts.append("\U0001f3a4")
        if self._is_admin:
            status_parts.append("\U0001f451")
        status_text = " ".join(status_parts)
        self._status_label = QLabel(status_text)
        self._status_label.setFont(QFont("Segoe UI", 8))
        self._status_label.setStyleSheet("color: #a0a0a0; background: transparent;")
        layout.addWidget(self._status_label)

    @staticmethod
    def _default_avatar():
        pix = QPixmap(_AVATAR_SIZE, _AVATAR_SIZE)
        pix.fill(QColor("#5B5B5B"))
        p = QPainter(pix)
        p.setRenderHint(QPainter.Antialiasing)
        path = QPainterPath()
        path.addEllipse(0, 0, _AVATAR_SIZE, _AVATAR_SIZE)
        p.setClipPath(path)
        p.setBrush(QColor("#808080"))
        p.setPen(Qt.NoPen)
        p.drawEllipse(2, 2, _AVATAR_SIZE - 4, _AVATAR_SIZE - 4)
        p.setBrush(Qt.white)
        head_r = _AVATAR_SIZE // 5
        p.drawEllipse(
            _AVATAR_SIZE // 2 - head_r, _AVATAR_SIZE // 3 - head_r,
            head_r * 2, head_r * 2
        )
        body = QPainterPath()
        body.moveTo(_AVATAR_SIZE // 2 - _AVATAR_SIZE // 3, _AVATAR_SIZE - 4)
        body.quadTo(
            _AVATAR_SIZE // 2, _AVATAR_SIZE // 2.2,
            _AVATAR_SIZE // 2 + _AVATAR_SIZE // 3, _AVATAR_SIZE - 4
        )
        p.drawPath(body)
        p.end()
        return pix

    @property
    def user_id(self) -> int:
        return self._user_id

    def trigger_speaking(self):
        self._light_bar.set_speaking(True)
        self._speak_timer.start(_SPEAK_TIMEOUT_MS)

    def _on_speak_timeout(self):
        self._light_bar.set_speaking(False)

    def cleanup(self):
        self._speak_timer.stop()
        self._light_bar.stop_anim()


class UserListView(QFrame):
    user_context_menu = pyqtSignal(int, str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._users: list[dict] = []
        self._local_user_id = 0
        self._is_admin = False
        self._row_map: dict[int, _UserRow] = {}
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
        self._container.setStyleSheet("background-color: #2b2d31;")
        self._content_layout = QVBoxLayout(self._container)
        self._content_layout.setContentsMargins(6, 8, 6, 8)
        self._content_layout.setSpacing(2)

        self.channel_label = QLabel(self.tr("Not in a channel"))
        self.channel_label.setFont(QFont("Segoe UI", 10, QFont.Bold))
        self.channel_label.setStyleSheet("color: white; background: transparent; padding: 0 0 4px 0;")
        self._content_layout.addWidget(self.channel_label)

        self._content_layout.addStretch()

        self._scroll.setWidget(self._container)
        main_layout.addWidget(self._scroll, 1)

    def update_users(self, users: list, local_user_id: int = 0, is_admin: bool = False,
                     channel_name: str = ""):
        self._users = users or []
        self._local_user_id = local_user_id
        self._is_admin = is_admin

        if channel_name:
            self.channel_label.setText(self.tr("Channel: {}").format(channel_name))
        else:
            self.channel_label.setText(self.tr("Not in a channel"))

        self._rebuild_rows()

    def set_speaking(self, user_id: int, speaking: bool = True):
        row = self._row_map.get(user_id)
        if row and speaking:
            row.trigger_speaking()

    def _rebuild_rows(self):
        for row in self._row_map.values():
            row.cleanup()
        self._row_map.clear()

        while self._content_layout.count() > 2:
            item = self._content_layout.takeAt(1)
            w = item.widget()
            if w:
                w.deleteLater()

        for user in self._users:
            uid = user.get("id", 0)
            uname = user.get("username", "")
            row = _UserRow(
                user_id=uid,
                username=uname,
                is_admin=user.get("group_id") == 1,
                muted=bool(user.get("muted")),
                deafened=bool(user.get("deafened")),
                parent=self._container,
            )
            self._row_map[uid] = row
            insert_idx = self._content_layout.count() - 1
            self._content_layout.insertWidget(insert_idx, row)

    def _show_context_menu(self, pos):
        pass
