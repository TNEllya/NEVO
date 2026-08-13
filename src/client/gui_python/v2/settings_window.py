"""NEVO v2 settings window — design/pages/设置.html.

Audio settings page (the active section in the design) wired to the existing
AudioManager. Other nav sections render as simple placeholders.
"""

import os
import sys

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QFrame,
    QComboBox, QSlider, QScrollArea, QButtonGroup, QRadioButton, QStackedWidget,
)
from qfluentwidgets import ComboBox

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from v2.theme import palette, render_icon, render_icon_qicon, v2_qss
from audio_manager import AudioManager, InputMode


class _Toggle(QFrame):
    """iOS-style toggle switch matching the design."""

    toggled = pyqtSignal(bool)

    def __init__(self, checked: bool = False, parent=None):
        super().__init__(parent)
        self.setFixedSize(40, 22)
        self.setCursor(Qt.PointingHandCursor)
        self._checked = checked
        self._knob = QFrame(self)
        self._knob.setFixedSize(18, 18)
        self._refresh()
        self._anim = None

    def _refresh(self):
        if self._checked:
            self.setObjectName("ToggleOn")
            self._knob.move(20, 2)
        else:
            self.setObjectName("ToggleOff")
            self._knob.move(2, 2)
        self._knob.setStyleSheet("background: white; border-radius: 9px;")
        self.style().unpolish(self)
        self.style().polish(self)

    def isChecked(self) -> bool:
        return self._checked

    def setChecked(self, checked: bool):
        self._checked = checked
        self._refresh()
        self.toggled.emit(checked)

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            self.setChecked(not self._checked)


class _Row(QFrame):
    """A labeled settings row: [label 140px] [control]."""

    def __init__(self, label: str, parent=None):
        super().__init__(parent)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(16)
        self._lbl = QLabel(label)
        self._lbl.setFixedWidth(140)
        self._lbl.setStyleSheet(f"color: {palette().get('text_secondary','')}; font-size: 13px;")
        lay.addWidget(self._lbl)
        self._control_wrap = QFrame()
        self._control_wrap.setMaximumWidth(400)
        cl = QHBoxLayout(self._control_wrap)
        cl.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self._control_wrap)
        lay.addStretch()
        self._cl = cl

    def add_widget(self, w):
        self._cl.addWidget(w)

    def add_layout(self, l):
        self._cl.addLayout(l)

    def set_label_width(self, w: int):
        self._lbl.setFixedWidth(w)


class _Section(QFrame):
    def __init__(self, title: str, parent=None):
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(12)
        h = QLabel(title)
        h.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 17px; font-weight: 600;")
        lay.addWidget(h)
        self._body = QVBoxLayout()
        self._body.setSpacing(12)
        lay.addLayout(self._body)

    def add_row(self, row: _Row):
        self._body.addWidget(row)


