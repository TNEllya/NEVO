import datetime
import hashlib
import os
import re
import sys
import tempfile

from PyQt5.QtCore import Qt, QSize, pyqtSignal, QRectF, QUrl
from PyQt5.QtGui import QColor, QFont, QPainter, QPainterPath, QPixmap, QTextCursor
from PyQt5.QtWidgets import (
    QFrame, QVBoxLayout, QHBoxLayout, QLabel, QDialog,
    QScrollArea, QWidget, QSizePolicy,
)
from qfluentwidgets import PushButton, FluentIcon

from theme_manager import ThemeManager, chat_bubble_stylesheet, system_msg_stylesheet, timestamp_stylesheet
from views.emoji_panel import EmojiPanel
from views.file_upload_dialog import select_file
from views.chat_input_bar import ChatInputBar

_USER_COLORS = [
    "#5865F2",
    "#EB459E",
    "#57F287",
    "#FEE75C",
    "#ED4245",
    "#9B59B6",
    "#3498DB",
    "#1ABC9C",
    "#E67E22",
    "#95A5A6",
]

def _user_color(name: str) -> str:
    h = int(hashlib.md5(name.encode()).hexdigest()[:8], 16)
    return _USER_COLORS[h % len(_USER_COLORS)]

class _AvatarLabel(QLabel):
    def __init__(self, size=36, parent=None):
        super().__init__(parent)
        self._size = size
        self.setFixedSize(size, size)
        self.setAlignment(Qt.AlignCenter)
        self._set_avatar_style(False)
        self.setText("?")

    def set_initial(self, text: str):
        self.setText(text[0].upper() if text else "?")
        self._set_avatar_style(False)

    def set_pixmap(self, pixmap: QPixmap):
        if not pixmap.isNull():
            super().setPixmap(pixmap.scaled(self._size, self._size,
                Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation))
            self.setStyleSheet(
                f"border-radius: {self._size // 2}px; background: transparent;"
            )
        else:
            self.clear()
            self._set_avatar_style(False)

    def _set_avatar_style(self, has_image: bool):
        if has_image:
            self.setStyleSheet(
                f"border-radius: {self._size // 2}px; background: transparent;"
            )
        else:
            self.setStyleSheet(
                f"background-color: #3a3a50; border-radius: {self._size // 2}px;"
                f"color: #8888aa; font-size: {self._size // 2}px; font-weight: bold;"
            )

class _CodeBlock(QFrame):
    def __init__(self, code: str, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 8, 10, 8)
        label = QLabel(code)
        label.setWordWrap(False)
        label.setTextInteractionFlags(Qt.TextSelectableByMouse)
        label.setStyleSheet(
            "font-family: 'Consolas', 'Courier New', monospace;"
            "font-size: 12px; color: #e0e0e0; background: transparent;"
            "padding: 0px; border: none;"
        )
        layout.addWidget(label)
        self.setStyleSheet(chat_bubble_stylesheet())

class _BubbleLabel(QLabel):
    def __init__(self, text: str, parent=None):
        super().__init__(text, parent)
        self.setMinimumWidth(0)

    def minimumSizeHint(self):
        hint = super().minimumSizeHint()
        return QSize(1, hint.height())

