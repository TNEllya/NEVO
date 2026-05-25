import json
import os

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QLabel, QSlider, QPushButton,
    QDialog, QWidget,
)

_SETTINGS_DIR = os.path.join(os.path.expanduser("~"), ".nevo")
_SETTINGS_FILE = os.path.join(_SETTINGS_DIR, "per_user_volume.json")

MAX_RECENT_SERVERS = 5


class PerUserVolumeManager:
    def __init__(self, voice_engine=None):
        self._voice_engine = voice_engine
        self._settings = {}
        self._load()

    def _server_key(self, host, port):
        return "{}:{}".format(host, port)

    def save_settings(self, host, port, user_id):
        if self._voice_engine is None:
            return
        engine_settings = self._voice_engine.get_all_user_settings()
        key = self._server_key(host, port)
        server_data = self._settings.setdefault(key, {})
        server_data[str(user_id)] = {
            "volume": engine_settings["volumes"].get(user_id, 1.0),
            "muted": user_id in engine_settings["mutes"],
        }
        self._trim_recent(key)
        self._save()

    def restore_settings(self, host, port, user_ids):
        if self._voice_engine is None:
            return
        key = self._server_key(host, port)
        server_data = self._settings.get(key, {})
        for uid in user_ids:
            uid_str = str(uid)
            if uid_str in server_data:
                entry = server_data[uid_str]
                self._voice_engine.set_user_volume(uid, entry.get("volume", 1.0))
                self._voice_engine.set_user_local_mute(uid, entry.get("muted", False))

    def get_user_volume(self, host, port, user_id):
        key = self._server_key(host, port)
        entry = self._settings.get(key, {}).get(str(user_id), {})
        return entry.get("volume", 1.0)

    def is_user_muted(self, host, port, user_id):
        key = self._server_key(host, port)
        entry = self._settings.get(key, {}).get(str(user_id), {})
        return entry.get("muted", False)

    def _trim_recent(self, keep_key):
        if len(self._settings) <= MAX_RECENT_SERVERS:
            return
        keys = list(self._settings.keys())
        if keep_key not in keys:
            keys_to_remove = keys[:len(keys) - MAX_RECENT_SERVERS]
        else:
            others = [k for k in keys if k != keep_key]
            keys_to_remove = others[:len(others) - (MAX_RECENT_SERVERS - 1)]
        for k in keys_to_remove:
            self._settings.pop(k, None)

    def _load(self):
        try:
            if os.path.exists(_SETTINGS_FILE):
                with open(_SETTINGS_FILE, "r", encoding="utf-8") as f:
                    self._settings = json.load(f)
        except Exception:
            self._settings = {}

    def _save(self):
        try:
            os.makedirs(_SETTINGS_DIR, exist_ok=True)
            with open(_SETTINGS_FILE, "w", encoding="utf-8") as f:
                json.dump(self._settings, f, indent=2)
        except Exception:
            pass


class VolumeSliderDialog(QDialog):
    volume_changed = pyqtSignal(float)

    def __init__(self, user_name, current_volume, parent=None):
        super().__init__(parent)
        self.setWindowTitle(self.tr("Adjust Volume - {}").format(user_name))
        self.setFixedSize(320, 200)
        self.setWindowFlags(self.windowFlags() & ~Qt.WindowContextHelpButtonHint)

        layout = QVBoxLayout(self)
        layout.setSpacing(12)
        layout.setContentsMargins(20, 16, 20, 16)

        title_label = QLabel(self.tr("Volume for {}").format(user_name))
        title_label.setStyleSheet("color: #ffffff; font-size: 13px; font-weight: bold;")
        layout.addWidget(title_label)

        slider_row = QHBoxLayout()
        slider_row.setSpacing(10)

        self._slider = QSlider(Qt.Horizontal)
        self._slider.setMinimum(0)
        self._slider.setMaximum(100)
        self._slider.setValue(int(current_volume * 100))
        self._slider.setStyleSheet(
            "QSlider::groove:horizontal {"
            "  height: 6px; background: #4f545c; border-radius: 3px;"
            "}"
            "QSlider::handle:horizontal {"
            "  background: #5865f2; width: 14px; height: 14px;"
            "  margin: -4px 0; border-radius: 7px;"
            "}"
            "QSlider::sub-page:horizontal {"
            "  background: #5865f2; border-radius: 3px;"
            "}"
        )
        self._slider.valueChanged.connect(self._on_slider_changed)
        slider_row.addWidget(self._slider, 1)

        self._percent_label = QLabel("{}%".format(int(current_volume * 100)))
        self._percent_label.setFixedWidth(40)
        self._percent_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self._percent_label.setStyleSheet("color: #b9bbbe; font-size: 13px;")
        slider_row.addWidget(self._percent_label)

        layout.addLayout(slider_row)

        vol_icon_row = QHBoxLayout()
        vol_icon_row.addWidget(QLabel("\U0001f507"))
        vol_icon_row.addStretch()
        vol_icon_row.addWidget(QLabel("\U0001f50a"))
        layout.addLayout(vol_icon_row)

        btn_row = QHBoxLayout()
        btn_row.addStretch()
        apply_btn = QPushButton(self.tr("Apply"))
        apply_btn.setFixedWidth(80)
        apply_btn.setStyleSheet(
            "QPushButton {"
            "  background-color: #5865f2; color: white; border: none;"
            "  border-radius: 4px; padding: 6px 16px; font-size: 12px;"
            "}"
            "QPushButton:hover { background-color: #4752c4; }"
        )
        apply_btn.clicked.connect(self.accept)
        btn_row.addWidget(apply_btn)
        layout.addLayout(btn_row)

    def _on_slider_changed(self, value):
        self._percent_label.setText("{}%".format(value))
        self.volume_changed.emit(value / 100.0)

    def get_volume(self):
        return self._slider.value() / 100.0