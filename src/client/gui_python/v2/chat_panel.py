"""NEVO v2 chat panel — messages area + input bar (design col 3 left)."""

import os
import shutil
import sys
import datetime

from PyQt5.QtCore import Qt, pyqtSignal, QSize, QObject, QThread
from PyQt5.QtGui import QPixmap
from PyQt5.QtWidgets import (
    QFrame, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton,
    QScrollArea, QWidget, QSizePolicy, QDialog, QFileDialog,
)

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from v2.theme import palette, render_icon, Avatar, IconButton, RADIUS_MD
import nevo_client  # noqa: E402  本地文件缓存工具（get_file_cache_dir / get_cached_file_path）


# 文件传输信号总线：消息行内的图片/文件卡片通过它向 ChatPanel 转发下载请求
class _FileTransferSignals(QObject):
    download_requested = pyqtSignal(str)   # file_id


_file_transfer_signals = _FileTransferSignals()


def _parse_file_parts(text: str) -> list:
    """把聊天文本拆成 [text / image / file] 部件（[IMG:id] / [FILE:id:name]）。"""
    import re
    parts = []
    pattern = re.compile(r"\[IMG:(\w+)\]|\[FILE:(\w+):([^\]]+)\]")
    pos = 0
    for m in pattern.finditer(text):
        if m.start() > pos:
            parts.append({"type": "text", "content": text[pos:m.start()]})
        if m.group(1):
            parts.append({"type": "image", "file_id": m.group(1)})
        else:
            parts.append({"type": "file", "file_id": m.group(2), "filename": m.group(3)})
        pos = m.end()
    if pos < len(text):
        parts.append({"type": "text", "content": text[pos:]})
    return parts or [{"type": "text", "content": text}]


def _fmt_timestamp(ts: int) -> str:
    try:
        dt = datetime.datetime.fromtimestamp(ts if ts < 1e12 else ts / 1000)
        return dt.strftime("今天 %H:%M")
    except Exception:
        return ""


class _V2ImageLabel(QFrame):
    """聊天消息中的图片：异步从本地缓存加载，点击查看大图/触发取回。"""

    def __init__(self, file_id: str, max_width: int = 260, parent=None):
        super().__init__(parent)
        self._file_id = file_id
        self._max_width = max_width
        self._pixmap = None
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 4, 0, 4)
        self._lbl = QLabel("Loading image...")
        self._lbl.setAlignment(Qt.AlignCenter)
        self._lbl.setFixedSize(max_width, max_width)
        self._lbl.setWordWrap(True)
        self._lbl.setStyleSheet(
            f"color: {palette().get('text_muted','')}; font-size: 12px; "
            f"background-color: rgba(255,255,255,0.06); border-radius: 8px;"
        )
        self._lbl.setCursor(Qt.PointingHandCursor)
        lay.addWidget(self._lbl)
        self.load_from_cache()

    def load_from_cache(self):
        class Loader(QThread):
            done = pyqtSignal(bytes, str)
            def run(self):
                found = nevo_client.get_cached_file_path(self._fid)
                if found and os.path.exists(found):
                    try:
                        with open(found, "rb") as fh:
                            data = fh.read()
                        if data:
                            self.done.emit(data, ""); return
                    except (OSError, IOError):
                        pass
                self.done.emit(b"", "Image not found")

        self._loader = Loader()
        self._loader._fid = self._file_id
        self._loader.done.connect(self._on_loaded)
        self._loader.start()

    def reload(self):
        self.load_from_cache()

    def _on_loaded(self, raw_bytes, error_msg):
        if raw_bytes:
            pm = QPixmap()
            pm.loadFromData(raw_bytes)
            if not pm.isNull():
                self._pixmap = pm
                scaled = pm.scaled(self._max_width, self._max_width,
                                   Qt.KeepAspectRatio, Qt.SmoothTransformation)
                self._lbl.setPixmap(scaled)
                self._lbl.setText("")
                self._lbl.setFixedSize(scaled.size())
                self._lbl.setStyleSheet("background: transparent;")
                return
        self._lbl.setText(error_msg or "Failed to load image")

    def mousePressEvent(self, event):
        if event.button() != Qt.LeftButton:
            return
        if self._pixmap and not self._pixmap.isNull():
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
        else:
            self._lbl.setText("Fetching image...")
            _file_transfer_signals.download_requested.emit(self._file_id)


