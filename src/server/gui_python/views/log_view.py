"""Log view - Server log message viewer."""

from PyQt5.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt5.QtWidgets import QVBoxLayout, QHBoxLayout, QFrame
from qfluentwidgets import (
    HeaderCardWidget, TitleLabel, CaptionLabel, TextEdit,
    PushButton, FluentIcon, StrongBodyLabel,
)


class _LogSignalHelper(QObject):
    """Helper to emit log messages from any thread to the UI thread."""
    log_message = pyqtSignal(str)


class LogView(QFrame):
    """Server log viewer page."""

    MAX_LOG_LINES = 2000

    def __init__(self, server_proc, parent=None):
        super().__init__(parent)
        self.server = server_proc
        self.setObjectName("logView")

        self._signal_helper = _LogSignalHelper()
        self._signal_helper.log_message.connect(self._append_log)

        # Register for log events from server process
        self.server.set_on_log(self._on_log_message)

        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(36, 20, 36, 20)
        layout.setSpacing(16)

        # Title
        title = TitleLabel(self.tr("Server Log"))
        layout.addWidget(title)

        desc = CaptionLabel(self.tr("Real-time server log output"))
        layout.addWidget(desc)
        layout.addSpacing(8)

        # Log card
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Log Output"))

        # Toolbar
        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)
        toolbar.addStretch(1)

        self.btn_clear = PushButton(self.tr("Clear"))
        self.btn_clear.setIcon(FluentIcon.DELETE)
        self.btn_clear.clicked.connect(self._on_clear)
        toolbar.addWidget(self.btn_clear)

        card.viewLayout.insertLayout(0, toolbar)

        # Log text area
        self.log_text = TextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setMinimumHeight(400)
        self.log_text.setStyleSheet(
            "QTextEdit {"
            "  font-family: 'Consolas', 'Courier New', monospace;"
            "  font-size: 12px;"
            "  background-color: #1e1e2e;"
            "  color: #cdd6f4;"
            "  border: 1px solid #313244;"
            "  border-radius: 8px;"
            "  padding: 8px;"
            "}"
        )

        card.viewLayout.addWidget(self.log_text)
        layout.addWidget(card)

    def _on_log_message(self, msg: str):
        """Called from the control client on any thread."""
        self._signal_helper.log_message.emit(msg)

    def _append_log(self, msg: str):
        """Append a log message (called on the UI thread via signal)."""
        self.log_text.append(msg)

        # Limit log lines
        doc = self.log_text.document()
        if doc.blockCount() > self.MAX_LOG_LINES:
            cursor = self.log_text.textCursor()
            cursor.movePosition(cursor.Start)
            cursor.movePosition(cursor.Down, cursor.KeepAnchor,
                                doc.blockCount() - self.MAX_LOG_LINES)
            cursor.removeSelectedText()

        # Auto-scroll
        scrollbar = self.log_text.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    def _on_clear(self):
        self.log_text.clear()

    def append_log(self, msg: str):
        """Public method to append log messages."""
        self._append_log(msg)

    def stop(self):
        pass
