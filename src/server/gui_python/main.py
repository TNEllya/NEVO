"""NEVO Server Manager - Entry point.

A PyQt-Fluent-Widgets GUI for managing the NEVO VoIP server
via the ControlServer JSON-over-TCP IPC protocol.
"""

import sys
import os

# Add the parent directory to the path so imports work
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Import qfluentwidgets fully to ensure Qt resources are registered
import qfluentwidgets  # noqa: F401
from qfluentwidgets._rc.resource import qInitResources
qInitResources()

from PyQt5.QtWidgets import QApplication
from PyQt5.QtCore import Qt
from qfluentwidgets import setTheme, Theme

from main_window import MainWindow


def main():
    # Enable high DPI scaling
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling)
    QApplication.setAttribute(Qt.AA_UseHighDpiPixmaps)

    app = QApplication(sys.argv)
    app.setApplicationName("NEVO Server Manager")
    app.setOrganizationName("NEVO")

    # Set dark theme
    setTheme(Theme.DARK)

    window = MainWindow()
    window.show()

    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