class _V2FileCard(QFrame):
    """聊天消息中的文件卡片：点击保存；本地无数据时触发取回。"""

    def __init__(self, file_id: str, filename: str, parent=None):
        super().__init__(parent)
        self._file_id = file_id
        self._filename = filename
        lay = QHBoxLayout(self)
        lay.setContentsMargins(8, 6, 8, 6)
        lay.setSpacing(8)
        icon_lbl = QLabel("\U0001F4C4")
        icon_lbl.setStyleSheet("font-size: 22px; background: transparent;")
        lay.addWidget(icon_lbl)
        info = QVBoxLayout()
        info.setSpacing(2)
        name_lbl = QLabel(filename)
        name_lbl.setStyleSheet(
            f"font-size: 13px; font-weight: bold; color: {palette().get('text_primary','')}; background: transparent;"
        )
        info.addWidget(name_lbl)
        self._status_lbl = QLabel("Click to fetch")
        self._status_lbl.setStyleSheet(
            f"font-size: 11px; color: {palette().get('text_muted','')}; background: transparent;"
        )
        info.addWidget(self._status_lbl)
        lay.addLayout(info)
        lay.addStretch()
        self.setStyleSheet(
            "QFrame { background-color: rgba(255,255,255,0.06); border-radius: 8px; }"
            "QFrame:hover { border: 1px solid #2DD4A8; }"
        )
        self.setCursor(Qt.PointingHandCursor)

    def mark_available(self):
        self._status_lbl.setText("Available — click to save")

    def mousePressEvent(self, event):
        if event.button() != Qt.LeftButton:
            return
        found = nevo_client.get_cached_file_path(self._file_id)
        if found and os.path.exists(found):
            path, _ = QFileDialog.getSaveFileName(self.window(), "Save File", self._filename)
            if path:
                shutil.copy2(found, path)
        else:
            self._status_lbl.setText("Fetching...")
            _file_transfer_signals.download_requested.emit(self._file_id)


