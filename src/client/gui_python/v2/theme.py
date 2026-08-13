"""NEVO v2 theme — design tokens, QSS, SVG icons and shared widgets.

All colors are sourced from the existing ThemeManager palette so v2 respects
the user's light/dark preference and switches live with it. The dark palette
matches the design tokens in design/pages/ exactly (#2DD4A8 primary,
#1A1B1E bg-base, #22242A bg-surface, …).
"""

import os
import sys

from PyQt5.QtCore import Qt, QSize, QRectF, pyqtProperty, QEasingCurve, QPropertyAnimation
from PyQt5.QtGui import QIcon, QPixmap, QPainter, QPainterPath, QColor, QFont, QPen, QBrush
from PyQt5.QtWidgets import QFrame, QLabel, QPushButton, QSizePolicy

# Reuse the canonical palette + theme manager from the existing client.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from theme_manager import ThemeManager, DARK_PALETTE, LIGHT_PALETTE


# ───────────────────────── Design tokens (from design/pages/) ─────────────────────────
# Kept as constants for layout math; colors are resolved live via palette().
PRIMARY = "#2DD4A8"
PRIMARY_HOVER = "#26B892"
PRIMARY_ACTIVE = "#1F9C7C"

RADIUS_SM = 4
RADIUS_MD = 8
RADIUS_LG = 12

# Column widths from the design
W_SERVER_BAR = 72
W_CHANNEL_BAR = 240
W_VOICE_PANEL = 280

# Avatar colors used in the design samples
AVATAR_COLORS = [
    "#3B82F6", "#8B5CF6", "#EC4899", "#F59E0B",
    "#10B981", "#EF4444", "#06B6D4", "#F97316",
]


def palette() -> dict:
    """Return the live theme palette (dark or light)."""
    return ThemeManager.instance().palette()


def color(key: str) -> str:
    return palette().get(key, "")


def is_dark() -> bool:
    return ThemeManager.instance().is_dark


def avatar_color(name: str) -> str:
    import hashlib
    h = int(hashlib.md5((name or "?").encode()).hexdigest()[:8], 16)
    return AVATAR_COLORS[h % len(AVATAR_COLORS)]