class SettingsWindow(QWidget):
    """Standalone settings window with left nav + content."""

    back_requested = pyqtSignal()
    input_mode_changed = pyqtSignal(str)

    def __init__(self, audio_manager: AudioManager, parent=None):
        super().__init__(parent)
        self.setWindowFlags(Qt.Window)
        self.setWindowTitle("设置 — NEVO")
        self.resize(900, 680)
        self.setMinimumSize(720, 560)
        self.setStyleSheet(v2_qss())
        self._audio = audio_manager
        self._ptt_listening = False
        self._setup_ui()
        self._refresh_devices()

    def _setup_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # Header bar
        header = QFrame()
        header.setStyleSheet(f"background-color: {palette().get('bg_secondary','')}; border-bottom: 1px solid {palette().get('bg_hover','')};")
        header.setFixedHeight(56)
        hlay = QHBoxLayout(header)
        hlay.setContentsMargins(24, 0, 24, 0)
        back = QPushButton("返回")
        back.setObjectName("GhostBtn")
        back.setCursor(Qt.PointingHandCursor)
        back.setIcon(render_icon_qicon("arrow-left", 16, palette().get("text_secondary", "#9CA3B4")))
        back.setIconSize(__import__("PyQt5.QtCore", fromlist=["QSize"]).QSize(16, 16))
        back.clicked.connect(self.back_requested.emit)
        hlay.addWidget(back)
        hlay.addStretch()
        title = QLabel("设置")
        title.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 20px; font-weight: 600;")
        hlay.addWidget(title)
        hlay.addStretch()
        ver = QLabel("v2.0")
        ver.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 11px;")
        hlay.addWidget(ver)
        root.addWidget(header)

        # Body: nav + content
        body = QHBoxLayout()
        body.setContentsMargins(0, 0, 0, 0)
        body.setSpacing(0)

        # Left nav
        nav = QFrame()
        nav.setFixedWidth(200)
        nav.setStyleSheet(f"background-color: {palette().get('bg_secondary','')}; border-right: 1px solid {palette().get('bg_hover','')};")
        nlay = QVBoxLayout(nav)
        nlay.setContentsMargins(16, 24, 16, 24)
        nlay.setSpacing(2)
        self._nav_btns = {}
        for key, label, icon in [
            ("account", "账户", "user"),
            ("audio", "音频设置", "mic"),
            ("video", "视频设置", "video"),
            ("notifications", "通知", "bell"),
            ("hotkeys", "快捷键", "keyboard"),
            ("about", "关于", "info"),
        ]:
            btn = QPushButton(f"  {label}")
            btn.setObjectName("SettingsNav")
            btn.setCheckable(True)
            btn.setCursor(Qt.PointingHandCursor)
            btn.setIcon(render_icon_qicon(icon, 18, palette().get("text_secondary", "#9CA3B4")))
            btn.setIconSize(__import__("PyQt5.QtCore", fromlist=["QSize"]).QSize(18, 18))
            btn.clicked.connect(lambda checked, k=key: self._show_section(k))
            nlay.addWidget(btn)
            self._nav_btns[key] = btn
        nlay.addStretch()
        body.addWidget(nav)

        # Right content (stacked)
        self._stack = QStackedWidget()
        self._stack.setStyleSheet(f"background-color: {palette().get('bg_primary','')};")
        self._build_audio_page()
        self._build_placeholder("账户", "账户管理功能即将推出。")
        self._build_placeholder("视频设置", "视频设备与编码设置即将推出。")
        self._build_placeholder("通知", "通知偏好设置即将推出。")
        self._build_placeholder("快捷键", "快捷键自定义即将推出。")
        self._build_about_page()
        body.addWidget(self._stack, 1)
        root.addLayout(body, 1)

        self._show_section("audio")

    def _build_audio_page(self):
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        host = QWidget()
        host.setStyleSheet(f"background-color: {palette().get('bg_primary','')};")
        lay = QVBoxLayout(host)
        lay.setContentsMargins(32, 32, 32, 32)
        lay.setSpacing(24)

        # Section: 输入设备
        sec_in = _Section("输入设备")
        r_mic = _Row("麦克风")
        self._combo_mic = ComboBox()
        self._combo_mic.setMinimumWidth(360)
        r_mic.add_widget(self._combo_mic)
        sec_in.add_row(r_mic)

        r_sens = _Row("输入灵敏度")
        self._slider_sens = QSlider(Qt.Horizontal)
        self._slider_sens.setRange(0, 100)
        self._slider_sens.setValue(65)
        r_sens.add_widget(self._slider_sens)
        sec_in.add_row(r_sens)

        r_test = _Row("")
        btn_test = QPushButton("测试麦克风")
        btn_test.setObjectName("OutlineBtn")
        btn_test.setCursor(Qt.PointingHandCursor)
        btn_test.setIcon(render_icon_qicon("play", 14, palette().get("primary", "#2DD4A8")))
        btn_test.setIconSize(__import__("PyQt5.QtCore", fromlist=["QSize"]).QSize(14, 14))
        r_test.add_widget(btn_test)
        sec_in.add_row(r_test)
        lay.addWidget(sec_in)

        lay.addWidget(self._divider())

        # Section: 输出设备
        sec_out = _Section("输出设备")
        r_spk = _Row("扬声器")
        self._combo_spk = ComboBox()
        self._combo_spk.setMinimumWidth(360)
        r_spk.add_widget(self._combo_spk)
        sec_out.add_row(r_spk)

        r_vol = _Row("输出音量")
        vol_row = QHBoxLayout()
        vol_row.setSpacing(8)
        self._slider_vol = QSlider(Qt.Horizontal)
        self._slider_vol.setRange(0, 100)
        self._slider_vol.setValue(75)
        vol_row.addWidget(self._slider_vol)
        self._vol_lbl = QLabel("75%")
        self._vol_lbl.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 13px; min-width: 32px;")
        vol_row.addWidget(self._vol_lbl)
        r_vol.add_layout(vol_row)
        sec_out.add_row(r_vol)
        lay.addWidget(sec_out)

        lay.addWidget(self._divider())

        # Section: 高级设置
        sec_adv = _Section("高级设置")
        self._toggle_ns = self._add_toggle_row(sec_adv, "噪声抑制", True)
        self._toggle_aec = self._add_toggle_row(sec_adv, "回声消除", True)
        self._toggle_agc = self._add_toggle_row(sec_adv, "自动调节增益", False)
        r_codec = _Row("音频编码")
        self._combo_codec = ComboBox()
        for c in ["Opus 48kHz", "Opus 24kHz", "Opus 16kHz", "Opus 8kHz"]:
            self._combo_codec.addItem(c)
        r_codec.add_widget(self._combo_codec)
        sec_adv.add_row(r_codec)
        lay.addWidget(sec_adv)

        lay.addWidget(self._divider())

        # Section: 语音活动检测
        sec_vad = _Section("语音活动检测")
        r_mode = _Row("")
        mode_row = QHBoxLayout()
        mode_row.setSpacing(24)
        self._rb_vad = QRadioButton("语音激活")
        self._rb_ptt = QRadioButton("按键说话")
        self._rb_cont = QRadioButton("持续输出")
        self._mode_group = QButtonGroup(self)
        self._mode_group.addButton(self._rb_vad)
        self._mode_group.addButton(self._rb_ptt)
        self._mode_group.addButton(self._rb_cont)
        mode_row.addWidget(self._rb_vad)
        mode_row.addWidget(self._rb_ptt)
        mode_row.addWidget(self._rb_cont)
        r_mode.add_layout(mode_row)
        sec_vad.add_row(r_mode)

        r_dsens = _Row("检测灵敏度")
        self._slider_dsens = QSlider(Qt.Horizontal)
        self._slider_dsens.setRange(0, 100)
        self._slider_dsens.setValue(50)
        r_dsens.add_widget(self._slider_dsens)
        sec_vad.add_row(r_dsens)

        r_ptt = _Row("按键绑定")
        self._ptt_lbl = QLabel(self._audio.ptt_key)
        self._ptt_lbl.setStyleSheet(f"color: {palette().get('text_secondary','')}; font-family: 'Consolas',monospace; padding: 6px 14px; background: {palette().get('bg_card_solid','')}; border: 1px solid {palette().get('bg_hover','')}; border-radius: 4px;")
        btn_set = QPushButton("设置")
        btn_set.setObjectName("OutlineBtn")
        btn_set.setCursor(Qt.PointingHandCursor)
        btn_set.clicked.connect(self._on_set_ptt)
        ptt_row = QHBoxLayout()
        ptt_row.setSpacing(8)
        ptt_row.addWidget(self._ptt_lbl)
        ptt_row.addWidget(btn_set)
        r_ptt.add_layout(ptt_row)
        sec_vad.add_row(r_ptt)
        lay.addWidget(sec_vad)

        lay.addWidget(self._divider())

        # Action buttons
        actions = QHBoxLayout()
        actions.setSpacing(12)
        btn_save = QPushButton("保存更改")
        btn_save.setObjectName("PrimaryBtn")
        btn_save.setCursor(Qt.PointingHandCursor)
        btn_save.clicked.connect(self._on_save)
        btn_reset = QPushButton("重置默认")
        btn_reset.setObjectName("GhostBtn")
        btn_reset.setCursor(Qt.PointingHandCursor)
        actions.addWidget(btn_save)
        actions.addWidget(btn_reset)
        actions.addStretch()
        lay.addLayout(actions)

        lay.addStretch()
        scroll.setWidget(host)
        self._stack.addWidget(scroll)

        # Sync input mode radios
        self._sync_mode_radios()
        self._rb_vad.toggled.connect(lambda _: self._on_mode_changed())
        self._rb_ptt.toggled.connect(lambda _: self._on_mode_changed())
        self._rb_cont.toggled.connect(lambda _: self._on_mode_changed())
        self._slider_vol.valueChanged.connect(lambda v: self._vol_lbl.setText(f"{v}%"))

    def _add_toggle_row(self, section: _Section, label: str, checked: bool) -> _Toggle:
        row = _Row(label)
        wrap = QHBoxLayout()
        wrap.setSpacing(8)
        toggle = _Toggle(checked)
        state = QLabel("开启" if checked else "关闭")
        state.setStyleSheet(f"color: {palette().get('text_muted','')}; font-size: 13px;")
        toggle.toggled.connect(lambda c: state.setText("开启" if c else "关闭"))
        wrap.addWidget(toggle)
        wrap.addWidget(state)
        row.add_layout(wrap)
        section.add_row(row)
        return toggle

    def _divider(self) -> QFrame:
        d = QFrame()
        d.setObjectName("Divider")
        d.setFixedHeight(1)
        return d

    def _build_placeholder(self, title: str, text: str):
        w = QWidget()
        w.setStyleSheet(f"background-color: {palette().get('bg_primary','')};")
        lay = QVBoxLayout(w)
        lay.setContentsMargins(32, 32, 32, 32)
        lay.setAlignment(Qt.AlignTop)
        h = QLabel(title)
        h.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 17px; font-weight: 600;")
        lay.addWidget(h)
        body = QLabel(text)
        body.setStyleSheet(f"color: {palette().get('text_secondary','')}; font-size: 14px;")
        body.setWordWrap(True)
        lay.addWidget(body)
        lay.addStretch()
        self._stack.addWidget(w)

    def _build_about_page(self):
        w = QWidget()
        w.setStyleSheet(f"background-color: {palette().get('bg_primary','')};")
        lay = QVBoxLayout(w)
        lay.setContentsMargins(32, 32, 32, 32)
        lay.setAlignment(Qt.AlignTop)
        h = QLabel("关于 NEVO")
        h.setStyleSheet(f"color: {palette().get('text_primary','')}; font-size: 17px; font-weight: 600;")
        lay.addWidget(h)
        for line in [
            "NEVO v2 — 基于 design 设计稿的全新客户端界面。",
            "语音引擎: Opus 编码 + XChaCha20-Poly1305 加密",
            "视频引擎: 多编码协商 (H.264 默认) + UDP 自研协议",
            "© 2026 NEVO",
        ]:
            lbl = QLabel(line)
            lbl.setStyleSheet(f"color: {palette().get('text_secondary','')}; font-size: 14px;")
            lbl.setWordWrap(True)
            lay.addWidget(lbl)
        lay.addStretch()
        self._stack.addWidget(w)

    def _show_section(self, key: str):
        idx = {"account": 1, "audio": 0, "video": 2, "notifications": 3, "hotkeys": 4, "about": 5}.get(key, 0)
        self._stack.setCurrentIndex(idx)
        for k, btn in self._nav_btns.items():
            btn.setChecked(k == key)

    def _refresh_devices(self):
        devs_in, devs_out = [], []
        try:
            import sounddevice as sd
            for i, d in enumerate(sd.query_devices()):
                name = d.get("name", f"Device {i}")
                if d.get("max_input_channels", 0) > 0:
                    devs_in.append(f"{name} (#{i})")
                if d.get("max_output_channels", 0) > 0:
                    devs_out.append(f"{name} (#{i})")
        except Exception:
            pass
        self._combo_mic.clear()
        self._combo_mic.addItems(devs_in or ["默认麦克风"])
        self._combo_spk.clear()
        self._combo_spk.addItems(devs_out or ["默认扬声器"])

    def _sync_mode_radios(self):
        mode = self._audio.input_mode
        self._rb_vad.setChecked(mode == InputMode.VAD)
        self._rb_ptt.setChecked(mode == InputMode.PTT)
        self._rb_cont.setChecked(mode == InputMode.CONTINUOUS)

    def _on_mode_changed(self):
        if self._rb_vad.isChecked():
            self._audio.set_input_mode(InputMode.VAD)
            self.input_mode_changed.emit(InputMode.VAD)
        elif self._rb_ptt.isChecked():
            self._audio.set_input_mode(InputMode.PTT)
            self.input_mode_changed.emit(InputMode.PTT)
        elif self._rb_cont.isChecked():
            self._audio.set_input_mode(InputMode.CONTINUOUS)
            self.input_mode_changed.emit(InputMode.CONTINUOUS)

    def _on_set_ptt(self):
        self._ptt_listening = True
        self.setFocusPolicy(Qt.StrongFocus)
        self.grabKeyboard()
        self._ptt_lbl.setText("按键中…")

    def keyPressEvent(self, e):
        if self._ptt_listening:
            from PyQt5.QtCore import Qt
            key_map = {Qt.Key_Control: "ctrl", Qt.Key_Shift: "shift", Qt.Key_Alt: "alt",
                       Qt.Key_Space: "space", Qt.Key_Return: "enter"}
            name = key_map.get(e.key())
            if name is None:
                name = e.text().strip().lower() if e.text().strip() else None
            if name:
                combo = name if not (e.modifiers() & Qt.ControlModifier) else f"ctrl+{name}"
                self._audio.set_ptt_key(combo)
                self._ptt_lbl.setText(combo)
                self._ptt_listening = False
                self.releaseKeyboard()
            return
        super().keyPressEvent(e)

    def _on_save(self):
        # Persist via audio_manager's settings (already applied live).
        try:
            self._audio.save_settings()
        except Exception:
            pass

    def cleanup(self):
        if self._ptt_listening:
            self.releaseKeyboard()
            self._ptt_listening = False

    def refresh_theme(self):
        self.setStyleSheet(v2_qss())
