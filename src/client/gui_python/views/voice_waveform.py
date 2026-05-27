import os
import sys as _sys
import numpy as np
from PyQt5.QtCore import Qt, QTimer, QRectF, pyqtSignal, QPropertyAnimation, QEasingCurve, QPointF
from PyQt5.QtGui import (
    QPixmap, QPainter, QPainterPath, QColor, QFont,
    QLinearGradient, QPen, QBrush,
)
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QWidget, QLabel,
    QScrollArea, QSizePolicy, QFrame, QApplication,
)
from theme_manager import ThemeManager

if getattr(_sys, 'frozen', False):
    _WF_LOG = os.path.join(os.path.dirname(_sys.executable), "voice_waveform_debug.log")
else:
    _WF_LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "voice_waveform_debug.log")


def _log_wf(msg: str):
    from datetime import datetime
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    line = f"[{ts}] {msg}"
    try:
        with open(_WF_LOG, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass

_WAVEFORM_BAR_COUNT = 8
_WAVEFORM_ROW_HEIGHT = 64
_WAVEFORM_BAR_WIDTH = 4
_WAVEFORM_BAR_GAP = 3
_WAVEFORM_BAR_RADIUS = 2
_AVATAR_SIZE = 36
_SAMPLER_INTERVAL_MS = 50
_ANIM_DURATION_MS = 80
_AMPLITUDE_DECAY = 0.85
_MIN_AMPLITUDE_THRESHOLD = 0.015
_SMOOTH_FACTOR = 0.35

_COLOR_LOW = QColor("#43b581")
_COLOR_MID = QColor("#faa61a")
_COLOR_HIGH = QColor("#f04747")
_COLOR_MUTED_STRIKE = QColor("#ed4245")

def _get_theme_colors():
    tm = ThemeManager.instance()
    pal = tm.palette()
    return {
        "muted_bg": QColor(pal["bg_inner_card"]),
        "bg": QColor(pal["bg_primary"]),
        "card_bg": QColor(pal["bg_card_solid"]),
        "text_primary": QColor(pal["text_primary"]),
        "text_secondary": QColor(pal["text_secondary"]),
        "text_muted": QColor(pal["text_muted"]),
        "border": QColor(pal["bg_hover"]),
    }


class VoiceAmplitudeSampler(QWidget):
    amplitudes_updated = pyqtSignal(dict)

    def __init__(self, voice_engine, parent=None):
        super().__init__(parent)
        self._voice_engine = voice_engine
        self._timer = QTimer(self)
        self._timer.setTimerType(Qt.PreciseTimer)
        self._timer.setInterval(_SAMPLER_INTERVAL_MS)
        self._timer.timeout.connect(self._sample)
        self._levels = {}
        self._active = False
        self.setVisible(False)

    def start(self):
        if self._active:
            return
        self._active = True
        self._timer.start()

    def stop(self):
        self._active = False
        self._timer.stop()
        self._levels.clear()

    def _sample(self):
        if not self._voice_engine or not self._voice_engine._running:
            return
        try:
            if self._voice_engine._deafened:
                return

            raw_levels = self._voice_engine.get_user_audio_levels()

            updated = False
            active_uids = set(raw_levels.keys())

            for uid in list(self._levels.keys()):
                if uid not in active_uids:
                    self._levels[uid] *= _AMPLITUDE_DECAY
                    if self._levels[uid] < _MIN_AMPLITUDE_THRESHOLD:
                        self._levels[uid] = 0.0
                    updated = True

            for uid, rms in raw_levels.items():
                scaled = min(rms * 3.0, 1.0)
                prev = self._levels.get(uid, 0.0)
                if scaled > prev:
                    smoothed = scaled
                else:
                    smoothed = prev * _SMOOTH_FACTOR + scaled * (1.0 - _SMOOTH_FACTOR)
                self._levels[uid] = smoothed
                updated = True

            if updated:
                _log_wf(f"[WAVEFORM] _sample emit: levels={dict(self._levels)}")
                self.amplitudes_updated.emit(dict(self._levels))
        except Exception as e:
            _log_wf(f"[WAVEFORM] _sample EXCEPTION: {e}")

    def reset_user(self, user_id):
        self._levels.pop(user_id, None)


class _WaveformBar(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._bars = np.zeros(_WAVEFORM_BAR_COUNT, dtype=np.float32)
        self._smoothed = np.zeros(_WAVEFORM_BAR_COUNT, dtype=np.float32)
        self.setFixedHeight(_WAVEFORM_ROW_HEIGHT - 20)

    def set_amplitude(self, level):
        power = max(0.0, level)
        perceptual = min(power ** 0.5, 1.0)
        base = np.linspace(0.5, 1.0, _WAVEFORM_BAR_COUNT, dtype=np.float32)
        noise = np.random.RandomState(abs(hash(id(self))) % (2**31)).uniform(0.7, 1.0, _WAVEFORM_BAR_COUNT).astype(np.float32)
        target = np.clip(base * perceptual * noise, 0.0, 1.0)
        self._smoothed = self._smoothed * 0.65 + target * 0.35
        self.update()

    def set_muted(self):
        self._smoothed = np.zeros(_WAVEFORM_BAR_COUNT, dtype=np.float32)
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)

        bar_total_w = _WAVEFORM_BAR_COUNT * _WAVEFORM_BAR_WIDTH + (_WAVEFORM_BAR_COUNT - 1) * _WAVEFORM_BAR_GAP
        start_x = (self.width() - bar_total_w) // 2
        max_bar_h = self.height()

        for i in range(_WAVEFORM_BAR_COUNT):
            level = self._smoothed[i]
            bar_h = max(3, int(level * max_bar_h))
            x = start_x + i * (_WAVEFORM_BAR_WIDTH + _WAVEFORM_BAR_GAP)
            y = (self.height() - bar_h) // 2

            if level < 0.15:
                color = _COLOR_LOW
            elif level < 0.5:
                t = (level - 0.15) / 0.35
                color = QColor(
                    int(_COLOR_LOW.red() + (_COLOR_MID.red() - _COLOR_LOW.red()) * t),
                    int(_COLOR_LOW.green() + (_COLOR_MID.green() - _COLOR_LOW.green()) * t),
                    int(_COLOR_LOW.blue() + (_COLOR_MID.blue() - _COLOR_LOW.blue()) * t),
                )
            else:
                t = (level - 0.5) / 0.5
                color = QColor(
                    int(_COLOR_MID.red() + (_COLOR_HIGH.red() - _COLOR_MID.red()) * t),
                    int(_COLOR_MID.green() + (_COLOR_HIGH.green() - _COLOR_MID.green()) * t),
                    int(_COLOR_MID.blue() + (_COLOR_HIGH.blue() - _COLOR_MID.blue()) * t),
                )

            grad = QLinearGradient(QPointF(x, y), QPointF(x, y + bar_h))
            grad.setColorAt(0.0, color.lighter(140))
            grad.setColorAt(0.5, color)
            grad.setColorAt(1.0, color.darker(120))

            p.setBrush(QBrush(grad))
            p.setPen(Qt.NoPen)
            p.drawRoundedRect(QRectF(x, y, _WAVEFORM_BAR_WIDTH, bar_h), _WAVEFORM_BAR_RADIUS, _WAVEFORM_BAR_RADIUS)

        p.end()


class _WaveformUserRow(QWidget):
    user_clicked = pyqtSignal(int)

    def __init__(self, user_id, username, is_admin=False, muted=False, deafened=False, parent=None):
        super().__init__(parent)
        self._user_id = user_id
        self._username = username
        self._is_admin = is_admin
        self._muted = muted
        self._deafened = deafened
        self._amplitude = 0.0
        self._is_speaking = False
        self._local_muted = muted
        self._local_deafened = deafened

        self.setFixedHeight(_WAVEFORM_ROW_HEIGHT)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.setCursor(Qt.PointingHandCursor)
        self.setMouseTracking(True)
        self._hovered = False
        self._build_layout()

    def _build_layout(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(10)

        self._avatar_label = QLabel()
        self._avatar_label.setFixedSize(_AVATAR_SIZE, _AVATAR_SIZE)
        self._avatar_label.setAlignment(Qt.AlignCenter)
        self._avatar_label.setPixmap(self._default_avatar())
        layout.addWidget(self._avatar_label)

        text_layout = QVBoxLayout()
        text_layout.setSpacing(2)

        name_layout = QHBoxLayout()
        name_layout.setSpacing(4)

        self._name_label = QLabel(self._username)
        self._name_label.setFont(QFont("Segoe UI", 9, QFont.Bold))
        self._name_label.setStyleSheet(f"color: {_get_theme_colors()['text_primary'].name()}; background: transparent;")
        name_layout.addWidget(self._name_label)

        if self._is_admin:
            admin_badge = QLabel("ADMIN")
            admin_badge.setFont(QFont("Segoe UI", 7, QFont.Bold))
            admin_badge.setStyleSheet(
                "color: #faa61a; background-color: rgba(250,166,26,0.15); "
                "border-radius: 3px; padding: 1px 4px;"
            )
            admin_badge.setFixedHeight(16)
            name_layout.addWidget(admin_badge)

        name_layout.addStretch()

        self._status_label = QLabel("")
        self._status_label.setFont(QFont("Segoe UI", 8))
        tc = _get_theme_colors()
        self._status_label.setStyleSheet(f"color: {tc['text_muted'].name()}; background: transparent;")
        name_layout.addWidget(self._status_label)
        self._update_status_text()

        text_layout.addLayout(name_layout)

        self._wave_bar = _WaveformBar()
        text_layout.addWidget(self._wave_bar, 1)

        layout.addLayout(text_layout, 1)

    @staticmethod
    def _default_avatar():
        pix = QPixmap(_AVATAR_SIZE, _AVATAR_SIZE)
        pix.fill(Qt.transparent)
        p = QPainter(pix)
        p.setRenderHint(QPainter.Antialiasing)
        path = QPainterPath()
        path.addEllipse(0, 0, _AVATAR_SIZE, _AVATAR_SIZE)
        p.setClipPath(path)
        tc = _get_theme_colors()
        p.setBrush(tc["muted_bg"])
        p.setPen(Qt.NoPen)
        p.drawEllipse(0, 0, _AVATAR_SIZE, _AVATAR_SIZE)
        p.setBrush(tc["text_secondary"])
        head_r = _AVATAR_SIZE // 5
        p.drawEllipse(
            _AVATAR_SIZE // 2 - head_r, _AVATAR_SIZE // 3 - head_r,
            head_r * 2, head_r * 2,
        )
        body = QPainterPath()
        body.moveTo(_AVATAR_SIZE // 2 - _AVATAR_SIZE // 3, _AVATAR_SIZE - 4)
        body.quadTo(
            _AVATAR_SIZE // 2, _AVATAR_SIZE // 2.2,
            _AVATAR_SIZE // 2 + _AVATAR_SIZE // 3, _AVATAR_SIZE - 4,
        )
        p.drawPath(body)
        p.end()
        return pix

    def _update_status_text(self):
        parts = []
        if self._local_deafened:
            parts.append("Deafened")
        elif self._local_muted:
            parts.append("Muted")
        elif self._is_speaking:
            parts.append("Speaking")
        else:
            parts.append("")
        self._status_label.setText(" ".join(parts))

    @property
    def user_id(self):
        return self._user_id

    def set_amplitude(self, level):
        self._amplitude = max(0.0, min(1.0, level))
        was_speaking = self._is_speaking
        self._is_speaking = self._amplitude > _MIN_AMPLITUDE_THRESHOLD and not self._local_muted
        if was_speaking != self._is_speaking:
            self._update_status_text()

        if self._local_muted or self._local_deafened:
            self._wave_bar.set_muted()
        else:
            self._wave_bar.set_amplitude(self._amplitude)

    def set_muted(self, muted):
        self._local_muted = muted
        self._update_status_text()
        if muted:
            self._wave_bar.set_muted()
            self._is_speaking = False

    def set_deafened(self, deafened):
        self._local_deafened = deafened
        self._update_status_text()

    def set_avatar(self, pixmap):
        if pixmap and not pixmap.isNull():
            scaled = pixmap.scaled(_AVATAR_SIZE, _AVATAR_SIZE, Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
            self._avatar_label.setPixmap(scaled)

    def cleanup(self):
        pass

    def enterEvent(self, event):
        self._hovered = True
        self.update()

    def leaveEvent(self, event):
        self._hovered = False
        self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.user_clicked.emit(self._user_id)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)

        tc = _get_theme_colors()
        if self._hovered:
            p.setBrush(tc["border"])
        else:
            p.setBrush(tc["bg"])
        p.setPen(Qt.NoPen)
        p.drawRoundedRect(self.rect().adjusted(4, 2, -4, -2), 6, 6)

        if self._local_muted:
            p.setPen(QPen(_COLOR_MUTED_STRIKE, 1.5))
            bar_center_y = self._wave_bar.geometry().center().y()
            bar_left = self._wave_bar.geometry().left() + 4
            bar_right = self._wave_bar.geometry().right() - 4
            p.drawLine(bar_left, bar_center_y, bar_right, bar_center_y)

        p.end()


class VoiceWaveformPanel(QFrame):
    def __init__(self, voice_engine=None, parent=None):
        super().__init__(parent)
        self._voice_engine = voice_engine
        self._sampler = VoiceAmplitudeSampler(voice_engine, self)
        self._sampler.amplitudes_updated.connect(self._on_amplitudes_updated)
        self._user_rows = {}
        self._channel_name = ""
        self._users = []
        self._local_user_id = 0
        self._connected = False
        self._setup_ui()

    def _setup_ui(self):
        self.setObjectName("voiceWaveformPanel")
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(8, 8, 8, 8)
        main_layout.setSpacing(4)

        header = QHBoxLayout()
        header.setContentsMargins(4, 0, 4, 0)

        self._title_label = QLabel(self.tr("Voice Activity"))
        self._title_label.setFont(QFont("Segoe UI", 11, QFont.Bold))
        tc = _get_theme_colors()
        self._title_label.setStyleSheet(f"color: {tc['text_primary'].name()}; background: transparent;")
        header.addWidget(self._title_label)

        header.addStretch()

        self._info_label = QLabel("")
        self._info_label.setFont(QFont("Segoe UI", 8))
        tc = _get_theme_colors()
        self._info_label.setStyleSheet(f"color: {tc['text_muted'].name()}; background: transparent;")
        header.addWidget(self._info_label)

        main_layout.addLayout(header)

        self._empty_label = QLabel(self.tr("Join a channel to see voice activity"))
        self._empty_label.setFont(QFont("Segoe UI", 9))
        tc = _get_theme_colors()
        self._empty_label.setStyleSheet(f"color: {tc['text_muted'].name()}; background: transparent; padding: 20px;")
        self._empty_label.setAlignment(Qt.AlignCenter)
        self._empty_label.setWordWrap(True)
        main_layout.addWidget(self._empty_label)

        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self._scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        tc = _get_theme_colors()
        sb_color = tc["text_muted"].name()
        sb_hover = tc["text_secondary"].name()
        self._scroll.setStyleSheet(
            f"QScrollArea {{ border: none; background-color: transparent; }}"
            f"QScrollBar:vertical {{"
            f"  width: 5px; background: transparent; border-radius: 2px;"
            f"}}"
            f"QScrollBar::handle:vertical {{"
            f"  background: {sb_color}; border-radius: 2px; min-height: 16px;"
            f"}}"
            f"QScrollBar::handle:vertical:hover {{ background: {sb_hover}; }}"
            f"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}"
            f"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {{ background: none; }}"
        )
        self._scroll.setVisible(False)

        self._container = QWidget()
        self._container.setStyleSheet("background-color: transparent;")
        self._content_layout = QVBoxLayout(self._container)
        self._content_layout.setContentsMargins(0, 2, 0, 2)
        self._content_layout.setSpacing(2)
        self._content_layout.addStretch()

        self._scroll.setWidget(self._container)
        main_layout.addWidget(self._scroll, 1)

        tc = _get_theme_colors()
        self.setStyleSheet(
            f"#voiceWaveformPanel {{"
            f"  background-color: {tc['bg'].name()};"
            f"  border-top: 1px solid {tc['border'].name()};"
            f"  border-radius: 12px;"
            f"}}"
        )

    def set_voice_engine(self, engine):
        self._voice_engine = engine
        self._sampler._voice_engine = engine

    def set_connected(self, connected):
        self._connected = connected
        if not connected:
            self._sampler.stop()
            self._show_empty(self.tr("Not connected"))
        self._update_info()

    def set_channel_users(self, users, local_user_id, channel_name=""):
        self._users = users or []
        self._local_user_id = local_user_id
        self._channel_name = channel_name
        self._rebuild_rows()
        self._update_info()

        if self._connected and self._channel_name:
            self._sampler.start()
            self._empty_label.setVisible(False)
            self._scroll.setVisible(True)
        else:
            self._sampler.stop()
            self._show_empty(self.tr("Join a channel to see voice activity"))

    def _rebuild_rows(self):
        _log_wf(f"[WAVEFORM] _rebuild_rows: users={[(u.get('id'), u.get('username')) for u in self._users]} local_id={self._local_user_id}")
        for row in self._user_rows.values():
            row.cleanup()
            self._content_layout.removeWidget(row)
            row.deleteLater()
        self._user_rows.clear()

        for user in self._users:
            uid = user.get("id", 0)
            uname = user.get("username", "")
            _log_wf(f"[WAVEFORM] _rebuild_rows: adding row uid={uid} type={type(uid).__name__} name={uname}")
            row = _WaveformUserRow(
                user_id=uid,
                username=uname,
                is_admin=user.get("group_id") == 1,
                muted=bool(user.get("muted")),
                deafened=bool(user.get("deafened")),
                parent=self._container,
            )
            row.user_clicked.connect(self._on_user_clicked)
            self._user_rows[uid] = row
            self._content_layout.insertWidget(self._content_layout.count() - 1, row)

        self._sampler.reset_user(self._local_user_id)

    def _on_amplitudes_updated(self, levels):
        _log_wf(f"[WAVEFORM] _on_amplitudes_updated: uids={list(levels.keys())} rows_keys={list(self._user_rows.keys())}")
        for uid, level in levels.items():
            row = self._user_rows.get(uid)
            if row:
                row.set_amplitude(level)
            else:
                _log_wf(f"[WAVEFORM] _on_amplitudes_updated: NO ROW for uid={uid}")

    def _on_user_clicked(self, user_id):
        pass

    def _show_empty(self, text):
        self._empty_label.setText(text)
        self._empty_label.setVisible(True)
        self._scroll.setVisible(False)

    def _update_info(self):
        if self._channel_name:
            user_count = len(self._users)
            self._info_label.setText(self.tr("{}  |  {} online").format(self._channel_name, user_count))
        else:
            self._info_label.setText("")

    def refresh_theme(self):
        tm = ThemeManager.instance()
        pal = tm.palette()
        tc = _get_theme_colors()
        self._info_label.setStyleSheet(f"color: {pal['text_muted']}; background: transparent;")
        self._empty_label.setStyleSheet(f"color: {pal['text_muted']}; background: transparent; padding: 20px;")
        self._title_label.setStyleSheet(f"color: {pal['text_primary']}; background: transparent;")
        self.setStyleSheet(
            f"#voiceWaveformPanel {{"
            f"  background-color: {tc['bg'].name()};"
            f"  border-top: 1px solid {tc['border'].name()};"
            f"  border-radius: 12px;"
            f"}}"
        )
        sb_color = tc["text_muted"].name()
        sb_hover = tc["text_secondary"].name()
        self._scroll.setStyleSheet(
            f"QScrollArea {{ border: none; background-color: transparent; }}"
            f"QScrollBar:vertical {{"
            f"  width: 5px; background: transparent; border-radius: 2px;"
            f"}}"
            f"QScrollBar::handle:vertical {{"
            f"  background: {sb_color}; border-radius: 2px; min-height: 16px;"
            f"}}"
            f"QScrollBar::handle:vertical:hover {{ background: {sb_hover}; }}"
            f"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}"
            f"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {{ background: none; }}"
        )
        for row in self._user_rows.values():
            row._status_label.setStyleSheet(f"color: {pal['text_muted']}; background: transparent;")
            row._name_label.setStyleSheet(f"color: {pal['text_primary']}; background: transparent;")
        self.update()