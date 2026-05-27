"""NEVO Client - Entry point.

A PyQt-Fluent-Widgets GUI client for the NEVO VoIP server.
Connects via the TCP protobuf control protocol.
"""

import sys
import os
import traceback
import logging
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# 配置日志文件
_LOG_DIR = os.getcwd()
_LOG_FILE = os.path.join(_LOG_DIR, "nevo_client.log")

# 清除旧日志文件（每次启动创建新日志）
try:
    with open(_LOG_FILE, "w", encoding="utf-8") as f:
        f.write(f"=== NEVO Client Log Started at {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} ===\n\n")
except Exception:
    pass

# 配置日志记录器
logger = logging.getLogger("nevo_client")
logger.setLevel(logging.DEBUG)

# 文件处理器
file_handler = logging.FileHandler(_LOG_FILE, mode="a", encoding="utf-8")
file_handler.setLevel(logging.DEBUG)
file_formatter = logging.Formatter("%(asctime)s [%(levelname)s] %(message)s", datefmt="%H:%M:%S")
file_handler.setFormatter(file_formatter)
logger.addHandler(file_handler)

# 控制台处理器
console_handler = logging.StreamHandler(sys.stdout)
console_handler.setLevel(logging.DEBUG)
console_formatter = logging.Formatter("[%(levelname)s] %(message)s")
console_handler.setFormatter(console_formatter)
logger.addHandler(console_handler)

# 重定向 stdout 和 stderr 到日志
class TeeStream:
    """同时将输出写入原始流和日志文件"""
    def __init__(self, original_stream, log_filename):
        self.original_stream = original_stream
        self.log_filename = log_filename
        self._buffer = ""
    
    def write(self, text):
        self.original_stream.write(text)
        try:
            self._buffer += text
            for line in text.splitlines(True):
                if line.endswith('\n'):
                    if line.strip():
                        with open(self.log_filename, "a", encoding="utf-8") as f:
                            f.write(line)
        except Exception:
            pass
        return len(text)
    
    def flush(self):
        self.original_stream.flush()


_CRASH_LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "crash.log")


def _global_exception_hook(exc_type, exc_value, exc_tb):
    msg = "".join(traceback.format_exception(exc_type, exc_value, exc_tb))
    print(f"[FATAL] Unhandled exception:\n{msg}")
    logger.critical(f"Unhandled exception:\n{msg}")
    try:
        with open(_CRASH_LOG, "a", encoding="utf-8") as f:
            f.write(f"=== CRASH ===\n{msg}\n\n")
    except Exception:
        pass
    sys.__excepthook__(exc_type, exc_value, exc_tb)


sys.excepthook = _global_exception_hook

import qfluentwidgets  # noqa: F401
from qfluentwidgets._rc.resource import qInitResources
qInitResources()

from PyQt5.QtWidgets import QApplication
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont, QFontDatabase
from qfluentwidgets import setTheme, Theme

from theme_manager import ThemeManager
from main_window import MainWindow


def _resource_path(relative_path):
    if getattr(sys, 'frozen', False):
        return os.path.join(sys._MEIPASS, relative_path)
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), relative_path)


def _load_font(app: QApplication):
    font_path = _resource_path("resources/MiSans-Regular.otf")
    if os.path.exists(font_path):
        font_id = QFontDatabase.addApplicationFont(font_path)
        if font_id != -1:
            families = QFontDatabase.applicationFontFamilies(font_id)
            if families:
                font = QFont(families[0])
                font.setPointSize(10)
                app.setFont(font)
                return

    if sys.platform == "darwin":
        fallback_family = "SF Pro Text"
        if not any(f for f in QFontDatabase().families() if f == fallback_family):
            fallback_family = "Helvetica Neue"
    elif sys.platform == "win32":
        fallback_family = "Microsoft YaHei UI"
    else:
        fallback_family = "Noto Sans CJK SC"

    font = QFont(fallback_family)
    font.setPointSize(10)
    app.setFont(font)


def main():
    # 重定向 stdout 和 stderr 到日志
    sys.stdout = TeeStream(sys.stdout, _LOG_FILE)
    sys.stderr = TeeStream(sys.stderr, _LOG_FILE)
    
    logger.info("=" * 50)
    logger.info("NEVO Client Starting")
    logger.info(f"Log file: {_LOG_FILE}")
    logger.info(f"Python version: {sys.version}")
    logger.info(f"Working directory: {os.getcwd()}")
    logger.info("=" * 50)
    
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling)
    QApplication.setAttribute(Qt.AA_UseHighDpiPixmaps)

    app = QApplication(sys.argv)
    app.setApplicationName("NEVO")
    app.setOrganizationName("NEVO")

    _load_font(app)

    tm = ThemeManager.instance()
    tm.load_preference()

    logger.info("Creating MainWindow...")
    window = MainWindow()
    window.show()
    logger.info("MainWindow shown, entering event loop...")

    exit_code = app.exec_()
    logger.info(f"Application exiting with code: {exit_code}")
    logger.info("=" * 50)
    logger.info("NEVO Client Stopped")
    logger.info("=" * 50)
    
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
