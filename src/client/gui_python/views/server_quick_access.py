"""Server quick access panel with recent and pinned server lists.

Matches the PyQt-Fluent-Widgets navigation panel style.
"""

import json
import os
import sys
from datetime import datetime

from PyQt5.QtCore import Qt, pyqtSignal, QPoint
from PyQt5.QtGui import QPainter, QColor
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QFrame, QLabel,
    QWidget, QScrollArea,
)
from qfluentwidgets import FluentIcon, RoundMenu, Action, isDarkTheme


def _data_path():
    if getattr(sys, 'frozen', False):
        base = os.path.dirname(sys.executable)
    else:
        base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base, "server_quick_access.json")


def _load_data():
    path = _data_path()
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, dict):
                data.setdefault("recent", [])
                data.setdefault("pinned", [])
                return data
        except Exception:
            pass
    return {"recent": [], "pinned": []}


def _save_data(data):
    path = _data_path()
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
    except Exception:
        pass


def _server_key(entry):
    return f"{entry.get('host', '')}:{entry.get('port', 0)}"


class _NavStyleItem(QFrame):
    """A single server item styled like NavigationPushButton."""

    clicked = pyqtSignal(dict)
    right_clicked = pyqtSignal(dict, QPoint)

    def __init__(self, entry, is_pinned=False, parent=None):
        super().__init__(parent)
        self.entry = entry
        self._is_pinned = is_pinned
        self.setFixedHeight(36)
        self.setCursor(Qt.PointingHandCursor)
        self._is_enter = False
        self._is_pressed = False
        self._build()

    def _build(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(12, 0, 12, 0)
        layout.setSpacing(8)

        icon_label = QLabel()
        icon_label.setFixedSize(16, 16)
        if self._is_pinned:
            icon_label.setText("📌")
            icon_label.setStyleSheet("color: #f1c40f; font-size: 12px;")
        else:
            icon_label.setText("🖥")
            icon_label.setStyleSheet("color: #8e9297; font-size: 12px;")
        layout.addWidget(icon_label)

        host = self.entry.get("host", "127.0.0.1")
        name_label = QLabel(host)
        name_label.setStyleSheet("color: #ffffff; font-size: 13px;")
        layout.addWidget(name_label, 1)

        username = self.entry.get("username", "")
        if username:
            user_label = QLabel(username)
            user_label.setStyleSheet("color: #8e9297; font-size: 11px;")
            layout.addWidget(user_label)

    def enterEvent(self, event):
        self._is_enter = True
        self.update()
        super().enterEvent(event)

    def leaveEvent(self, event):
        self._is_enter = False
        self._is_pressed = False
        self.update()
        super().leaveEvent(event)

    def mousePressEvent(self, event):
        self._is_pressed = True
        self.update()
        super().mousePressEvent(event)

    def mouseReleaseEvent(self, event):
        self._is_pressed = False
        self.update()
        if event.button() == Qt.LeftButton:
            self.clicked.emit(self.entry)
        elif event.button() == Qt.RightButton:
            self.right_clicked.emit(self.entry, event.globalPos())
        super().mouseReleaseEvent(event)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHints(QPainter.Antialiasing)
        painter.setPen(Qt.NoPen)

        c = 255 if isDarkTheme() else 0

        if self._is_pressed:
            painter.setOpacity(0.7)

        if self._is_enter:
            painter.setBrush(QColor(c, c, c, 10))
            painter.drawRoundedRect(self.rect(), 5, 5)

        painter.end()
        super().paintEvent(event)


class ServerQuickAccessPanel(QWidget):
    """Panel styled like the navigation sidebar."""

    connect_requested = pyqtSignal(str, int, str)

    MAX_RECENT = 5

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data = _load_data()
        self._items = {}
        self._build()

    def _build(self):
        self.setFixedWidth(312)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 4, 0, 4)
        layout.setSpacing(0)

        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self._scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self._scroll.setStyleSheet(
            "QScrollArea { background: transparent; border: none; }"
            "QScrollBar:vertical { width: 4px; background: transparent; }"
            "QScrollBar::handle:vertical { background: rgba(255,255,255,0.15); border-radius: 2px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        )

        self._content = QWidget()
        self._content.setStyleSheet("background: transparent;")
        self._content_layout = QVBoxLayout(self._content)
        self._content_layout.setContentsMargins(8, 0, 8, 0)
        self._content_layout.setSpacing(2)
        self._content_layout.setAlignment(Qt.AlignTop)

        self._pinned_section = QVBoxLayout()
        self._pinned_section.setSpacing(2)
        self._pinned_title = QLabel(self.tr("Pinned"))
        self._pinned_title.setStyleSheet(
            "color: #8e9297; font-size: 10px; font-weight: 600; padding: 4px 4px 0px 4px;"
        )
        self._pinned_title.setVisible(False)
        self._pinned_section.addWidget(self._pinned_title)
        self._content_layout.addLayout(self._pinned_section)

        self._recent_section = QVBoxLayout()
        self._recent_section.setSpacing(2)
        self._recent_title = QLabel(self.tr("Recent"))
        self._recent_title.setStyleSheet(
            "color: #8e9297; font-size: 10px; font-weight: 600; padding: 4px 4px 0px 4px;"
        )
        self._recent_title.setVisible(False)
        self._recent_section.addWidget(self._recent_title)
        self._content_layout.addLayout(self._recent_section)

        self._content_layout.addStretch()
        self._scroll.setWidget(self._content)
        layout.addWidget(self._scroll)

        self._refresh()

    def _refresh(self):
        self._clear_layout(self._pinned_section)
        self._clear_layout(self._recent_section)
        self._items.clear()

        self._pinned_title = QLabel(self.tr("Pinned"))
        self._pinned_title.setStyleSheet(
            "color: #8e9297; font-size: 10px; font-weight: 600; padding: 4px 4px 0px 4px;"
        )
        self._pinned_section.addWidget(self._pinned_title)

        self._recent_title = QLabel(self.tr("Recent"))
        self._recent_title.setStyleSheet(
            "color: #8e9297; font-size: 10px; font-weight: 600; padding: 4px 4px 0px 4px;"
        )
        self._recent_section.addWidget(self._recent_title)

        has_pinned = len(self._data.get("pinned", [])) > 0
        has_recent = len(self._data.get("recent", [])) > 0

        self._pinned_title.setVisible(has_pinned)
        self._recent_title.setVisible(has_recent)

        empty = not has_pinned and not has_recent
        self.setVisible(not empty)

        if empty:
            return

        if has_pinned:
            for entry in self._data["pinned"]:
                self._add_item(entry, self._pinned_section, is_pinned=True)

        if has_recent:
            for entry in self._data["recent"]:
                key = _server_key(entry)
                if key in self._items:
                    continue
                self._add_item(entry, self._recent_section, is_pinned=False)

    def _clear_layout(self, layout):
        while layout.count():
            item = layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
            elif item.layout():
                self._clear_layout(item.layout())

    def _add_item(self, entry, layout, is_pinned=False):
        item = _NavStyleItem(entry, is_pinned=is_pinned)
        item.clicked.connect(self._on_item_clicked)
        item.right_clicked.connect(self._on_item_right_clicked)
        key = _server_key(entry)
        self._items[key] = item
        layout.addWidget(item)

    def _on_item_clicked(self, entry):
        self.connect_requested.emit(
            entry.get("host", "127.0.0.1"),
            entry.get("port", 24430),
            entry.get("username", "User"),
        )

    def _on_item_right_clicked(self, entry, global_pos):
        key = _server_key(entry)
        is_pinned = any(
            _server_key(p) == key for p in self._data.get("pinned", [])
        )

        menu = RoundMenu(parent=self)

        if is_pinned:
            action = Action(FluentIcon.CANCEL, self.tr("Remove from Pinned"))
            action.triggered.connect(lambda: self._unpin(entry))
        else:
            action = Action(FluentIcon.PIN, self.tr("Pin to Favorites"))
            action.triggered.connect(lambda: self._pin(entry))

        menu.addAction(action)
        menu.exec_(global_pos)

    def _pin(self, entry):
        key = _server_key(entry)
        self._data["pinned"] = [
            p for p in self._data["pinned"] if _server_key(p) != key
        ]
        self._data["pinned"].insert(0, {
            "host": entry.get("host", "127.0.0.1"),
            "port": entry.get("port", 24430),
            "username": entry.get("username", "User"),
        })
        _save_data(self._data)
        self._refresh()

    def _unpin(self, entry):
        key = _server_key(entry)
        self._data["pinned"] = [
            p for p in self._data["pinned"] if _server_key(p) != key
        ]
        _save_data(self._data)
        self._refresh()

    def add_recent(self, host, port, username):
        key = f"{host}:{port}"
        self._data["recent"] = [
            r for r in self._data["recent"] if _server_key(r) != key
        ]
        self._data["recent"].insert(0, {
            "host": host,
            "port": port,
            "username": username,
            "accessed_at": datetime.now().isoformat(),
        })
        if len(self._data["recent"]) > self.MAX_RECENT:
            self._data["recent"] = self._data["recent"][:self.MAX_RECENT]
        _save_data(self._data)
        self._refresh()