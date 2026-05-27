from PyQt5.QtCore import Qt, pyqtSignal, QTimer
from PyQt5.QtWidgets import QVBoxLayout, QHBoxLayout, QFrame, QWidget, QLabel, QFileDialog, QScrollArea
from qfluentwidgets import (
    HeaderCardWidget, ComboBox, PushButton, Slider,
    CaptionLabel, StrongBodyLabel, SwitchButton, ProgressBar,
    FluentIcon, TitleLabel, InfoBar, InfoBarPosition, CardWidget,
)

from audio_manager import AudioManager, InputMode
from avatar_manager import AvatarManager
from theme_manager import ThemeManager, THEME_DARK, THEME_LIGHT, THEME_SYSTEM
import i18n

try:
    from updater import Updater, UpdateState
    HAS_UPDATER = True
except ImportError:
    HAS_UPDATER = False


class SettingsPage(QFrame):
    input_mode_changed = pyqtSignal(str)

    def __init__(self, audio_manager: AudioManager, avatar_manager: AvatarManager = None,
                 updater=None, parent=None):
        super().__init__(parent)
        self._audio = audio_manager
        self._avatar = avatar_manager
        self._updater = updater
        self._testing_input = False
        self._level_timer = QTimer(self)
        self._level_timer.setInterval(50)
        self._level_timer.timeout.connect(self._update_input_level)
        self._ptt_listening = False
        self._setup_ui()
        self._refresh_devices()
        self._refresh_avatar()
        self._refresh_update_card()

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll.setStyleSheet(
            "QScrollArea { background: transparent; border: none; }"
            "QScrollArea > QWidget > QWidget { background: transparent; }"
            "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
            "QScrollBar::handle:vertical { background: rgba(255,255,255,0.2); border-radius: 4px; min-height: 20px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        )

        content = QWidget()
        content.setStyleSheet("background: transparent;")
        layout = QVBoxLayout(content)
        layout.setContentsMargins(24, 20, 24, 20)
        layout.setSpacing(16)

        title = TitleLabel(self.tr("Settings"))
        layout.addWidget(title)

        layout.addWidget(self._create_avatar_card())
        layout.addWidget(self._create_theme_card())
        layout.addWidget(self._create_input_card())
        layout.addWidget(self._create_input_mode_card())
        layout.addWidget(self._create_output_card())
        layout.addWidget(self._create_noise_card())
        layout.addWidget(self._create_language_card())
        if HAS_UPDATER and self._updater:
            layout.addWidget(self._create_update_card())
        layout.addStretch(1)

        scroll.setWidget(content)
        main_layout.addWidget(scroll)

    def _add_row_to_card(self, card: HeaderCardWidget, layout: QHBoxLayout):
        wrapper = QWidget()
        wrapper.setLayout(layout)
        wrapper.setStyleSheet("background: transparent;")
        card.viewLayout.addWidget(wrapper)

    def _create_avatar_card(self) -> HeaderCardWidget:
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Avatar"))

        row = QHBoxLayout()
        row.setContentsMargins(16, 16, 16, 16)
        row.setSpacing(24)

        self._avatar_preview = QLabel()
        self._avatar_preview.setFixedSize(80, 80)
        self._avatar_preview.setAlignment(Qt.AlignCenter)
        self._avatar_preview.setStyleSheet(
            "border: 2px dashed #666; border-radius: 40px; background: transparent; font-size: 32px;"
        )
        row.addWidget(self._avatar_preview)

        btn_col = QWidget()
        btn_col.setFixedWidth(120)
        btn_layout = QVBoxLayout(btn_col)
        btn_layout.setContentsMargins(0, 0, 0, 0)
        btn_layout.setSpacing(12)

        self.btn_upload_avatar = PushButton(self.tr("Upload"))
        self.btn_upload_avatar.setIcon(FluentIcon.ADD)
        self.btn_upload_avatar.clicked.connect(self._on_upload_avatar)
        btn_layout.addWidget(self.btn_upload_avatar)

        self.btn_remove_avatar = PushButton(self.tr("Remove"))
        self.btn_remove_avatar.setIcon(FluentIcon.DELETE)
        self.btn_remove_avatar.clicked.connect(self._on_remove_avatar)
        btn_layout.addWidget(self.btn_remove_avatar)

        btn_layout.addStretch()
        row.addWidget(btn_col)
        row.addStretch()

        wrapper = QWidget()
        wrapper.setLayout(row)
        wrapper.setStyleSheet("background: transparent;")
        card.viewLayout.addWidget(wrapper)
        return card

    def _refresh_avatar(self):
        if self._avatar is None:
            self._avatar_preview.hide()
            self.btn_upload_avatar.hide()
            self.btn_remove_avatar.hide()
            return
        self._avatar_preview.show()
        self.btn_upload_avatar.show()
        if self._avatar.has_avatar:
            pix = self._avatar.get_pixmap(80)
            self._avatar_preview.setPixmap(pix)
            self._avatar_preview.setStyleSheet(
                "border: none; border-radius: 40px; background: transparent;"
            )
            self.btn_remove_avatar.setEnabled(True)
            self.btn_remove_avatar.show()
        else:
            self._avatar_preview.clear()
            self._avatar_preview.setText("👤")
            self._avatar_preview.setStyleSheet(
                "border: 2px dashed #ccc; border-radius: 40px; background: #f0f0f0;"
            )
            self.btn_remove_avatar.setEnabled(False)
            self.btn_remove_avatar.hide()

    def _on_upload_avatar(self):
        path, _ = QFileDialog.getOpenFileName(
            self,
            self.tr("Select Avatar Image"),
            "",
            "Images (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*)",
        )
        if path:
            if self._avatar and self._avatar.set_avatar_from_path(path):
                self._refresh_avatar()
                InfoBar.success(
                    self.tr("Success"),
                    self.tr("Avatar updated successfully."),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=2000,
                )
            else:
                InfoBar.warning(
                    self.tr("Failed"),
                    self.tr("Could not load the image file."),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=2000,
                )

    def _on_remove_avatar(self):
        if self._avatar:
            self._avatar.remove_avatar()
            self._refresh_avatar()

    def _create_input_card(self) -> HeaderCardWidget:
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Input Device (Microphone)"))

        row1 = QHBoxLayout()
        row1.setContentsMargins(16, 8, 16, 0)
        row1.addWidget(StrongBodyLabel(self.tr("Device:")))
        self.combo_input = ComboBox()
        self.combo_input.setMinimumWidth(300)
        self.combo_input.currentIndexChanged.connect(self._on_input_device_changed)
        row1.addWidget(self.combo_input, 1)
        self._add_row_to_card(card, row1)

        row2 = QHBoxLayout()
        row2.setContentsMargins(16, 4, 16, 0)
        row2.addWidget(StrongBodyLabel(self.tr("Input Level:")))
        self.input_level_bar = ProgressBar()
        self.input_level_bar.setRange(0, 100)
        self.input_level_bar.setValue(0)
        self.input_level_bar.setFixedHeight(16)
        row2.addWidget(self.input_level_bar, 1)
        self._add_row_to_card(card, row2)

        row3 = QHBoxLayout()
        row3.setContentsMargins(16, 4, 16, 0)
        self.btn_test_input = PushButton(self.tr("Test Input"))
        self.btn_test_input.setIcon(FluentIcon.MICROPHONE)
        self.btn_test_input.setCheckable(True)
        self.btn_test_input.clicked.connect(self._toggle_input_test)
        row3.addWidget(self.btn_test_input)

        self.lbl_input_status = CaptionLabel("")
        row3.addWidget(self.lbl_input_status)
        row3.addStretch(1)
        self._add_row_to_card(card, row3)

        row4 = QHBoxLayout()
        row4.setContentsMargins(16, 4, 16, 8)
        row4.addWidget(StrongBodyLabel(self.tr("Input Gain:")))
        self.slider_input_gain = Slider(Qt.Horizontal)
        self.slider_input_gain.setRange(0, 200)
        self.slider_input_gain.setValue(int(self._audio.settings["input_gain"] * 100))
        self.slider_input_gain.setFixedWidth(200)
        self.slider_input_gain.valueChanged.connect(self._on_input_gain_changed)
        row4.addWidget(self.slider_input_gain)
        self.lbl_input_gain = CaptionLabel(f"{self._audio.settings['input_gain']:.1f}")
        self.lbl_input_gain.setFixedWidth(40)
        row4.addWidget(self.lbl_input_gain)
        row4.addStretch(1)
        self._add_row_to_card(card, row4)

        return card

    def _create_input_mode_card(self) -> HeaderCardWidget:
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Microphone Activation Mode"))

        row_mode = QHBoxLayout()
        row_mode.setContentsMargins(16, 8, 16, 0)
        row_mode.addWidget(StrongBodyLabel(self.tr("Activation Mode:")))
        self.combo_input_mode = ComboBox()
        self.combo_input_mode.setMinimumWidth(300)
        for mode in InputMode.ALL:
            self.combo_input_mode.addItem(self.tr(InputMode.display_name(mode)), userData=mode)
            if mode == self._audio.input_mode:
                self.combo_input_mode.setCurrentIndex(self.combo_input_mode.count() - 1)
        self.combo_input_mode.currentIndexChanged.connect(self._on_input_mode_changed)
        row_mode.addWidget(self.combo_input_mode, 1)
        self._add_row_to_card(card, row_mode)

        self._ptt_row = QHBoxLayout()
        self._ptt_row.setContentsMargins(16, 4, 16, 0)
        self._ptt_row.addWidget(StrongBodyLabel(self.tr("PTT Key:")))
        self.lbl_ptt_key = CaptionLabel(self._audio.ptt_key)
        self.lbl_ptt_key.setFixedWidth(120)
        self._ptt_row.addWidget(self.lbl_ptt_key)
        self.btn_set_ptt = PushButton(self.tr("Set Key"))
        self.btn_set_ptt.setFixedWidth(80)
        self.btn_set_ptt.clicked.connect(self._on_set_ptt_key)
        self._ptt_row.addWidget(self.btn_set_ptt)
        self._ptt_row.addStretch(1)
        self._add_row_to_card(card, self._ptt_row)

        self._vad_row = QHBoxLayout()
        self._vad_row.setContentsMargins(16, 4, 16, 0)
        self._vad_row.addWidget(StrongBodyLabel(self.tr("VAD Sensitivity:")))
        self.slider_vad = Slider(Qt.Horizontal)
        self.slider_vad.setRange(1, 100)
        self.slider_vad.setValue(int(self._audio.vad_threshold * 1000))
        self.slider_vad.setFixedWidth(200)
        self.slider_vad.valueChanged.connect(self._on_vad_threshold_changed)
        self._vad_row.addWidget(self.slider_vad)
        self.lbl_vad_threshold = CaptionLabel(f"{self._audio.vad_threshold:.3f}")
        self.lbl_vad_threshold.setFixedWidth(50)
        self._vad_row.addWidget(self.lbl_vad_threshold)
        self._vad_row.addStretch(1)
        self._add_row_to_card(card, self._vad_row)

        desc_row = QHBoxLayout()
        desc_row.setContentsMargins(16, 4, 16, 8)
        desc = CaptionLabel(
            self.tr(
                "Continuous: Always transmit when unmuted. "
                "PTT: Transmit only while holding the hotkey. "
                "VAD: Automatically transmit when voice is detected."
            )
        )
        desc.setWordWrap(True)
        desc.setStyleSheet("color: gray;")
        desc_row.addWidget(desc)
        self._add_row_to_card(card, desc_row)

        self._update_mode_visibility()
        return card

    def _update_mode_visibility(self):
        mode = self._audio.input_mode
        ptt_visible = (mode == InputMode.PTT)
        vad_visible = (mode == InputMode.VAD)
        self.lbl_ptt_key.setVisible(ptt_visible)
        self.btn_set_ptt.setVisible(ptt_visible)
        self.slider_vad.setVisible(vad_visible)
        self.lbl_vad_threshold.setVisible(vad_visible)
        for i in range(self._ptt_row.count()):
            item = self._ptt_row.itemAt(i)
            if item and item.widget():
                item.widget().setVisible(ptt_visible)
        for i in range(self._vad_row.count()):
            item = self._vad_row.itemAt(i)
            if item and item.widget():
                item.widget().setVisible(vad_visible)

    def _on_input_mode_changed(self, index):
        if index < 0:
            return
        mode = self.combo_input_mode.itemData(index)
        if mode:
            self._audio.set_input_mode(mode)
            self._update_mode_visibility()
            self.input_mode_changed.emit(mode)

    def _on_set_ptt_key(self):
        self._ptt_listening = True
        self.btn_set_ptt.setText(self.tr("Press key..."))
        self.btn_set_ptt.setEnabled(False)
        self.grabKeyboard()

    def keyPressEvent(self, event):
        if self._ptt_listening:
            key_combination = self._key_event_to_string(event)
            if key_combination:
                self._audio.set_ptt_key(key_combination)
                self.lbl_ptt_key.setText(key_combination)
            self._ptt_listening = False
            self.btn_set_ptt.setText(self.tr("Set Key"))
            self.btn_set_ptt.setEnabled(True)
            self.releaseKeyboard()
            return
        super().keyPressEvent(event)

    @staticmethod
    def _key_event_to_string(event) -> str:
        parts = []
        modifiers = event.modifiers()
        if modifiers & Qt.ControlModifier:
            parts.append("Ctrl")
        if modifiers & Qt.AltModifier:
            parts.append("Alt")
        if modifiers & Qt.ShiftModifier:
            parts.append("Shift")
        key = event.key()
        if key not in (Qt.Key_Control, Qt.Key_Alt, Qt.Key_Shift, Qt.Key_Meta):
            key_name = {
                Qt.Key_Space: "Space",
                Qt.Key_Return: "Enter",
                Qt.Key_Tab: "Tab",
                Qt.Key_Escape: "Esc",
                Qt.Key_Backspace: "Backspace",
            }.get(key, chr(key) if 32 <= key <= 126 else "")
            if key_name:
                parts.append(key_name)
        return "+".join(parts) if parts else ""

    def _on_vad_threshold_changed(self, value):
        threshold = value / 1000.0
        self._audio.set_vad_threshold(threshold)
        self.lbl_vad_threshold.setText(f"{threshold:.3f}")

    def _create_output_card(self) -> HeaderCardWidget:
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Output Device (Speaker)"))

        row1 = QHBoxLayout()
        row1.setContentsMargins(16, 8, 16, 0)
        row1.addWidget(StrongBodyLabel(self.tr("Device:")))
        self.combo_output = ComboBox()
        self.combo_output.setMinimumWidth(300)
        self.combo_output.currentIndexChanged.connect(self._on_output_device_changed)
        row1.addWidget(self.combo_output, 1)
        self._add_row_to_card(card, row1)

        row2 = QHBoxLayout()
        row2.setContentsMargins(16, 4, 16, 0)
        self.btn_test_output = PushButton(self.tr("Test Output"))
        self.btn_test_output.setIcon(FluentIcon.VOLUME)
        self.btn_test_output.clicked.connect(self._test_output)
        row2.addWidget(self.btn_test_output)
        self.lbl_output_status = CaptionLabel("")
        row2.addWidget(self.lbl_output_status)
        row2.addStretch(1)
        self._add_row_to_card(card, row2)

        row3 = QHBoxLayout()
        row3.setContentsMargins(16, 4, 16, 8)
        row3.addWidget(StrongBodyLabel(self.tr("Output Volume:")))
        self.slider_output_vol = Slider(Qt.Horizontal)
        self.slider_output_vol.setRange(0, 100)
        self.slider_output_vol.setValue(int(self._audio.settings["output_volume"] * 100))
        self.slider_output_vol.setFixedWidth(200)
        self.slider_output_vol.valueChanged.connect(self._on_output_volume_changed)
        row3.addWidget(self.slider_output_vol)
        self.lbl_output_vol = CaptionLabel(f"{self._audio.settings['output_volume']:.1f}")
        self.lbl_output_vol.setFixedWidth(40)
        row3.addWidget(self.lbl_output_vol)
        row3.addStretch(1)
        self._add_row_to_card(card, row3)

        return card

    def _create_noise_card(self) -> HeaderCardWidget:
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Noise Suppression"))

        row1 = QHBoxLayout()
        row1.setContentsMargins(16, 8, 16, 0)
        self.switch_noise = SwitchButton(self.tr("Enable Noise Suppression"))
        self.switch_noise.setChecked(self._audio.noise_suppression_enabled)
        self.switch_noise.checkedChanged.connect(self._on_noise_toggle)
        row1.addWidget(self.switch_noise)
        row1.addStretch(1)
        self._add_row_to_card(card, row1)

        desc_row = QHBoxLayout()
        desc_row.setContentsMargins(16, 4, 16, 8)
        desc = CaptionLabel(
            self.tr(
                "Uses spectral gating noise reduction to filter background noise from your microphone. "
                "May slightly increase CPU usage."
            )
        )
        desc.setWordWrap(True)
        desc.setStyleSheet("color: gray;")
        desc_row.addWidget(desc)
        self._add_row_to_card(card, desc_row)

        return card

    def _create_theme_card(self) -> HeaderCardWidget:
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Theme"))

        row1 = QHBoxLayout()
        row1.setContentsMargins(16, 8, 16, 0)
        row1.addWidget(StrongBodyLabel(self.tr("Appearance:")))
        self.combo_theme = ComboBox()
        self.combo_theme.setMinimumWidth(300)
        self.combo_theme.addItem(self.tr("Light"), userData=THEME_LIGHT)
        self.combo_theme.addItem(self.tr("Dark"), userData=THEME_DARK)
        self.combo_theme.addItem(self.tr("Follow System"), userData=THEME_SYSTEM)

        tm = ThemeManager.instance()
        current = tm.mode
        for i in range(self.combo_theme.count()):
            if self.combo_theme.itemData(i) == current:
                self.combo_theme.setCurrentIndex(i)
                break

        self.combo_theme.currentIndexChanged.connect(self._on_theme_changed)
        row1.addWidget(self.combo_theme, 1)
        self._add_row_to_card(card, row1)

        desc_row = QHBoxLayout()
        desc_row.setContentsMargins(16, 4, 16, 8)
        desc = CaptionLabel(
            self.tr(
                "Light: Use light color scheme. "
                "Dark: Use dark color scheme. "
                "Follow System: Automatically switch based on your OS settings."
            )
        )
        desc.setWordWrap(True)
        desc.setStyleSheet("color: gray;")
        desc_row.addWidget(desc)
        self._add_row_to_card(card, desc_row)

        return card

    def _on_theme_changed(self, index):
        if index < 0:
            return
        mode = self.combo_theme.itemData(index)
        if mode:
            tm = ThemeManager.instance()
            tm.set_mode(mode)

    def _create_language_card(self) -> HeaderCardWidget:
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Language"))

        row1 = QHBoxLayout()
        row1.setContentsMargins(16, 8, 16, 0)
        row1.addWidget(StrongBodyLabel(self.tr("Display Language:")))
        self.combo_language = ComboBox()
        self.combo_language.setMinimumWidth(300)
        for lang in i18n.available_languages():
            self.combo_language.addItem(i18n.language_display_name(lang), userData=lang)
            if lang == i18n.current_language():
                self.combo_language.setCurrentIndex(self.combo_language.count() - 1)
        self.combo_language.currentIndexChanged.connect(self._on_language_changed)
        row1.addWidget(self.combo_language, 1)
        self._add_row_to_card(card, row1)

        desc_row = QHBoxLayout()
        desc_row.setContentsMargins(16, 4, 16, 8)
        desc = CaptionLabel(
            self.tr("Change the display language. Restart the application for full effect.")
        )
        desc.setWordWrap(True)
        desc.setStyleSheet("color: gray;")
        desc_row.addWidget(desc)
        self._add_row_to_card(card, desc_row)

        return card

    def _on_language_changed(self, index):
        if index < 0:
            return
        lang = self.combo_language.itemData(index)
        if lang and lang != i18n.current_language():
            i18n.load_language(lang)
            i18n.save_language_preference(lang, "NEVO")
            InfoBar.info(
                self.tr("Language"),
                self.tr("Language changed. Please restart the application for full effect."),
                parent=self.window(),
                position=InfoBarPosition.TOP,
                duration=3000,
            )

    def _refresh_devices(self):
        self.combo_input.blockSignals(True)
        self.combo_output.blockSignals(True)

        self.combo_input.clear()
        self.combo_output.clear()

        input_devices = self._audio.enumerate_input_devices()
        output_devices = self._audio.enumerate_output_devices()

        current_input = self._audio.get_current_input_device()
        current_output = self._audio.get_current_output_device()

        input_sel = 0
        for i, dev in enumerate(input_devices):
            label = dev["name"]
            if dev["is_default"]:
                label += self.tr(" (Default)")
            self.combo_input.addItem(label, userData=dev["index"])
            if dev["index"] == current_input:
                input_sel = i

        output_sel = 0
        for i, dev in enumerate(output_devices):
            label = dev["name"]
            if dev["is_default"]:
                label += self.tr(" (Default)")
            self.combo_output.addItem(label, userData=dev["index"])
            if dev["index"] == current_output:
                output_sel = i

        self.combo_input.setCurrentIndex(input_sel)
        self.combo_output.setCurrentIndex(output_sel)

        self.combo_input.blockSignals(False)
        self.combo_output.blockSignals(False)

    def _on_input_device_changed(self, index):
        if index < 0:
            return
        dev_index = self.combo_input.itemData(index)
        if dev_index is not None:
            self._audio.set_input_device(dev_index)
            if self._testing_input:
                self._stop_input_test()
                self._start_input_test()

    def _on_output_device_changed(self, index):
        if index < 0:
            return
        dev_index = self.combo_output.itemData(index)
        if dev_index is not None:
            self._audio.set_output_device(dev_index)

    def _on_input_gain_changed(self, value):
        gain = value / 100.0
        self._audio.set_input_gain(gain)
        self.lbl_input_gain.setText(f"{gain:.1f}")

    def _on_output_volume_changed(self, value):
        vol = value / 100.0
        self._audio.set_output_volume(vol)
        self.lbl_output_vol.setText(f"{vol:.1f}")

    def _on_noise_toggle(self, checked):
        self._audio.set_noise_suppression(checked)

    def _toggle_input_test(self, checked):
        if checked:
            self._start_input_test()
            self.btn_test_input.setText(self.tr("Stop Test"))
        else:
            self._stop_input_test()
            self.btn_test_input.setText(self.tr("Test Input"))

    def _start_input_test(self):
        self._testing_input = True
        self._audio.start_input_test()
        self._level_timer.start()
        self.lbl_input_status.setText(self.tr("Listening..."))

    def _stop_input_test(self):
        self._testing_input = False
        self._audio.stop_input_test()
        self._level_timer.stop()
        self.input_level_bar.setValue(0)
        self.lbl_input_status.setText("")
        self.btn_test_input.setChecked(False)
        self.btn_test_input.setText(self.tr("Test Input"))

    def _update_input_level(self):
        level = self._audio.get_input_level()
        self.input_level_bar.setValue(int(min(level * 100, 100)))

    def _test_output(self):
        self.lbl_output_status.setText(self.tr("Playing test tone..."))
        self.btn_test_output.setEnabled(False)

        def play():
            self._audio.start_output_test()
            import time
            time.sleep(2.2)
            self._audio.stop_output_test()

        def on_done():
            self.lbl_output_status.setText("")
            self.btn_test_output.setEnabled(True)

        import threading
        t = threading.Thread(target=play, daemon=True)
        t.start()

        from PyQt5.QtCore import QTimer
        QTimer.singleShot(2500, on_done)

    def _create_update_card(self) -> HeaderCardWidget:
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Software Update"))

        tm = ThemeManager.instance()
        pal = tm.palette()

        self._update_version_row = QHBoxLayout()
        self._update_version_row.setContentsMargins(16, 8, 16, 0)
        self._update_version_row.addWidget(StrongBodyLabel(self.tr("Current Version:")))
        self._update_version_label = CaptionLabel(
            self._updater.current_version if self._updater else "-")
        self._update_version_row.addWidget(self._update_version_label)
        self._update_version_row.addStretch(1)
        self._add_row_to_card(card, self._update_version_row)

        self._update_status_row = QHBoxLayout()
        self._update_status_row.setContentsMargins(16, 4, 16, 0)
        self._update_status_label = CaptionLabel("")
        self._update_status_label.setWordWrap(True)
        self._update_status_row.addWidget(self._update_status_label)
        self._update_status_row.addStretch(1)
        self._add_row_to_card(card, self._update_status_row)

        self._update_changelog_label = QLabel("")
        self._update_changelog_label.setWordWrap(True)
        self._update_changelog_label.setTextFormat(Qt.RichText)
        self._update_changelog_label.setVisible(False)
        changelog_row = QHBoxLayout()
        changelog_row.setContentsMargins(16, 8, 16, 0)
        changelog_row.addWidget(self._update_changelog_label)
        self._add_row_to_card(card, changelog_row)

        self._update_progress = ProgressBar()
        self._update_progress.setRange(0, 100)
        self._update_progress.setValue(0)
        self._update_progress.setFixedHeight(8)
        self._update_progress.setVisible(False)
        progress_row = QHBoxLayout()
        progress_row.setContentsMargins(16, 8, 16, 0)
        progress_row.addWidget(self._update_progress)
        self._add_row_to_card(card, progress_row)

        btn_row = QHBoxLayout()
        btn_row.setContentsMargins(16, 12, 16, 8)
        btn_row.setSpacing(12)

        self._btn_check_update = PushButton(self.tr("Check for Updates"))
        self._btn_check_update.setIcon(FluentIcon.SYNC)
        self._btn_check_update.clicked.connect(self._on_check_update)
        btn_row.addWidget(self._btn_check_update)

        self._btn_download_update = PushButton(self.tr("Download Update"))
        self._btn_download_update.setIcon(FluentIcon.DOWNLOAD)
        self._btn_download_update.clicked.connect(self._on_start_download)
        self._btn_download_update.setVisible(False)
        btn_row.addWidget(self._btn_download_update)

        self._btn_install_update = PushButton(self.tr("Install & Restart"))
        self._btn_install_update.setIcon(FluentIcon.ACCEPT)
        self._btn_install_update.clicked.connect(self._on_install_update)
        self._btn_install_update.setVisible(False)
        btn_row.addWidget(self._btn_install_update)

        self._btn_cancel_update = PushButton(self.tr("Cancel"))
        self._btn_cancel_update.clicked.connect(self._on_cancel_update)
        self._btn_cancel_update.setVisible(False)
        btn_row.addWidget(self._btn_cancel_update)

        btn_row.addStretch()
        self._add_row_to_card(card, btn_row)

        self._update_check_thread = None
        self._update_download_thread = None
        self._downloaded_file = None

        return card

    def _refresh_update_card(self):
        if not self._updater:
            return
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._update_version_label.setText(self._updater.current_version)

    def _on_check_update(self):
        if not self._updater:
            return
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._set_update_checking_ui()
        from update_dialog import UpdateCheckThread
        self._update_check_thread = UpdateCheckThread(self._updater, self)
        self._update_check_thread.finished.connect(self._on_check_finished)
        self._update_check_thread.error.connect(self._on_check_error)
        self._update_check_thread.start()

    def _on_check_finished(self, result):
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._btn_check_update.setEnabled(True)
        if result:
            info = self._updater.latest_info
            if info:
                self._update_status_label.setText(
                    self.tr("New version available: v%s") % info.version)
                self._update_status_label.setStyleSheet(
                    f"color: {pal['text_accent']}; font-size: 13px; font-weight: bold;")
                if info.changelog:
                    self._update_changelog_label.setVisible(True)
                    self._update_changelog_label.setText(
                        self.tr("<b>Changelog:</b><br>%s") % info.changelog)
                    self._update_changelog_label.setStyleSheet(
                        f"color: {pal['text_secondary']}; font-size: 12px;")
                self._btn_check_update.setVisible(False)
                self._btn_download_update.setVisible(True)
        else:
            self._update_status_label.setText(self.tr("You are using the latest version."))
            self._update_status_label.setStyleSheet(
                f"color: {pal['text_secondary']}; font-size: 13px;")
            self._update_changelog_label.setVisible(False)
            self._btn_check_update.setVisible(True)
            self._btn_download_update.setVisible(False)
            self._btn_install_update.setVisible(False)
            self._btn_cancel_update.setVisible(False)

    def _on_check_error(self, error_msg: str):
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._btn_check_update.setEnabled(True)
        self._update_status_label.setText(self.tr("Check failed: %s") % error_msg)
        self._update_status_label.setStyleSheet(
            f"color: {pal['text_warning']}; font-size: 13px;")
        self._update_changelog_label.setVisible(False)
        self._btn_check_update.setVisible(True)
        self._btn_download_update.setVisible(False)
        self._btn_install_update.setVisible(False)
        self._btn_cancel_update.setVisible(False)

    def _set_update_checking_ui(self):
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._btn_check_update.setEnabled(False)
        self._btn_check_update.setVisible(True)
        self._btn_download_update.setVisible(False)
        self._btn_install_update.setVisible(False)
        self._btn_cancel_update.setVisible(False)
        self._update_progress.setVisible(False)
        self._update_status_label.setText(self.tr("Checking for updates..."))
        self._update_status_label.setStyleSheet(
            f"color: {pal['text_secondary']}; font-size: 13px;")

    def _on_start_download(self):
        if not self._updater:
            return
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._btn_check_update.setVisible(False)
        self._btn_download_update.setVisible(False)
        self._btn_install_update.setVisible(False)
        self._btn_cancel_update.setVisible(True)
        self._update_progress.setVisible(True)
        self._update_progress.setValue(0)
        self._update_status_label.setText(self.tr("Downloading update... 0%"))
        self._update_status_label.setStyleSheet(
            f"color: {pal['text_secondary']}; font-size: 13px;")
        from update_dialog import UpdateDownloadThread
        self._update_download_thread = UpdateDownloadThread(self._updater, self)
        self._update_download_thread.progress.connect(self._on_download_progress)
        self._update_download_thread.finished.connect(self._on_download_finished)
        self._update_download_thread.error.connect(self._on_download_error)
        self._update_download_thread.start()

    def _on_download_progress(self, percent, speed, downloaded, total):
        self._update_progress.setValue(int(percent))
        tm = ThemeManager.instance()
        pal = tm.palette()
        if speed > 1024 * 1024:
            speed_str = self.tr("%.1f MB/s") % (speed / (1024 * 1024))
        elif speed > 1024:
            speed_str = self.tr("%.1f KB/s") % (speed / 1024)
        else:
            speed_str = self.tr("%.0f B/s") % speed
        if total > 0:
            dl_mb = downloaded / (1024 * 1024)
            total_mb = total / (1024 * 1024)
            self._update_status_label.setText(
                self.tr("Downloading... %.0f%%  |  %.1f / %.1f MB  |  %s") % (
                    percent, dl_mb, total_mb, speed_str))
        else:
            self._update_status_label.setText(
                self.tr("Downloading... %.0f%%  |  %s") % (percent, speed_str))
        self._update_status_label.setStyleSheet(
            f"color: {pal['text_secondary']}; font-size: 13px;")

    def _on_download_finished(self, path):
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._downloaded_file = path
        self._update_progress.setValue(100)
        self._update_progress.setVisible(True)
        self._btn_download_update.setVisible(False)
        self._btn_cancel_update.setVisible(False)
        self._btn_install_update.setVisible(True)
        self._update_status_label.setText(
            self.tr("Download complete! Click 'Install & Restart' to apply the update."))
        self._update_status_label.setStyleSheet(
            f"color: {pal['text_accent']}; font-size: 13px; font-weight: bold;")

    def _on_download_error(self, error_msg: str):
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._update_progress.setVisible(False)
        self._btn_check_update.setVisible(True)
        self._btn_download_update.setVisible(False)
        self._btn_install_update.setVisible(False)
        self._btn_cancel_update.setVisible(False)
        self._update_status_label.setText(self.tr("Download failed: %s") % error_msg)
        self._update_status_label.setStyleSheet(
            f"color: {pal['text_warning']}; font-size: 13px;")

    def _on_install_update(self):
        if self._downloaded_file and self._updater:
            try:
                self._updater.install_update(self._downloaded_file)
            except Exception as e:
                tm = ThemeManager.instance()
                pal = tm.palette()
                self._update_status_label.setText(
                    self.tr("Installation failed: %s") % str(e))
                self._update_status_label.setStyleSheet(
                    f"color: {pal['text_warning']}; font-size: 13px;")

    def _on_cancel_update(self):
        if self._updater:
            self._updater.cancel_download()
        tm = ThemeManager.instance()
        pal = tm.palette()
        self._update_progress.setVisible(False)
        self._btn_cancel_update.setVisible(False)
        if self._updater and self._updater.latest_info:
            self._btn_download_update.setVisible(True)
            self._update_status_label.setText(
                self.tr("Download cancelled. You can download again."))
        else:
            self._btn_check_update.setVisible(True)
            self._update_status_label.setText(self.tr("Download cancelled."))
        self._btn_install_update.setVisible(False)
        self._update_status_label.setStyleSheet(
            f"color: {pal['text_secondary']}; font-size: 13px;")

    def cleanup(self):
        self._stop_input_test()
        self._audio.stop_output_test()
        self._audio.stop_vad()