# ───────────────────────── SVG icons (lucide-style) ─────────────────────────
# Minimal stroke-based icon set matching the design. Each value is the inner
# SVG path/shape markup; render_icon() wraps it into a full SVG document.
_ICONS = {
    "hash": '<line x1="4" y1="9" x2="20" y2="9"/><line x1="4" y1="15" x2="20" y2="15"/><line x1="10" y1="3" x2="8" y2="21"/><line x1="16" y1="3" x2="14" y2="21"/>',
    "volume": '<polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/>',
    "mic": '<path d="M12 2a3 3 0 0 0-3 3v7a3 3 0 0 0 6 0V5a3 3 0 0 0-3-3Z"/><path d="M19 10v2a7 7 0 0 1-14 0v-2"/><line x1="12" y1="19" x2="12" y2="22"/>',
    "mic-off": '<line x1="1" y1="1" x2="23" y2="23"/><path d="M9 9v3a3 3 0 0 0 5.12 2.12M15 9.34V4a3 3 0 0 0-5.94-.6"/><path d="M17 16.95A7 7 0 0 1 5 12v-2m14 0v2c0 .97-.2 1.9-.56 2.74"/><line x1="12" y1="19" x2="12" y2="23"/>',
    "headphones": '<path d="M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-5a9 9 0 0 1 18 0v5a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3"/>',
    "deafen": '<path d="M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-5a9 9 0 0 1 18 0v5a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3"/><line x1="1" y1="1" x2="23" y2="23"/>',
    "settings": '<path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/><circle cx="12" cy="12" r="3"/>',
    "plus": '<line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/>',
    "chevron-down": '<polyline points="6 9 12 15 18 9"/>',
    "search": '<circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/>',
    "inbox": '<path d="M22 12h-6l-2 3h-4l-2-3H2"/><path d="M5.45 5.11L2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 4H7.24a2 2 0 0 0-1.79 1.11z"/>',
    "users": '<path d="M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M22 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/>',
    "send": '<path d="M2.01 21L23 12 2.01 3 2 10l15 2-15 2z"/>',
    "smile": '<circle cx="12" cy="12" r="10"/><path d="M8 14s1.5 2 4 2 4-2 4-2"/><line x1="9" y1="9" x2="9.01" y2="9"/><line x1="15" y1="9" x2="15.01" y2="9"/>',
    "paperclip": '<path d="M21.44 11.05l-9.19 9.19a6 6 0 0 1-8.49-8.49l9.19-9.19a4 4 0 0 1 5.66 5.66l-9.2 9.19a2 2 0 0 1-2.83-2.83l8.49-8.48"/>',
    "phone": '<path d="M22 16.92v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.5 19.5 0 0 1-6-6 19.79 19.79 0 0 1-3.07-8.67A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72 12.84 12.84 0 0 0 .7 2.81 2 2 0 0 1-.45 2.11L8.09 9.91a16 16 0 0 0 6 6l1.27-1.27a2 2 0 0 1 2.11-.45 12.84 12.84 0 0 0 2.81.7A2 2 0 0 1 22 16.92z"/>',
    "video": '<path d="M14.5 4h-5L7 7H4a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2h-3l-2.5-3z"/><circle cx="12" cy="13" r="3"/>',
    "video-off": '<path d="M16 16v1a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V7a2 2 0 0 1 2-2h2m5.66 0H14a2 2 0 0 1 2 2v3.34l1 1L23 7v10"/><line x1="1" y1="1" x2="23" y2="23"/>',
    "monitor": '<rect width="20" height="14" x="2" y="3" rx="2"/><line x1="8" x2="16" y1="21" y2="21"/><line x1="12" x2="12" y1="17" y2="21"/>',
    "phone-off": '<path d="M10.68 13.31a16 16 0 0 0 3.41 2.6l1.27-1.27a2 2 0 0 1 2.11-.45 12.84 12.84 0 0 0 2.81.7A2 2 0 0 1 22 16.92v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.42 19.42 0 0 1-3.33-2.67m-2.67-3.34a19.79 19.79 0 0 1-3.07-8.63A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72 12.84 12.84 0 0 0 .7 2.81 2 2 0 0 1-.45 2.11L8.09 9.91"/><line x1="22" y1="2" x2="2" y2="22"/>',
    "shield": '<rect width="18" height="11" x="3" y="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/>',
    "signal": '<path d="M2 20h.01"/><path d="M7 20v-4"/><path d="M12 20v-8"/><path d="M17 20V8"/>',
    "arrow-left": '<path d="m15 18-6-6 6-6"/>',
    "bell": '<path d="M18 8A6 6 0 0 0 6 8c0 7-3 9-3 9h18s-3-2-3-9"/><path d="M13.73 21a2 2 0 0 1-3.46 0"/>',
    "keyboard": '<rect x="2" y="4" width="20" height="16" rx="2"/><path d="M6 8h.001M10 8h.001M14 8h.001M18 8h.001M8 12h.001M12 12h.001M16 12h.001M7 16h10"/>',
    "info": '<circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/>',
    "user": '<path d="M19 21v-2a4 4 0 0 0-4-4H9a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>',
    "play": '<polygon points="5 3 19 12 5 21 5 3"/>',
    "volume-2": '<polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/>',
    "log-out": '<path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/>',
    "message": '<path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>',
    "check": '<polyline points="20 6 9 17 4 12"/>',
    "x": '<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>',
}


def render_icon(name: str, size: int = 18, color: str = None, stroke: float = 2.0) -> QPixmap:
    """Render a lucide-style stroke icon to a transparent QPixmap."""
    if color is None:
        color = palette().get("text_secondary", "#9CA3B4")
    inner = _ICONS.get(name, "")
    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" '
        f'viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="{stroke}" '
        f'stroke-linecap="round" stroke-linejoin="round">{inner}</svg>'
    )
    from PyQt5.QtSvg import QSvgRenderer
    renderer = QSvgRenderer(svg.encode("utf-8"))
    pix = QPixmap(size, size)
    pix.fill(Qt.transparent)
    p = QPainter(pix)
    p.setRenderHint(QPainter.Antialiasing, True)
    p.setRenderHint(QPainter.SmoothPixmapTransform, True)
    renderer.render(p)
    p.end()
    return pix


def render_icon_qicon(name: str, size: int = 18, color: str = None, stroke: float = 2.0) -> QIcon:
    """Render a lucide-style stroke icon as a QIcon for QPushButton.setIcon()."""
    return QIcon(render_icon(name, size, color, stroke))


