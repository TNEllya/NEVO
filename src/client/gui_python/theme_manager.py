import sys
import logging
from PyQt5.QtCore import QObject, pyqtSignal, QTimer, QSettings
from PyQt5.QtGui import QColor
from qfluentwidgets import setTheme, Theme, isDarkTheme

logger = logging.getLogger("nevo.theme")

THEME_DARK = "dark"
THEME_LIGHT = "light"
THEME_SYSTEM = "system"

APP_NAME = "NEVO"

DARK_PALETTE = {
    "bg_primary": "#2b2d31",
    "bg_secondary": "#1e1e2e",
    "bg_card": "rgba(43, 45, 49, 0.85)",
    "bg_card_solid": "#2b2d31",
    "bg_input": "rgba(255, 255, 255, 0.05)",
    "bg_input_hover": "rgba(255, 255, 255, 0.08)",
    "bg_input_pressed": "rgba(255, 255, 255, 0.12)",
    "bg_overlay": "rgba(0, 0, 0, 0.2)",
    "bg_hover": "rgba(79, 84, 92, 0.4)",
    "bg_inner_card": "#383a45",
    "bg_status": "rgba(255, 255, 255, 0.05)",
    "text_primary": "#dbdee1",
    "text_secondary": "#8b8d97",
    "text_muted": "#6d6f78",
    "text_timestamp": "#72767d",
    "text_system_msg": "#72767d",
    "text_white": "#ffffff",
    "text_accent": "#43b581",
    "text_warning": "#faa61a",
    "border_dashed": "#666",
    "border_avatar": "#ccc",
    "bg_avatar_empty": "#f0f0f0",
    "scrollbar_handle": "rgba(255,255,255,0.2)",
    "status_online": "#2ecc71",
    "status_offline": "#e74c3c",
    "star_color": "#f1c40f",
    "chat_bg": "#313338",
}

LIGHT_PALETTE = {
    "bg_primary": "#f2f3f5",
    "bg_secondary": "#ffffff",
    "bg_card": "rgba(255, 255, 255, 0.85)",
    "bg_card_solid": "#ffffff",
    "bg_input": "rgba(0, 0, 0, 0.04)",
    "bg_input_hover": "rgba(0, 0, 0, 0.06)",
    "bg_input_pressed": "rgba(0, 0, 0, 0.08)",
    "bg_overlay": "rgba(0, 0, 0, 0.06)",
    "bg_hover": "rgba(0, 0, 0, 0.04)",
    "bg_inner_card": "#ebedef",
    "bg_status": "rgba(0, 0, 0, 0.03)",
    "text_primary": "#1e1f22",
    "text_secondary": "#6d6f78",
    "text_muted": "#8e9297",
    "text_timestamp": "#a0a0a0",
    "text_system_msg": "#a0a0a0",
    "text_white": "#1e1f22",
    "text_accent": "#2d8f5e",
    "text_warning": "#c4850c",
    "border_dashed": "#bbb",
    "border_avatar": "#ddd",
    "bg_avatar_empty": "#e8e8e8",
    "scrollbar_handle": "rgba(0,0,0,0.15)",
    "status_online": "#2ecc71",
    "status_offline": "#e74c3c",
    "star_color": "#f1c40f",
    "chat_bg": "#e3e5e8",
}


def _is_windows_dark_mode() -> bool:
    if sys.platform != "win32":
        return False
    try:
        import winreg
        key = winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
        )
        value, _ = winreg.QueryValueEx(key, "AppsUseLightTheme")
        winreg.CloseKey(key)
        return value == 0
    except Exception:
        return False


def _is_macos_dark_mode() -> bool:
    if sys.platform != "darwin":
        return False
    try:
        import subprocess
        result = subprocess.run(
            ["defaults", "read", "-g", "AppleInterfaceStyle"],
            capture_output=True, text=True, timeout=2,
        )
        return result.stdout.strip().lower() == "dark"
    except Exception:
        return False


def _is_linux_dark_mode() -> bool:
    if sys.platform != "linux":
        return False
    try:
        import subprocess
        result = subprocess.run(
            ["gsettings", "get", "org.gnome.desktop.interface", "color-scheme"],
            capture_output=True, text=True, timeout=2,
        )
        return "dark" in result.stdout.strip().lower()
    except Exception:
        return False


def is_system_dark() -> bool:
    if sys.platform == "win32":
        return _is_windows_dark_mode()
    elif sys.platform == "darwin":
        return _is_macos_dark_mode()
    elif sys.platform == "linux":
        return _is_linux_dark_mode()
    return False