class _ImageLabel(QLabel):
    _cache_dir = None

    def __init__(self, file_id: str, max_width=240, parent=None):
        super().__init__(parent)
        self._file_id = file_id
        self._max_width = max_width
        self._pixmap = None
        self.setFixedSize(max_width, max_width)
        self.setAlignment(Qt.AlignCenter)
        self.setText("Loading image...")
        self.setStyleSheet(
            "QLabel { color: #72767d; font-size: 12px; background-color: #3a3a50; "
            "border-radius: 8px; }"
        )
        self.setCursor(Qt.PointingHandCursor)
        self._load_async()

    @classmethod
    def cache_dir(cls):
        if cls._cache_dir is None:
            import os
            if getattr(sys, 'frozen', False):
                base = os.path.join(os.path.dirname(sys.executable), "_image_cache")
            else:
                base = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "_image_cache")
            os.makedirs(base, exist_ok=True)
            cls._cache_dir = base
        return cls._cache_dir

    @staticmethod
    def cache_image(file_id: str, source_path: str) -> str:
        """Copy source image to cache dir, return cached path."""
        import os, shutil
        d = _ImageLabel.cache_dir()
        ext = os.path.splitext(source_path)[1] or ".png"
        cached = os.path.join(d, f"{file_id}{ext}")
        shutil.copy2(source_path, cached)
        return cached

    def _load_async(self):
        from PyQt5.QtCore import QThread, pyqtSignal as qPySignal
        fid = self._file_id
        class Loader(QThread):
            done = qPySignal(bytes, str)
            def run(self):
                import os
                d = _ImageLabel.cache_dir()
                found = None
                try:
                    for f in os.listdir(d):
                        if f.startswith(fid + ".") or f == fid:
                            found = os.path.join(d, f)
                            break
                except OSError:
                    pass
                if found and os.path.exists(found):
                    try:
                        with open(found, "rb") as fh:
                            data = fh.read()
                        if data:
                            self.done.emit(data, ""); return
                    except (OSError, IOError):
                        pass
                self.done.emit(b"", "Image not found in local cache")

        self._loader = Loader()
        self._loader.done.connect(self._on_loaded)
        self._loader.start()

    def _on_loaded(self, raw_bytes, error_msg):
        if raw_bytes:
            pm = QPixmap()
            pm.loadFromData(raw_bytes)
            if not pm.isNull():
                self._pixmap = pm
                scaled = pm.scaled(self._max_width, self._max_width,
                    Qt.KeepAspectRatio, Qt.SmoothTransformation)
                self.setPixmap(scaled)
                self.setText("")
                self.setStyleSheet(
                    "QLabel { background-color: transparent; border-radius: 8px; }"
                )
                return
        self.setText(error_msg or "Failed to load image")

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton and self._pixmap and not self._pixmap.isNull():
            dlg = QDialog(self.window())
            dlg.setWindowTitle("Image Viewer")
            dlg.setModal(True)
            layout = QVBoxLayout(dlg)
            lbl = QLabel()
            lbl.setPixmap(self._pixmap)
            lbl.setAlignment(Qt.AlignCenter)
            layout.addWidget(lbl)
            dlg.setLayout(layout)
            dlg.resize(600, 400)
            dlg.exec_()

class _FileCard(QFrame):
    def __init__(self, file_id: str, filename: str, parent=None):
        super().__init__(parent)
        self._file_id = file_id
        self._filename = filename
        layout = QHBoxLayout(self)
        layout.setContentsMargins(10, 8, 10, 8)
        icon_lbl = QLabel("📄")
        icon_lbl.setStyleSheet("font-size: 24px; background: transparent;")
        layout.addWidget(icon_lbl)
        info_layout = QVBoxLayout()
        info_layout.setSpacing(2)
        name_lbl = QLabel(filename)
        name_lbl.setStyleSheet(
            "font-size: 13px; font-weight: bold; color: #ffffff; background: transparent;"
        )
        info_layout.addWidget(name_lbl)
        size_lbl = "Unknown size"
        size_lbl_widget = QLabel(size_lbl)
        size_lbl_widget.setStyleSheet(
            "font-size: 11px; color: #72767d; background: transparent;"
        )
        info_layout.addWidget(size_lbl_widget)
        layout.addLayout(info_layout)
        layout.addStretch()
        self.setStyleSheet(
            chat_bubble_stylesheet()
            + " QFrame:hover { border: 1px solid #5865F2; }"
        )
        self.setCursor(Qt.PointingHandCursor)

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            from PyQt5.QtWidgets import QFileDialog, QMessageBox
            d = _ImageLabel.cache_dir()
            import os
            found = None
            for f in os.listdir(d):
                if f.startswith(self._file_id + ".") or f == self._file_id:
                    found = os.path.join(d, f)
                    break
            if found and os.path.exists(found):
                path, _ = QFileDialog.getSaveFileName(
                    self.window(), "Save File", self._filename
                )
                if path:
                    import shutil
                    shutil.copy2(found, path)
            else:
                QMessageBox.information(
                    self.window(), "Download",
                    f"File '{self._filename}' is not available locally."
                )