class _MessageRow(QFrame):
    """A single chat message: avatar + name + timestamp + body."""

    def __init__(self, sender_id: int, sender_name: str, text: str, timestamp: int,
                 is_self: bool, avatar_pixmap: QPixmap = None, parent=None):
        super().__init__(parent)
        self._sender_id = sender_id
        self._sender_name = sender_name
        self._is_self = is_self
        self._text_lbl = None
        self._file_widgets = []
        lay = QHBoxLayout(self)
        lay.setContentsMargins(16, 8, 16, 4)
        lay.setSpacing(12)

        self._avatar = Avatar(40)
        self._avatar.set_user(sender_name, avatar_pixmap if is_self else None)
        lay.addWidget(self._avatar, 0, Qt.AlignTop)

        body = QVBoxLayout()
        body.setContentsMargins(0, 0, 0, 0)
        body.setSpacing(2)
        head = QHBoxLayout()
        head.setSpacing(8)
        self._name_lbl = QLabel(sender_name)
        name_col = palette().get("primary", "#2DD4A8") if not is_self else palette().get("text_primary", "#E8EAF0")
        self._name_lbl.setStyleSheet(f"color: {name_col}; font-size: 14px; font-weight: 600;")
        self._name_lbl.setCursor(Qt.PointingHandCursor)
        head.addWidget(self._name_lbl)
        self._time_lbl = QLabel(_fmt_timestamp(timestamp))
        self._time_lbl.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")
        head.addWidget(self._time_lbl)
        head.addStretch()
        body.addLayout(head)

        # 消息体：文本 / 图片 / 文件卡片（[IMG:id]、[FILE:id:name]）
        for part in _parse_file_parts(text):
            if part["type"] == "text":
                lbl = QLabel(part["content"])
                lbl.setWordWrap(True)
                lbl.setTextInteractionFlags(Qt.TextSelectableByMouse)
                lbl.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 14px; line-height: 1.5;")
                lbl.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
                body.addWidget(lbl)
                if self._text_lbl is None:
                    self._text_lbl = lbl
            elif part["type"] == "image":
                w = _V2ImageLabel(part["file_id"], 260, self)
                body.addWidget(w, 0, Qt.AlignLeft)
                self._file_widgets.append(w)
            elif part["type"] == "file":
                w = _V2FileCard(part["file_id"], part["filename"], self)
                body.addWidget(w)
                self._file_widgets.append(w)
        lay.addLayout(body, 1)

    def refresh_file(self, file_id: str):
        for w in self._file_widgets:
            if w._file_id == file_id:
                if isinstance(w, _V2ImageLabel):
                    w.reload()
                else:
                    w.mark_available()

    def refresh_theme(self, avatar_pixmap: QPixmap = None):
        name_col = palette().get("primary", "#2DD4A8") if not self._is_self else palette().get("text_primary", "#E8EAF0")
        self._name_lbl.setStyleSheet(f"color: {name_col}; font-size: 14px; font-weight: 600;")
        self._time_lbl.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")
        if self._text_lbl is not None:
            self._text_lbl.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 14px;")
        if self._is_self and avatar_pixmap is not None:
            self._avatar.set_user(self._sender_name, avatar_pixmap)


class _SystemMessage(QLabel):
    def __init__(self, text: str, parent=None):
        super().__init__(text, parent)
        self._refresh()

    def _refresh(self):
        self.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 13px; font-style: italic; padding: 2px 16px;")

    def refresh_theme(self):
        self._refresh()