class ThemeManager(QObject):
    theme_changed = pyqtSignal(bool)

    _instance = None

    def __init__(self, parent=None):
        super().__init__(parent)
        self._mode = THEME_DARK
        self._system_watcher = QTimer(self)
        self._system_watcher.setInterval(3000)
        self._system_watcher.timeout.connect(self._check_system_theme)
        self._last_system_dark = is_system_dark()

    @staticmethod
    def instance() -> "ThemeManager":
        if ThemeManager._instance is None:
            ThemeManager._instance = ThemeManager()
        return ThemeManager._instance

    @property
    def mode(self) -> str:
        return self._mode

    @property
    def is_dark(self) -> bool:
        if self._mode == THEME_SYSTEM:
            return is_system_dark()
        return self._mode == THEME_DARK

    def palette(self) -> dict:
        return DARK_PALETTE if self.is_dark else LIGHT_PALETTE

    def color(self, key: str) -> str:
        pal = self.palette()
        return pal.get(key, "")

    def set_mode(self, mode: str):
        if mode not in (THEME_DARK, THEME_LIGHT, THEME_SYSTEM):
            mode = THEME_DARK
        old_dark = self.is_dark
        self._mode = mode
        self._apply()
        new_dark = self.is_dark
        self._save_preference(mode)
        if old_dark != new_dark:
            self.theme_changed.emit(new_dark)
        if mode == THEME_SYSTEM:
            self._system_watcher.start()
        else:
            self._system_watcher.stop()

    def _apply(self):
        if self.is_dark:
            setTheme(Theme.DARK)
        else:
            setTheme(Theme.LIGHT)

    def _check_system_theme(self):
        current = is_system_dark()
        if current != self._last_system_dark:
            self._last_system_dark = current
            old_dark = not current
            self._apply()
            self.theme_changed.emit(current)

    def load_preference(self) -> str:
        settings = QSettings(APP_NAME, APP_NAME)
        mode = settings.value("theme/mode", THEME_DARK, type=str)
        self._mode = mode
        self._apply()
        if mode == THEME_SYSTEM:
            self._system_watcher.start()
        return mode

    def _save_preference(self, mode: str):
        settings = QSettings(APP_NAME, APP_NAME)
        settings.setValue("theme/mode", mode)

    def stop(self):
        self._system_watcher.stop()


def card_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return (
        f"HeaderCardWidget {{ background-color: {pal['bg_card']}; border: none; border-radius: 12px; }}"
        f"#headerView {{ background-color: transparent; border-top-left-radius: 12px; border-top-right-radius: 12px; }}"
        f"#headerLabel {{ color: {pal['text_white']}; }}"
        f"#view {{ background-color: transparent; border-bottom-left-radius: 12px; border-bottom-right-radius: 12px; }}"
    )


def scroll_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return (
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"
        f"QScrollBar:vertical {{ background: transparent; width: 8px; margin: 0; }}"
        f"QScrollBar::handle:vertical {{ background: {pal['scrollbar_handle']}; border-radius: 4px; min-height: 20px; }}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    )


def input_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return (
        f"QFrame {{ background-color: {pal['bg_input']}; border-radius: 8px; }}"
        f"QFrame:hover {{ background-color: {pal['bg_input_hover']}; }}"
        f"QLineEdit {{ background-color: {pal['bg_input']}; border: none; border-radius: 8px; "
        f"color: {pal['text_primary']}; padding: 6px 12px; }}"
        f"QLineEdit:hover {{ background-color: {pal['bg_input_hover']}; }}"
        f"QLineEdit:focus {{ background-color: {pal['bg_input_pressed']}; }}"
        f"QSpinBox {{ background-color: {pal['bg_input']}; border: none; border-radius: 8px; "
        f"color: {pal['text_primary']}; padding: 4px 8px; }}"
    )


def button_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return (
        f"QPushButton {{ background-color: {pal['bg_input']}; border: none; border-radius: 6px; "
        f"color: {pal['text_primary']}; padding: 6px 14px; }}"
        f"QPushButton:hover {{ background-color: {pal['bg_input_hover']}; }}"
        f"QPushButton:pressed {{ background-color: {pal['bg_input_pressed']}; }}"
        f"QPushButton:disabled {{ background-color: {pal['bg_input']}; color: {pal['text_muted']}; }}"
    )


def status_dot_stylesheet(online: bool) -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    color = pal["status_online"] if online else pal["status_offline"]
    return f"color: {color}; font-size: 16px;"


def channel_container_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return f"background-color: {pal['bg_primary']};"


def inner_card_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return f"QFrame {{ background-color: {pal['bg_inner_card']}; border-radius: 8px; }}"


def chat_bubble_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return (
        f"QFrame {{ background-color: {pal['chat_bg']}; border-radius: 8px; "
        f"color: {pal['text_primary']}; }}"
    )


def system_msg_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return f"font-size: 12px; color: {pal['text_system_msg']}; font-style: italic; background: transparent;"


def timestamp_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return f"font-size: 11px; color: {pal['text_timestamp']}; background: transparent;"


def video_area_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return f"background-color: {pal['bg_primary']}; border: none;"


def overlay_stylesheet() -> str:
    tm = ThemeManager.instance()
    pal = tm.palette()
    return f"background-color: {pal['bg_overlay']}; border: none;"