class _MessageBubble(QFrame):
    def __init__(self, text: str, is_self: bool = False, parent=None):
        super().__init__(parent)
        self._is_self = is_self
        self.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Preferred)
        self.setMinimumWidth(0)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 8, 12, 8)
        layout.setSpacing(4)
        parts = self._parse_message(text)
        is_image_only = len(parts) == 1 and parts[0]["type"] == "image"
        for part in parts:
            if part["type"] == "text":
                lbl = _BubbleLabel(part["content"])
                lbl.setWordWrap(True)
                lbl.setTextInteractionFlags(Qt.TextSelectableByMouse)
                lbl.setStyleSheet(
                    "font-size: 13px; color: #ffffff; background: transparent;"
                    "padding: 0px; border: none; line-height: 1.5;"
                )
                layout.addWidget(lbl)
            elif part["type"] == "code":
                layout.addWidget(_CodeBlock(part["content"], self))
            elif part["type"] == "image":
                img_lbl = _ImageLabel(part["file_id"], 240, self)
                layout.addWidget(img_lbl)
            elif part["type"] == "file":
                file_card = _FileCard(part["file_id"], part["filename"], self)
                layout.addWidget(file_card)

        if is_image_only:
            layout.setContentsMargins(0, 0, 0, 0)
            self.setStyleSheet("_MessageBubble { background-color: transparent; }")
            self._radius = 0
        else:
            bubble_color = "#5865F2" if is_self else "#3d3f47"
            radius = 10
            self.setStyleSheet(f"""
                _MessageBubble {{
                    background-color: {bubble_color};
                    border-radius: {radius}px;
                }}
            """)
            self._radius = radius

    def minimumSizeHint(self):
        hint = super().minimumSizeHint()
        return QSize(1, hint.height())

    @staticmethod
    def _parse_message(text: str) -> list:
        parts = []
        pattern = r'```(\w*)\n?(.*?)```'
        last = 0
        for m in re.finditer(pattern, text, re.DOTALL):
            if m.start() > last:
                parts.append({"type": "text", "content": text[last:m.start()]})
            parts.append({"type": "code", "content": m.group(2).rstrip()})
            last = m.end()
        img_pattern = r'\[IMG:(\w+)\]'
        file_pattern = r'\[FILE:(\w+):([^\]]+)\]'
        segments = []
        pos = 0
        for m in re.finditer(img_pattern, text):
            if m.start() > pos:
                segments.append({"type": "text", "content": text[pos:m.start()]})
            segments.append({"type": "image", "file_id": m.group(1)})
            pos = m.end()
        if pos < len(text):
            remaining = text[pos:]
            for m2 in re.finditer(file_pattern, remaining):
                if m2.start() > 0:
                    segments.append({"type": "text", "content": remaining[:m2.start()]})
                segments.append({"type": "file", "file_id": m2.group(1), "filename": m2.group(2)})
                pos = pos + m2.end()
                remaining = remaining[m2.end():]
            if remaining:
                segments.append({"type": "text", "content": remaining})
        if not segments:
            for m3 in re.finditer(pattern, text, re.DOTALL):
                if m3.start() > last:
                    parts.append({"type": "text", "content": text[last:m3.start()]})
                parts.append({"type": "code", "content": m3.group(2).rstrip()})
                last = m3.end()
            if last < len(text):
                parts.append({"type": "text", "content": text[last:]})
            if not parts:
                parts.append({"type": "text", "content": text})
            return parts
        result = []
        for seg in segments:
            if seg["type"] == "text":
                sub_parts = []
                sub_text = seg["content"]
                sub_last = 0
                for cm in re.finditer(pattern, sub_text, re.DOTALL):
                    if cm.start() > sub_last:
                        sub_parts.append({"type": "text", "content": sub_text[sub_last:cm.start()]})
                    sub_parts.append({"type": "code", "content": cm.group(2).rstrip()})
                    sub_last = cm.end()
                if sub_last < len(sub_text):
                    sub_parts.append({"type": "text", "content": sub_text[sub_last:]})
                if not sub_parts:
                    sub_parts.append({"type": "text", "content": sub_text})
                result.extend(sub_parts)
            else:
                result.append(seg)
        return result

    def paintEvent(self, event):
        if self._radius == 0:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        path = QPainterPath()
        rect = QRectF(self.rect())
        path.addRoundedRect(rect, self._radius, self._radius)
        painter.fillPath(path, QColor(self.palette().window().color()))
        painter.end()

