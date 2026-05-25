from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import QVBoxLayout, QHBoxLayout, QGridLayout, QLabel
from qfluentwidgets import (
    Dialog, PushButton, ComboBox, SpinBox, StrongBodyLabel,
    TabBar, TabItem, CardWidget, FluentIcon,
)

from screen_capture import ScreenCapture, SOURCE_SCREEN, SOURCE_WINDOW


AUDIO_NONE = 0
AUDIO_SYSTEM = 1
AUDIO_APPLICATION = 2


class ScreenShareDialog(Dialog):
    def __init__(self, parent=None):
        super().__init__(parent.tr("Share Screen"), "", parent)
        self.yesButton.setText(self.tr("Start Sharing"))
        self.cancelButton.setText(self.tr("Cancel"))

        self._sources = []
        self._selected_source = None
        self._audio_source = AUDIO_NONE

        layout = QVBoxLayout()
        layout.setSpacing(12)

        source_label = StrongBodyLabel(self.tr("Select Source"))
        layout.addWidget(source_label)

        self.source_combo = ComboBox()
        self.source_combo.setMinimumWidth(320)
        self.source_combo.currentIndexChanged.connect(self._on_source_changed)
        layout.addWidget(self.source_combo)

        audio_label = StrongBodyLabel(self.tr("Audio Source"))
        layout.addWidget(audio_label)

        self.audio_combo = ComboBox()
        self.audio_combo.addItems([
            self.tr("No Audio"),
            self.tr("System Audio"),
            self.tr("Application Audio (Windows only)"),
        ])
        self.audio_combo.currentIndexChanged.connect(self._on_audio_changed)
        layout.addWidget(self.audio_combo)

        fps_label = StrongBodyLabel(self.tr("Frame Rate"))
        layout.addWidget(fps_label)

        self.fps_spin = SpinBox()
        self.fps_spin.setRange(5, 30)
        self.fps_spin.setValue(15)
        layout.addWidget(self.fps_spin)

        self.contentLabel.hide()
        self.textLayout.insertLayout(self.textLayout.count(), layout)
        self.setFixedSize(420, 360)

        self._refresh_sources()

    def _refresh_sources(self):
        self._sources = ScreenCapture().enumerate_sources()
        self.source_combo.clear()
        for src in self._sources:
            type_prefix = "🖥" if src.source_type == SOURCE_SCREEN else "🪟"
            self.source_combo.addItem(f"{type_prefix} {src.name}")
        if self._sources:
            self._selected_source = self._sources[0]

    def _on_source_changed(self, index):
        if 0 <= index < len(self._sources):
            self._selected_source = self._sources[index]

    def _on_audio_changed(self, index):
        self._audio_source = index

    def get_config(self):
        if not self._selected_source:
            return None
        return {
            "source_type": self._selected_source.source_type,
            "source_index": self._selected_source.index,
            "source_name": self._selected_source.name,
            "width": self._selected_source.width,
            "height": self._selected_source.height,
            "fps": self.fps_spin.value(),
            "audio_source": self._audio_source,
        }
