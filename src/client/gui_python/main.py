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

# 统一日志引导（日志目录/文件、TeeStream 重定向、全局异常钩子）
from logging_setup import setup_client_logging  # noqa: E402
logger = setup_client_logging("v1")

import qfluentwidgets  # noqa: F401
from qfluentwidgets._rc.resource import qInitResources
qInitResources()

from PyQt5.QtWidgets import QApplication
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont, QFontDatabase, QIcon
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

    icon_path = _resource_path("resources/nevo_icon.ico")
    if os.path.exists(icon_path):
        app.setWindowIcon(QIcon(icon_path))

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