class ChatMessageWidget(QFrame):
    def __init__(self, sender_name: str, sender_id: int, text: str,
                 timestamp: int = 0, is_self: bool = False,
                 avatar_pixmap: QPixmap = None, parent=None):
        super().__init__(parent)
        self._sender_name = sender_name
        self._sender_id = sender_id
        self._is_self = is_self
        self._setup_ui(sender_name, text, timestamp, avatar_pixmap)

    def _setup_ui(self, name: str, text: str, timestamp: int, avatar_pixmap=None):
        main_layout = QHBoxLayout(self)
        main_layout.setContentsMargins(12, 8, 12, 8)
        main_layout.setSpacing(10)
        avatar = _AvatarLabel(38)
        if avatar_pixmap and not avatar_pixmap.isNull():
            avatar.set_pixmap(avatar_pixmap)
        else:
            avatar.set_initial(name)
        color = _user_color(name)
        right_layout = QVBoxLayout()
        right_layout.setSpacing(4)
        right_layout.setContentsMargins(0, 0, 0, 0)
        header = QHBoxLayout()
        header.setSpacing(8)
        header.setContentsMargins(0, 0, 0, 0)
        name_lbl = QLabel(name)
        name_lbl.setStyleSheet(f"font-size: 13px; font-weight: bold; color: {color}; background: transparent;")
        header.addWidget(name_lbl)
        if timestamp > 0:
            ts_dt = datetime.datetime.fromtimestamp(timestamp / 1000)
            day_abbr = ts_dt.strftime("%A")[:3].upper()
            ts_str = ts_dt.strftime(f"%H:%M - {day_abbr}")
        else:
            ts_str = datetime.datetime.now().strftime("%H:%M")
        time_lbl = QLabel(ts_str)
        time_lbl.setStyleSheet("font-size: 11px; color: #72767d; background: transparent;")
        header.addWidget(time_lbl)
        header.addStretch()
        right_layout.addLayout(header)
        bubble_wrapper = QHBoxLayout()
        bubble_wrapper.setContentsMargins(0, 0, 0, 0)
        bubble_wrapper.setSpacing(0)
        bubble = _MessageBubble(text, self._is_self, self)
        bubble_wrapper.addWidget(bubble, 0, Qt.AlignLeft | Qt.AlignTop)
        bubble_wrapper.addStretch(1)
        right_layout.addLayout(bubble_wrapper)
        main_layout.addWidget(avatar, 0, Qt.AlignTop)
        main_layout.addLayout(right_layout, 0)
        main_layout.addStretch(1)

