# -*- coding: utf-8 -*-
"""
EmojiPanel — 分类 Emoji 选择器面板

弹出式面板，提供 6 个分类的常用 emoji，点击后发射 emoji_selected 信号。
"""

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
    QScrollArea, QGridLayout, QSizePolicy,
)


# ============================================================
# Emoji 数据源 — 6 个分类，每类约 15 个
# ============================================================
EMOJI_CATEGORIES = {
    "😀 常用": [
        "😀", "😃", "😄", "😁", "😅", "😂", "🤣", "😊",
        "😇", "🙂", "😉", "😌", "😍", "🥰", "😘",
    ],
    "😂 搞笑": [
        "😋", "😛", "😜", "🤪", "😝", "🤑", "🤗", "🤭",
        "🤫", "🤔", "😏", "😒", "🙄", "😬", "🤥",
    ],
    "❤️ 爱心": [
        "❤️", "🧡", "💛", "💚", "💙", "💜", "🖤", "🤍",
        "🤎", "💔", "❣️", "💕", "💞", "💓", "💗",
    ],
    "🎉 庆祝": [
        "🎉", "🎊", "🎈", "🎁", "🎂", "🎄", "🎃", "🎆",
        "🎇", "✨", "🔥", "⭐", "🌟", "💫", "💥",
    ],
    "🍕 食物": [
        "🍕", "🍔", "🍟", "🌭", "🍿", "🍩", "🍪", "🍰",
        "🍫", "🍬", "☕", "🍵", "🥤", "🍺", "🍻",
    ],
    "👍 手势": [
        "👍", "👎", "👏", "🙌", "🤝", "👊", "✊", "🤛",
        "🤜", "👋", "🤚", "✌️", "🤞", "🤟", "🤘",
    ],
}


class EmojiPanel(QWidget):
    """分类 Emoji 选择器面板

    Signals:
        emoji_selected(str): 用户点击选择的 emoji 字符
    """
    emoji_selected = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowFlags(Qt.Popup | Qt.FramelessWindowHint)
        self.setFocusPolicy(Qt.StrongFocus)
        self._active_button = None
        self._setup_ui()
        self._select_category(0)

    def _setup_ui(self):
        """构建面板 UI"""
        self.setFixedSize(340, 310)
        self.setStyleSheet(
            "QWidget { background-color: #2b2d31; border-radius: 12px; }"
        )

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # 分类标签栏
        self._cat_bar = QHBoxLayout()
        self._cat_bar.setContentsMargins(8, 6, 8, 4)
        self._cat_bar.setSpacing(4)
        self._cat_buttons = []
        for i, cat_name in enumerate(EMOJI_CATEGORIES.keys()):
            btn = QPushButton(cat_name)
            btn.setFixedHeight(26)
            btn.setStyleSheet(self._cat_style(False))
            btn.clicked.connect(lambda _, idx=i: self._select_category(idx))
            btn.setCursor(Qt.PointingHandCursor)
            self._cat_bar.addWidget(btn)
            self._cat_buttons.append(btn)
        self._cat_bar.addStretch()
        layout.addLayout(self._cat_bar)

        # Emoji 网格滚动区
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        scroll.setStyleSheet(
            "QScrollArea { border: none; background: transparent; }"
            "QScrollBar:vertical { width: 6px; background: transparent; }"
            "QScrollBar::handle:vertical { background: #4f545c; border-radius: 3px; min-height: 20px; }"
        )

        self._grid_container = QWidget()
        self._grid_layout = QGridLayout(self._grid_container)
        self._grid_layout.setContentsMargins(8, 4, 8, 8)
        self._grid_layout.setSpacing(2)
        self._grid_layout.setColumnStretch(8, 1)
        scroll.setWidget(self._grid_container)
        layout.addWidget(scroll)

    def _cat_style(self, active: bool) -> str:
        """分类按钮样式"""
        if active:
            return (
                "QPushButton { background-color: #5865F2; color: #fff; "
                "border: none; border-radius: 12px; font-size: 12px; padding: 0 10px; }"
            )
        return (
            "QPushButton { background-color: transparent; color: #b9bbbe; "
            "border: none; border-radius: 12px; font-size: 12px; padding: 0 10px; }"
            "QPushButton:hover { background-color: #383a40; color: #dbdee1; }"
        )

    def _select_category(self, index: int):
        """选中某个分类，刷新网格"""
        # 更新按钮样式
        if self._active_button is not None:
            self._active_button.setStyleSheet(self._cat_style(False))
        self._active_button = self._cat_buttons[index]
        self._active_button.setStyleSheet(self._cat_style(True))

        # 清空旧网格
        while self._grid_layout.count():
            item = self._grid_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

        # 填充新 emoji
        emojis = list(EMOJI_CATEGORIES.values())[index]
        for i, emoji in enumerate(emojis):
            btn = QPushButton(emoji)
            btn.setFixedSize(36, 36)
            btn.setStyleSheet(
                "QPushButton { background: transparent; border: none; "
                "font-size: 22px; border-radius: 6px; }"
                "QPushButton:hover { background-color: #383a40; }"
                "QPushButton:pressed { background-color: #4f545c; }"
            )
            btn.setCursor(Qt.PointingHandCursor)
            btn.clicked.connect(lambda _, e=emoji: self._on_emoji(e))
            row = i // 8
            col = i % 8
            self._grid_layout.addWidget(btn, row, col)

        # 填充空白到最后一行
        remainder = len(emojis) % 8
        if remainder:
            for col in range(remainder, 8):
                self._grid_layout.addWidget(QWidget(), len(emojis) // 8, col)

    def _on_emoji(self, emoji: str):
        """用户点击了某个 emoji"""
        self.emoji_selected.emit(emoji)
        self.close()

    def focusOutEvent(self, event):
        """失去焦点时自动关闭"""
        super().focusOutEvent(event)
        self.close()
