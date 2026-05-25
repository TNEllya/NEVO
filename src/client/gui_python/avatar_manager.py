import os
from typing import Optional

from PyQt5.QtCore import Qt, QObject, pyqtSignal
from PyQt5.QtGui import QPixmap, QPainter, QPainterPath, QImage


_AVATAR_DIR = os.path.join(os.path.expanduser("~"), ".nevo")
_AVATAR_FILE = os.path.join(_AVATAR_DIR, "avatar.png")


class AvatarManager(QObject):
    avatar_changed = pyqtSignal()

    def __init__(self):
        super().__init__()
        self._pixmap: Optional[QPixmap] = None
        self._load_avatar()

    @property
    def has_avatar(self) -> bool:
        return self._pixmap is not None

    def get_pixmap(self, size: int) -> QPixmap:
        if self._pixmap is not None:
            return self._make_round(self._pixmap.scaled(size, size,
                Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation), size)
        return QPixmap()

    def get_raw(self) -> Optional[QPixmap]:
        return self._pixmap

    def set_avatar_from_path(self, file_path: str) -> bool:
        try:
            img = QImage(file_path)
            if img.isNull():
                return False
            img = img.convertToFormat(QImage.Format_ARGB32)
            side = min(img.width(), img.height())
            x = (img.width() - side) // 2
            y = (img.height() - side) // 2
            img = img.copy(x, y, side, side)
            pixmap = QPixmap.fromImage(img)
            os.makedirs(_AVATAR_DIR, exist_ok=True)
            pixmap.save(_AVATAR_FILE, "PNG")
            self._pixmap = pixmap
            self.avatar_changed.emit()
            return True
        except Exception:
            return False

    def remove_avatar(self):
        self._pixmap = None
        try:
            if os.path.exists(_AVATAR_FILE):
                os.remove(_AVATAR_FILE)
        except Exception:
            pass
        self.avatar_changed.emit()

    def _load_avatar(self):
        if os.path.exists(_AVATAR_FILE):
            pix = QPixmap(_AVATAR_FILE)
            if not pix.isNull():
                self._pixmap = pix

    @staticmethod
    def _make_round(pixmap: QPixmap, diameter: int) -> QPixmap:
        result = QPixmap(diameter, diameter)
        result.fill(Qt.transparent)
        p = QPainter(result)
        p.setRenderHint(QPainter.Antialiasing)
        path = QPainterPath()
        path.addEllipse(0, 0, diameter, diameter)
        p.setClipPath(path)
        target = pixmap.scaled(diameter, diameter, Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
        dx = (diameter - target.width()) // 2
        dy = (diameter - target.height()) // 2
        p.drawPixmap(dx, dy, target)
        p.end()
        return result