class SystemMessageWidget(QFrame):
    def __init__(self, text: str, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(16, 6, 16, 6)
        lbl = QLabel(text)
        lbl.setStyleSheet("font-size: 12px; color: #72767d; font-style: italic; background: transparent;")
        layout.addWidget(lbl, alignment=Qt.AlignCenter)

class ChatScrollArea(QScrollArea):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWidgetResizable(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.setStyleSheet(
            f"QScrollArea {{ border: none; background-color: {ThemeManager.instance().color('chat_bg')}; }}"
            "QScrollBar:vertical {"
            "  width: 8px; background: #2b2d31; border-radius: 4px;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: #4f545c; border-radius: 4px; min-height: 20px;"
            "}"
            "QScrollBar::handle:vertical:hover { background: #686d75; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        )
        container = QWidget()
        container.setStyleSheet(f"background-color: {ThemeManager.instance().color('chat_bg')};")
        self._layout = QVBoxLayout(container)
        self._layout.setContentsMargins(0, 8, 0, 8)
        self._layout.setSpacing(4)
        self._layout.addStretch()
        self.setWidget(container)

    def add_widget(self, widget: QWidget):
        count = self._layout.count()
        self._layout.insertWidget(count - 1, widget)

    def scroll_to_bottom(self):
        scrollbar = self.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    def clear_messages(self):
        while self._layout.count() > 1:
            item = self._layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

class ChatWidget(QFrame):
    chat_message_sent = pyqtSignal(str)
    file_upload_requested = pyqtSignal(str, int)
    image_upload_requested = pyqtSignal(str, int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._local_user_id = 0
        self._local_username = ""
        self._emoji_panel = None
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        self.message_display = ChatScrollArea()
        layout.addWidget(self.message_display, 1)
        self.input_bar = ChatInputBar()
        self.input_bar.setStyleSheet(
            "ChatInputBar { background-color: #2b2d31; border-top: 1px solid #1e1f22; }"
        )
        self.input_bar.message_sent.connect(self._on_send)
        self.input_bar.emoji_requested.connect(self._show_emoji)
        self.input_bar.file_requested.connect(self._on_file_select)
        self.input_bar.image_pasted.connect(self._on_image_pasted)
        layout.addWidget(self.input_bar)
        self.input_bar._input._text.setPlaceholderText(self.tr("Type a message..."))

    def set_local_user(self, user_id: int, username: str):
        self._local_user_id = user_id
        self._local_username = username

    def set_input_enabled(self, enabled: bool):
        self.input_bar.setEnabled(enabled)
        if not enabled:
            self.input_bar._input._text.setPlaceholderText(self.tr("Join a channel to chat"))
        else:
            self.input_bar._input._text.setPlaceholderText(self.tr("Type a message..."))

    def _show_emoji(self):
        if self._emoji_panel is None:
            self._emoji_panel = EmojiPanel(self)
            self._emoji_panel.emoji_selected.connect(self._on_emoji_selected)
        self._emoji_panel.move(
            self.input_bar._emoji_btn.mapToGlobal(self.input_bar.rect().bottomLeft())
        )
        self._emoji_panel.show()
        self._emoji_panel.raise_()

    def _on_emoji_selected(self, emoji: str):
        self.input_bar.insert_text(emoji)

    def _on_file_select(self):
        path, size = select_file(self)
        if path:
            self.file_upload_requested.emit(path, size)

    def _on_image_pasted(self, tmp_path: str):
        import os
        size = os.path.getsize(tmp_path)
        if size > 0:
            self.image_upload_requested.emit(tmp_path, size)

    def _on_send(self):
        text = self.input_bar.toPlainText().strip()
        if text:
            self.chat_message_sent.emit(text)
            self.input_bar.clear()

    def handle_upload_response(self, file_id: str, filename: str, is_image: bool):
        if is_image:
            self.chat_message_sent.emit(f"[IMG:{file_id}]")
        else:
            self.input_bar.insert_text(f"[FILE:{file_id}:{filename}]")

    def add_message(self, sender_id: int, sender_name: str, text: str,
                    timestamp: int = 0, is_self: bool = False,
                    avatar_pixmap=None):
        msg = ChatMessageWidget(sender_name, sender_id, text, timestamp, is_self, avatar_pixmap)
        self.message_display.add_widget(msg)
        self.message_display.scroll_to_bottom()

    def add_system_message(self, text: str):
        msg = SystemMessageWidget(text)
        self.message_display.add_widget(msg)
        self.message_display.scroll_to_bottom()

    def clear_chat(self):
        self.message_display.clear_messages()

    def refresh_avatars(self, user_id: int, avatar_pixmap):
        layout = self.message_display._layout
        for i in range(layout.count()):
            item = layout.itemAt(i)
            if item and item.widget():
                w = item.widget()
                if isinstance(w, ChatMessageWidget) and w._sender_id == user_id:
                    avatar = w.findChild(_AvatarLabel)
                    if avatar:
                        if avatar_pixmap and not avatar_pixmap.isNull():
                            avatar.set_pixmap(avatar_pixmap)
                        else:
                            avatar.set_initial(w._sender_name)

    def refresh_theme(self):
        tm = ThemeManager.instance()
        pal = tm.palette()
        self.message_display.setStyleSheet(
            f"QScrollArea {{ border: none; background-color: {pal['chat_bg']}; }}"
            "QScrollBar:vertical {"
            "  width: 8px; background: #2b2d31; border-radius: 4px;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: #4f545c; border-radius: 4px; min-height: 20px;"
            "}"
            "QScrollBar::handle:vertical:hover { background: #686d75; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        )
        if self.message_display.widget():
            self.message_display.widget().setStyleSheet(f"background-color: {pal['chat_bg']};")