# ───────────────────────── QSS ─────────────────────────
def v2_qss() -> str:
    """Full application stylesheet generated from the live palette."""
    p = palette()
    return f"""
    QWidget {{ background: transparent; color: {p['text_primary']}; font-family: 'MiSans','Microsoft YaHei UI','Segoe UI',sans-serif; font-size: 13px; }}

    /* Scrollbars — thin, overlay-style like the design's no-scrollbar */
    QScrollBar:vertical {{ background: transparent; width: 8px; margin: 0; }}
    QScrollBar::handle:vertical {{ background: {p.get('scrollbar_handle','rgba(255,255,255,0.15)')}; border-radius: 4px; min-height: 24px; }}
    QScrollBar::handle:vertical:hover {{ background: {p['text_muted']}; }}
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {{ background: transparent; }}
    QScrollBar:horizontal {{ background: transparent; height: 8px; margin: 0; }}
    QScrollBar::handle:horizontal {{ background: {p.get('scrollbar_handle','rgba(255,255,255,0.15)')}; border-radius: 4px; min-width: 24px; }}

    #ServerBar {{ background-color: {p['bg_primary']}; }}
    #ChannelBar {{ background-color: {p['bg_secondary']}; }}
    #MainArea {{ background-color: {p.get('bg_card_solid','#2A2D35')}; }}
    #VoicePanel {{ background-color: {p['bg_secondary']}; }}
    #ConnectionBar {{ background-color: {p['bg_primary']}; }}

    QLabel {{ background: transparent; }}
    QLabel[role="caption"] {{ color: {p['text_muted']}; font-size: 11px; font-weight: 600; }}
    QLabel[role="secondary"] {{ color: {p['text_secondary']}; }}
    QLabel[role="tertiary"] {{ color: {p['text_muted']}; }}
    QLabel[role="title"] {{ color: {p['text_primary']}; font-weight: 600; }}

    #HeaderBar {{ background-color: {p.get('bg_card_solid','#2A2D35')}; border-bottom: 1px solid {p['bg_hover']}; }}
    #ChannelHeader {{ background-color: {p['bg_secondary']}; border-bottom: 1px solid {p['bg_hover']}; }}

    /* Server icon buttons */
    QPushButton#ServerIcon {{ background-color: {p['bg_secondary']}; border: none; border-radius: {RADIUS_MD}px; color: {p['text_secondary']}; font-weight: 600; }}
    QPushButton#ServerIcon:hover {{ border-radius: {RADIUS_LG}px; background-color: {p['bg_hover']}; }}
    QPushButton#ServerIcon:checked {{ border-radius: {RADIUS_MD}px; background-color: {p['bg_secondary']}; border: 2px solid {p['primary']}; color: {p['text_primary']}; }}
    QPushButton#ServerLogo {{ background-color: {p['primary']}; border: none; border-radius: {RADIUS_MD}px; color: {p.get('bg_primary','#0A1A14')}; font-weight: 700; }}
    QPushButton#ServerLogo:hover {{ border-radius: {RADIUS_LG}px; }}
    QPushButton#AddServer {{ background-color: {p['bg_secondary']}; border: none; border-radius: {RADIUS_MD}px; }}
    QPushButton#AddServer:hover {{ border-radius: {RADIUS_LG}px; background-color: {p['primary']}; }}

    /* Channel rows */
    QFrame#ChannelRow {{ background: transparent; border-radius: {RADIUS_SM}px; }}
    QFrame#ChannelRow:hover {{ background-color: {p['bg_hover']}; }}
    QFrame#ChannelRow[active="true"] {{ background-color: {p['bg_hover']}; }}
    QLabel#ChannelLabel {{ color: {p['text_secondary']}; font-size: 14px; }}
    QFrame#ChannelRow[active="true"] QLabel#ChannelLabel {{ color: {p['text_primary']}; font-weight: 500; }}
    QLabel#CategoryLabel {{ color: {p['text_muted']}; font-size: 11px; font-weight: 600; }}

    /* User rows in channel tree */
    QFrame#UserRow:hover {{ background-color: {p['bg_hover']}; }}

    /* Icon buttons (toolbar) */
    QPushButton#IconBtn {{ background: transparent; border: none; border-radius: {RADIUS_SM}px; }}
    QPushButton#IconBtn:hover {{ background-color: {p['bg_hover']}; }}
    QPushButton#IconBtn:checked {{ background-color: {p['bg_hover']}; }}

    /* Chat input */
    QFrame#ChatInput {{ background-color: {p['bg_secondary']}; border: 1px solid {p['bg_hover']}; border-radius: {RADIUS_MD}px; }}
    QLineEdit#ChatEntry {{ background: transparent; border: none; color: {p['text_primary']}; font-size: 14px; padding: 6px 4px; }}
    QLineEdit#ChatEntry:focus {{ border: none; }}

    /* Inputs / selects */
    QLineEdit, QComboBox, QSpinBox {{ background-color: {p.get('bg_card_solid','#2A2D35')}; border: 1px solid {p['bg_hover']}; border-radius: {RADIUS_SM}px; color: {p['text_primary']}; padding: 6px 10px; }}
    QLineEdit:focus, QComboBox:focus {{ border-color: {p['primary']}; }}
    QComboBox::drop-down {{ border: none; width: 22px; }}
    QComboBox QAbstractItemView {{ background-color: {p.get('bg_card_solid','#2A2D35')}; border: 1px solid {p['bg_hover']}; selection-background-color: {p['bg_hover']}; outline: none; }}

    /* Primary buttons */
    QPushButton#PrimaryBtn {{ background-color: {p['primary']}; color: {p.get('bg_primary','#0A1A14')}; border: none; border-radius: {RADIUS_SM}px; font-weight: 600; padding: 8px 18px; }}
    QPushButton#PrimaryBtn:hover {{ background-color: {PRIMARY_HOVER}; }}
    QPushButton#PrimaryBtn:pressed {{ background-color: {PRIMARY_ACTIVE}; }}
    QPushButton#GhostBtn {{ background: transparent; color: {p['text_secondary']}; border: none; border-radius: {RADIUS_SM}px; padding: 8px 14px; }}
    QPushButton#GhostBtn:hover {{ color: {p['text_primary']}; }}
    QPushButton#OutlineBtn {{ background-color: {p.get('primary_muted','rgba(45,212,168,0.12)')}; color: {p['primary']}; border: 1px solid rgba(45,212,168,0.25); border-radius: {RADIUS_SM}px; padding: 7px 16px; }}
    QPushButton#OutlineBtn:hover {{ background-color: rgba(45,212,168,0.20); }}

    QPushButton#HangupBtn {{ background-color: {p.get('error','#F87171')}; border: none; border-radius: 28px; }}
    QPushButton#HangupBtn:hover {{ background-color: {p.get('error_hover','#D63B3B')}; }}

    /* Sliders */
    QSlider::groove:horizontal {{ height: 4px; background: {p.get('bg_card_solid','#2A2D35')}; border-radius: 2px; }}
    QSlider::sub-page:horizontal {{ background: {p['primary']}; border-radius: 2px; }}
    QSlider::handle:horizontal {{ background: {p['primary']}; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; border: 2px solid {p['bg_primary']}; }}

    /* Toggle switch */
    QFrame#ToggleOn {{ background-color: {p['primary']}; border-radius: 11px; }}
    QFrame#ToggleOff {{ background-color: {p.get('bg_active','#363A45')}; border-radius: 11px; }}

    /* Settings nav */
    QPushButton#SettingsNav {{ background: transparent; border: none; border-radius: {RADIUS_MD}px; color: {p['text_secondary']}; text-align: left; padding: 10px 12px; }}
    QPushButton#SettingsNav:hover {{ background-color: {p['bg_hover']}; color: {p['text_primary']}; }}
    QPushButton#SettingsNav:checked {{ background-color: {p.get('primary_muted','rgba(45,212,168,0.12)')}; color: {p['primary']}; font-weight: 500; }}

    QFrame#Divider {{ background-color: {p['bg_hover']}; max-height: 1px; }}
    QFrame#VDivider {{ background-color: {p['bg_hover']}; max-width: 1px; }}

    QToolTip {{ background-color: {p.get('bg_overlay','#32363F')}; color: {p['text_primary']}; border: 1px solid {p['bg_hover']}; border-radius: 4px; padding: 4px 8px; }}
    """


