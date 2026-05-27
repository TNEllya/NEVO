# -*- coding: utf-8 -*-
"""
ChatInputBar — 工具栏 + 输入框 + 发送按钮

左侧工具栏: [😀 Emoji] [📎 文件]
中间: QPlainTextEdit 输入框（支持 Ctrl+V 粘贴图片）
右侧: Send 按钮

Signals:
    message_sent(str): 发送文本消息
    emoji_requested():  需要弹出 Emoji 面板
    file_requested():   需要打开文件选择器
    image_pasted(str):  粘贴了图片（参数为临时文件路径）
"""

import tempfile
from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QWidget, QHBoxLayout, QPushButton, QLabel, QSizePolicy, QTextEdit,
    QApplication,
)
from PyQt5.QtGui import QFont, QKeyEvent, QPixmap
from theme_manager import ThemeManager


class ChatInputEdit(QWidget):
    """多行输入框（支持 Enter 发送，Shift+Enter 换行，Ctrl+V 粘贴图片）"""

    message_sent = pyqtSignal()
    image_pasted = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setContentsMargins(0, 0, 0, 0)
        self._setup_ui()

    def _setup_ui(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self._text = QTextEdit(self)
        self._text.setFixedHeight(44)
        self._text.setFont(QFont("Segoe UI", 10))
        self._refresh_input_style()
        self._text.setStyleSheet(
            "QTextEdit::verticalScrollBar { width: 0px; }"
        )
        self._text.setPlaceholderText("Type a message...")
        self._text.setAcceptRichText(False)
        self._text.installEventFilter(self)
        layout.addWidget(self._text)

    def eventFilter(self, obj, event):
        if obj is self._text and event.type() == QKeyEvent.KeyPress:
            if event.key() == Qt.Key_Return and not event.modifiers() & Qt.ShiftModifier:
                self.message_sent.emit()
                return True
            if event.key() == Qt.Key_V and event.modifiers() & Qt.ControlModifier:
                clipboard = QApplication.clipboard()
                image = clipboard.image()
                if not image.isNull():
                    try:
                        suffix = ".png"
                        fd, tmp_path = tempfile.mkstemp(suffix=suffix)
                        import os
                        os.close(fd)
                        image.save(tmp_path, "PNG")
                        self.image_pasted.emit(tmp_path)
                    except Exception:
                        pass
                    return True
        return super().eventFilter(obj, event)

    def toPlainText(self) -> str:
        return self._text.toPlainText()

    def clear(self):
        self._text.clear()

    def _refresh_input_style(self):
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._text.setStyleSheet(
            f"QTextEdit {{"
            f"  background-color: {pal['bg_card_solid']};"
            f"  border: 1px solid {pal['bg_hover']};"
            f"  border-radius: 6px;"
            f"  color: {pal['text_primary']};"
            f"  padding: 8px 12px;"
            f"}}"
            f"QTextEdit:focus {{"
            f"  border: 1px solid #5865F2;"
            f"}}"
            f"QTextEdit::verticalScrollBar {{ width: 0px; }}"
        )

    def insertPlainText(self, text: str):
        self._text.insertPlainText(text)

    def setFocus(self, reason=Qt.OtherFocusReason):
        self._text.setFocus(reason)


class ChatInputBar(QWidget):
    """聊天输入栏 — 工具栏 + 输入框 + 发送"""

    message_sent = pyqtSignal(str)
    emoji_requested = pyqtSignal()
    file_requested = pyqtSignal()
    image_pasted = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(8, 4, 8, 4)
        layout.setSpacing(6)

        # 左侧工具栏
        self._toolbar = QHBoxLayout()
        self._toolbar.setSpacing(4)

        self._emoji_btn = self._tool_btn("😀", "Insert emoji")
        self._emoji_btn.clicked.connect(self.emoji_requested.emit)
        self._toolbar.addWidget(self._emoji_btn)

        self._file_btn = self._tool_btn("📎", "Upload file")
        self._file_btn.clicked.connect(self.file_requested.emit)
        self._toolbar.addWidget(self._file_btn)

        layout.addLayout(self._toolbar)

        # 输入框（支持粘贴图片）
        self._input = ChatInputEdit(self)
        self._input.message_sent.connect(self._on_send)
        self._input.image_pasted.connect(self.image_pasted.emit)
        layout.addWidget(self._input, stretch=1)

        # 发送按钮
        self._send_btn = QPushButton("Send ▶")
        self._send_btn.setFixedHeight(36)
        self._send_btn.setCursor(Qt.PointingHandCursor)
        self._send_btn.setFont(QFont("Segoe UI", 10, QFont.Bold))
        self._refresh_send_btn_style()
        self._send_btn.clicked.connect(self._on_send)
        layout.addWidget(self._send_btn)

    def _refresh_send_btn_style(self):
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._send_btn.setStyleSheet(
            f"QPushButton {{"
            f"  background-color: #5865F2;"
            f"  color: #ffffff;"
            f"  border: none;"
            f"  border-radius: 6px;"
            f"  padding: 0 16px;"
            f"}}"
            f"QPushButton:hover {{ background-color: #4752c4; }}"
            f"QPushButton:pressed {{ background-color: #3c45a5; }}"
        )

    def refresh_theme(self):
        self._input._refresh_input_style()
        self._refresh_send_btn_style()
        self._refresh_tool_btn_styles()

    def _refresh_tool_btn_styles(self):
        tm = ThemeManager.instance()
        pal = tm.palette()
        for btn in [self._emoji_btn, self._file_btn]:
            btn.setStyleSheet(
                f"QPushButton {{"
                f"  background-color: transparent;"
                f"  border: none;"
                f"  border-radius: 6px;"
                f"}}"
                f"QPushButton:hover {{"
                f"  background-color: {pal['bg_hover']};"
                f"}}"
            )
            lbl = btn.findChild(QLabel)
            if lbl:
                lbl.setStyleSheet(
                    f"background: transparent; border: none; font-size: 20px;"
                    f"color: {pal['text_muted']};"
                )

    @staticmethod
    def _tool_btn(icon_text: str, tooltip: str) -> QPushButton:
        """创建工具栏按钮，emoji 用 QLabel 渲染避免字体崩溃"""
        btn = QPushButton()
        btn.setFixedSize(36, 36)
        btn.setToolTip(tooltip)
        btn.setCursor(Qt.PointingHandCursor)
        lbl = QLabel(icon_text, btn)
        lbl.setAlignment(Qt.AlignCenter)
        lbl.setFixedSize(36, 36)
        return btn

    def _on_send(self):
        """发送消息"""
        text = self._input.toPlainText().strip()
        if text:
            self.message_sent.emit(text)
            self._input.clear()

    def insert_text(self, text: str):
        """插入文本到输入框"""
        self._input.insertPlainText(text)
        self._input.setFocus()

    def clear(self):
        self._input.clear()

    def toPlainText(self) -> str:
        return self._input.toPlainText()