class ChatPanel(QFrame):
    """Chat messages + input bar."""

    chat_message_sent = pyqtSignal(str)
    file_upload_requested = pyqtSignal(str, int)    # path, size
    image_upload_requested = pyqtSignal(str, int)
    file_download_requested = pyqtSignal(str)       # file_id

    def __init__(self, parent=None):
        super().__init__(parent)
        self._local_user_id = 0
        self._local_name = ""
        self._local_avatar = None
        self._rows = []   # list of (_MessageRow|_SystemMessage)
        # 图片/文件卡片的下载请求经总线转发到本信号
        _file_transfer_signals.download_requested.connect(self.file_download_requested.emit)
        self._setup_ui()

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

        # Messages scroll area
        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setFrameShape(QFrame.NoFrame)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        host = QWidget()
        self._msg_lay = QVBoxLayout(host)
        self._msg_lay.setContentsMargins(0, 8, 0, 8)
        self._msg_lay.setSpacing(0)
        self._msg_lay.setAlignment(Qt.AlignTop)
        self._scroll.setWidget(host)
        lay.addWidget(self._scroll, 1)

        # Input area
        input_wrap = QFrame()
        input_wrap.setContentsMargins(16, 0, 16, 16)
        iw_lay = QVBoxLayout(input_wrap)
        iw_lay.setContentsMargins(0, 0, 0, 0)
        iw_lay.setSpacing(4)

        self._typing_lbl = QLabel("")
        self._typing_lbl.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px; padding: 0 4px;")
        iw_lay.addWidget(self._typing_lbl)

        bar = QFrame()
        bar.setObjectName("ChatInput")
        bar_lay = QHBoxLayout(bar)
        bar_lay.setContentsMargins(8, 4, 8, 4)
        bar_lay.setSpacing(4)

        self._btn_emoji = IconButton("smile", 20)
        self._btn_emoji.setCheckable(False)
        bar_lay.addWidget(self._btn_emoji)

        self._entry = QLineEdit()
        self._entry.setObjectName("ChatEntry")
        self._entry.setPlaceholderText("发送消息")
        self._entry.returnPressed.connect(self._on_send)
        bar_lay.addWidget(self._entry, 1)

        self._btn_file = IconButton("paperclip", 20)
        self._btn_file.setCheckable(False)
        self._btn_file.setToolTip("上传文件")
        self._btn_file.clicked.connect(self._on_pick_file)
        bar_lay.addWidget(self._btn_file)

        self._btn_send = IconButton("send", 20)
        self._btn_send.setCheckable(False)
        self._btn_send.set_color(palette().get("primary", "#2DD4A8"))
        self._btn_send.clicked.connect(self._on_send)
        bar_lay.addWidget(self._btn_send)

        iw_lay.addWidget(bar)
        lay.addWidget(input_wrap)

        self.set_input_enabled(False)

    def _on_send(self):
        text = self._entry.text().strip()
        if not text:
            return
        self.chat_message_sent.emit(text)
        self._entry.clear()

    def _on_pick_file(self):
        from PyQt5.QtWidgets import QFileDialog
        path, _ = QFileDialog.getOpenFileName(self, "选择文件", "", "所有文件 (*.*)")
        if path:
            try:
                size = os.path.getsize(path)
            except OSError:
                size = 0
            lower = path.lower()
            if lower.endswith((".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp")):
                self.image_upload_requested.emit(path, size)
            else:
                self.file_upload_requested.emit(path, size)

    # ---- Public API ----
    def set_local_user(self, user_id: int, name: str):
        self._local_user_id = user_id
        self._local_name = name
        self._entry.setPlaceholderText(f"在频道发送消息")

    def set_input_enabled(self, enabled: bool):
        self._entry.setEnabled(enabled)
        self._btn_send.setEnabled(enabled)
        self._btn_file.setEnabled(enabled)
        self._btn_emoji.setEnabled(enabled)

    def set_typing(self, name: str):
        self._typing_lbl.setText(f"{name} 正在输入..." if name else "")

    def add_message(self, sender_id: int, sender_name: str, text: str,
                    timestamp: int, is_self: bool, avatar_pixmap: QPixmap = None):
        row = _MessageRow(sender_id, sender_name, text, timestamp, is_self,
                          avatar_pixmap if is_self else None)
        self._msg_lay.addWidget(row)
        self._rows.append(row)
        self._scroll_to_bottom()

    def handle_upload_response(self, file_id: str, filename: str, is_image: bool):
        """上传成功后发送聊天标记（与 v1 语义一致）。"""
        if is_image:
            self.chat_message_sent.emit(f"[IMG:{file_id}]")
        else:
            self._entry.insert(f"[FILE:{file_id}:{filename}]")

    def on_file_cached(self, file_id: str):
        """文件字节流已写入缓存目录：刷新对应图片/文件卡片。"""
        for r in self._rows:
            if isinstance(r, _MessageRow):
                r.refresh_file(file_id)

    def add_system_message(self, text: str):
        msg = _SystemMessage(text)
        self._msg_lay.addWidget(msg)
        self._rows.append(msg)
        self._scroll_to_bottom()

    def clear_messages(self):
        while self._msg_lay.count():
            it = self._msg_lay.takeAt(0)
            w = it.widget()
            if w:
                w.deleteLater()
        self._rows = []

    def refresh_avatars(self, user_id: int, pixmap: QPixmap):
        self._local_avatar = pixmap
        for r in self._rows:
            if isinstance(r, _MessageRow) and r._sender_id == user_id:
                r.refresh_theme(pixmap)

    def _scroll_to_bottom(self):
        from PyQt5.QtCore import QTimer
        QTimer.singleShot(0, lambda: self._scroll.verticalScrollBar().setValue(
            self._scroll.verticalScrollBar().maximum()))

    def refresh_theme(self):
        self._typing_lbl.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px; padding: 0 4px;")
        for r in self._rows:
            r.refresh_theme(self._local_avatar)