# ───────────────────────── Shared widgets ─────────────────────────
class Avatar(QLabel):
    """Circular avatar with a colored initial, matching the design."""

    def __init__(self, size: int = 36, parent=None):
        super().__init__(parent)
        self._size = size
        self._initial = "?"
        self._color = AVATAR_COLORS[0]
        self._pixmap = None
        self.setFixedSize(size, size)
        self.setAlignment(Qt.AlignCenter)
        self._render()

    def set_user(self, name: str, pixmap: QPixmap = None):
        self._initial = (name or "?")[0].upper()
        self._color = avatar_color(name or "?")
        self._pixmap = pixmap
        self._render()

    def set_pixmap_avatar(self, pixmap: QPixmap):
        self._pixmap = pixmap
        self._render()

    def _render(self):
        pix = QPixmap(self._size, self._size)
        pix.fill(Qt.transparent)
        p = QPainter(pix)
        p.setRenderHint(QPainter.Antialiasing)
        path = QPainterPath()
        path.addEllipse(0, 0, self._size, self._size)
        p.setClipPath(path)
        if self._pixmap is not None and not self._pixmap.isNull():
            p.drawPixmap(0, 0, self._size, self._size, self._pixmap.scaled(
                self._size, self._size, Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation))
        else:
            p.fillPath(path, QColor(self._color))
            p.setPen(QPen(QColor("white")))
            f = QFont()
            f.setPixelSize(int(self._size * 0.42))
            f.setBold(True)
            p.setFont(f)
            p.drawText(QRectF(0, 0, self._size, self._size), Qt.AlignCenter, self._initial)
        p.end()
        super().setPixmap(pix)


