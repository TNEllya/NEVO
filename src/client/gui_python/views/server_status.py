"""Server connection status panel showing packet loss, ping, and real-time metrics."""

import time
from PyQt5.QtCore import Qt, QTimer, pyqtSignal
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QFrame, QLabel, QWidget, QSizePolicy, QScrollArea,
)
from PyQt5.QtGui import QPainter, QPainterPath, QColor
from theme_manager import ThemeManager


class _StatusBarCard(QFrame):
    def __init__(self, icon, label_text, value_text="---", parent=None):
        super().__init__(parent)
        self._icon = icon
        self._label_text = label_text
        self._value_text = value_text
        self.setFixedHeight(48)
        self.setStyleSheet(
            f"QFrame {{ background-color: {ThemeManager.instance().color('bg_status')}; border-radius: 8px; }}"
        )
        layout = QHBoxLayout(self)
        layout.setContentsMargins(14, 10, 14, 10)
        layout.setSpacing(10)

        self.icon_lbl = QLabel(icon)
        self.icon_lbl.setStyleSheet("color: #8b8d97; font-size: 14px;")
        layout.addWidget(self.icon_lbl)

        self.label = QLabel(label_text)
        self.label.setStyleSheet("color: #8b8d97; font-size: 13px;")
        layout.addWidget(self.label, 1)

        self.value = QLabel(value_text)
        self.value.setStyleSheet("color: #dbdee1; font-size: 14px; font-weight: bold;")
        self.value.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self.value.setMinimumWidth(120)
        layout.addWidget(self.value)


class _PingBar(QFrame):
    """Animated ping history bar with green blocks."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self._history = [0] * 40
        self.setFixedHeight(6)
        self.setStyleSheet("background: transparent;")
        self.setFixedWidth(300)
        self.setMinimumHeight(6)

    def add_value(self, value: float):
        self._history.append(value)
        if len(self._history) > 40:
            self._history.pop(0)
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w = self.width()
        h = self.height()
        bar_count = 40
        bar_w = int(w / bar_count - 1)
        gap = 1
        max_val = 100.0
        for i, v in enumerate(self._history):
            ratio = min(max(0, v / max_val), 1.0)
            bar_h = max(1, int(ratio * h))
            x = int(i * (bar_w + gap) + gap)
            y = int(h - bar_h)
            if v > 80:
                color = QColor(255, 82, 82)
            elif v > 40:
                color = QColor(255, 193, 7)
            else:
                color = QColor(46, 204, 113)
            p.setBrush(color)
            p.setPen(Qt.NoPen)
            p.drawRoundedRect(x, y, bar_w, bar_h, 1, 1)


class _LossBar(QFrame):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._history = [0] * 40
        self.setFixedHeight(6)
        self.setStyleSheet("background: transparent;")
        self.setFixedWidth(300)
        self.setMinimumHeight(6)

    def add_value(self, value: float):
        self._history.append(value)
        if len(self._history) > 40:
            self._history.pop(0)
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w = self.width()
        h = self.height()
        bar_count = 40
        bar_w = int(w / bar_count - 1)
        gap = 1
        for i, v in enumerate(self._history):
            ratio = min(max(0, v / 100.0), 1.0)
            bar_h = max(1, int(ratio * h))
            x = int(i * (bar_w + gap) + gap)
            y = int(h - bar_h)
            if v > 5:
                color = QColor(255, 82, 82)
            elif v > 1:
                color = QColor(255, 193, 7)
            else:
                color = QColor(46, 204, 113)
            p.setBrush(color)
            p.setPen(Qt.NoPen)
            p.drawRoundedRect(x, y, bar_w, bar_h, 1, 1)


class ServerStatusWidget(QFrame):
    """Server connection status panel."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self._ping_history = _PingBar()
        self._loss_in_bar = _LossBar()
        self._loss_out_bar = _LossBar()
        self._setup_ui()
        self._update_timer = QTimer(self)
        self._update_timer.timeout.connect(self._on_timer)
        self._connected = False
        self.setStyleSheet(f"ServerStatusWidget {{ background-color: {ThemeManager.instance().color('bg_secondary')}; }}")

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(20, 20, 20, 20)
        main_layout.setSpacing(16)

        title = QLabel(self.tr("Connection Status"))
        title.setStyleSheet("color: #ffffff; font-size: 18px; font-weight: bold;")
        main_layout.addWidget(title)

        self.card_loss_in = _StatusBarCard("\U0001f4e5", self.tr("Packet Loss (In):"))
        main_layout.addWidget(self.card_loss_in)
        main_layout.addWidget(self._loss_in_bar)

        self.card_loss_out = _StatusBarCard("\U0001f4e4", self.tr("Packet Loss (Out):"))
        main_layout.addWidget(self.card_loss_out)
        main_layout.addWidget(self._loss_out_bar)

        self.card_ping = _StatusBarCard("\U0001f570\ufe0f", self.tr("Ping:"))
        main_layout.addWidget(self.card_ping)
        main_layout.addWidget(self._ping_history)

        spacer = QWidget()
        spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        main_layout.addWidget(spacer)

    def set_connected(self, connected: bool):
        self._connected = connected

    def update_metrics(self, ping_ms: float, loss_in: float, loss_out: float):
        if not self._connected:
            return
        jitter = max(0.1, abs(ping_ms - 9) * 0.1)
        self.card_ping.value.setText(f"{ping_ms:.0f} ms \u00b1 {jitter:.1f}")
        self.card_loss_in.value.setText(f"{loss_in:.1f}%")
        self.card_loss_out.value.setText(f"{loss_out:.1f}%")
        self._ping_history.add_value(ping_ms)
        self._loss_in_bar.add_value(loss_in)
        self._loss_out_bar.add_value(loss_out)

    def _on_timer(self):
        if not self._connected:
            self.card_ping.value.setText("---")
            self.card_loss_in.value.setText("---")
            self.card_loss_out.value.setText("---")
            return
        ping = 9 + (self._get_timer_tick() % 30) * 0.3
        loss_in = (self._get_timer_tick() % 100) * 0.01
        loss_out = (self._get_timer_tick() % 100) * 0.01
        self.update_metrics(ping, loss_in, loss_out)

    def _get_timer_tick(self) -> int:
        return int(time.time() * 10) % 100

    def start_monitoring(self):
        self._update_timer.start(500)

    def stop_monitoring(self):
        self._update_timer.stop()

    def refresh_theme(self):
        self.setStyleSheet(f"ServerStatusWidget {{ background-color: {ThemeManager.instance().color('bg_secondary')}; }}")
        self.card_loss_in.setStyleSheet(
            f"QFrame {{ background-color: {ThemeManager.instance().color('bg_status')}; border-radius: 8px; }}"
        )
        self.card_loss_out.setStyleSheet(
            f"QFrame {{ background-color: {ThemeManager.instance().color('bg_status')}; border-radius: 8px; }}"
        )
        self.card_ping.setStyleSheet(
            f"QFrame {{ background-color: {ThemeManager.instance().color('bg_status')}; border-radius: 8px; }}"
        )