class IconButton(QPushButton):
    """Flat icon button that recolors its icon on hover/theme change."""

    def __init__(self, icon_name: str, size: int = 18, parent=None):
        super().__init__(parent)
        self.setObjectName("IconBtn")
        self._icon_name = icon_name
        self._size = size
        self._color = None
        self.setFixedSize(size + 12, size + 12)
        self.setCursor(Qt.PointingHandCursor)
        self.setCheckable(True)
        self._refresh_icon()
        self.toggled.connect(lambda _: self._refresh_icon())

    def set_color(self, color: str):
        self._color = color
        self._refresh_icon()

    def set_icon(self, icon_name: str):
        self._icon_name = icon_name
        self._refresh_icon()

    def refresh_theme(self):
        self._refresh_icon()

    def _refresh_icon(self):
        col = self._color
        if col is None:
            col = palette().get("text_secondary", "#9CA3B4")
            if self.isChecked():
                col = palette().get("primary", PRIMARY)
        self.setIcon(render_icon_qicon(self._icon_name, self._size, col))
        self.setIconSize(QSize(self._size, self._size))


class VoiceActivityBars(QFrame):
    """Animated voice-activity bars (teal when speaking, dim when idle)."""

    def __init__(self, bar_count: int = 5, parent=None):
        super().__init__(parent)
        self._bars = []
        self._speaking = False
        self._muted = False
        lay = __import__("PyQt5.QtWidgets", fromlist=["QHBoxLayout"]).QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(2)
        for i in range(bar_count):
            bar = QLabel()
            bar.setFixedWidth(3)
            bar.setMinimumHeight(4)
            self._bars.append(bar)
            lay.addWidget(bar)
        self._anim = None
        self._refresh()

    def set_speaking(self, speaking: bool):
        self._speaking = speaking
        self._refresh()

    def set_muted(self, muted: bool):
        self._muted = muted
        self._refresh()

    def refresh_theme(self):
        self._refresh()

    def _refresh(self):
        pal = palette()
        if self._muted:
            col = pal.get("voice_idle", "#3D4250")
            for b in self._bars:
                b.setStyleSheet(f"background-color: {col}; border-radius: 1px; min-height: 4px; max-height: 4px;")
            return
        if self._speaking:
            col = pal.get("voice_active", PRIMARY)
            import secrets
            for b in self._bars:
                h = secrets.randbelow(11) + 8
                b.setStyleSheet(f"background-color: {col}; border-radius: 1px; min-height: {h}px; max-height: {h}px;")
            return
        col = pal.get("voice_idle", "#3D4250")
        for b in self._bars:
            b.setStyleSheet(f"background-color: {col}; border-radius: 1px; min-height: 4px; max-height: 6px;")
