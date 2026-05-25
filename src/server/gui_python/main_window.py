"""Main window with Fluent Design navigation sidebar."""

import os
import sys

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import QHBoxLayout, QVBoxLayout, QFrame, QLabel
from qfluentwidgets import (
    FluentWindow, NavigationAvatarWidget, NavigationItemPosition,
    FluentIcon, InfoBar, InfoBarPosition, SubtitleLabel,
    CaptionLabel, PushButton, PrimaryPushButton, LineEdit,
    SpinBox, HeaderCardWidget, StrongBodyLabel, RoundMenu, Action,
)

from server_process import ServerProcess
from views.dashboard import DashboardView
from views.sessions import SessionsView
from views.channels import ChannelsView
from views.config import ConfigView
from views.log_view import LogView
import i18n

APP_NAME = "NEVO Server Manager"


class MainWindow(FluentWindow):
    """Main application window with Fluent Design."""

    def __init__(self):
        super().__init__()

        self.server = ServerProcess()

        # Initialize translations - support both source and PyInstaller bundle
        if getattr(sys, 'frozen', False):
            base_dir = sys._MEIPASS
        else:
            base_dir = os.path.dirname(os.path.abspath(__file__))
        translations_dir = os.path.join(base_dir, "translations")
        i18n.init_translations(translations_dir)
        saved_lang = i18n.load_language_preference(APP_NAME)
        i18n.load_language(saved_lang)

        self._setup_ui()
        self._setup_navigation()

        self.resize(1200, 800)
        self.setMinimumSize(900, 600)

    def _setup_ui(self):
        self.setWindowTitle(APP_NAME)

        # Create views
        self.dashboard_view = DashboardView(self.server)
        self.sessions_view = SessionsView(self.server)
        self.channels_view = ChannelsView(self.server)
        self.config_view = ConfigView(self.server)
        self.log_view = LogView(self.server)

    def _setup_navigation(self):
        # Add navigation items
        self.addSubInterface(
            self.dashboard_view,
            FluentIcon.HOME,
            self.tr("Dashboard"),
            NavigationItemPosition.TOP,
        )
        self.addSubInterface(
            self.sessions_view,
            FluentIcon.PEOPLE,
            self.tr("Sessions"),
            NavigationItemPosition.TOP,
        )
        self.addSubInterface(
            self.channels_view,
            FluentIcon.FOLDER,
            self.tr("Channels"),
            NavigationItemPosition.TOP,
        )
        self.addSubInterface(
            self.config_view,
            FluentIcon.SETTING,
            self.tr("Configuration"),
            NavigationItemPosition.TOP,
        )
        self.addSubInterface(
            self.log_view,
            FluentIcon.DOCUMENT,
            self.tr("Log"),
            NavigationItemPosition.BOTTOM,
        )

        # Language switcher at bottom
        self.navigationInterface.addSeparator()
        self.navigationInterface.addItem(
            "language",
            FluentIcon.LANGUAGE,
            self.tr("Language"),
            onClick=self._show_language_menu,
            position=NavigationItemPosition.BOTTOM,
            selectable=False,
        )

        # Navigation avatar
        avatar = NavigationAvatarWidget("NEVO", "")
        self.navigationInterface.addWidget(
            "avatar", avatar, onClick=self._on_avatar_click,
            position=NavigationItemPosition.BOTTOM,
        )

    def _show_language_menu(self):
        """Show language selection menu."""
        menu = RoundMenu(parent=self)

        for lang in i18n.available_languages():
            display = i18n.language_display_name(lang)
            action = Action(display, triggered=lambda checked, l=lang: self._change_language(l))
            if lang == i18n.current_language():
                action.setEnabled(False)
            menu.addAction(action)

        # Position near the language nav item
        menu.exec_(self.mapToGlobal(self.rect().bottomLeft()))

    def _change_language(self, lang: str):
        """Switch language and restart the UI."""
        i18n.load_language(lang)
        i18n.save_language_preference(lang, APP_NAME)

        InfoBar.info(
            self.tr("Language"),
            self.tr("Language changed. Please restart the application for full effect."),
            parent=self,
            position=InfoBarPosition.TOP,
            duration=3000,
        )

    def _on_avatar_click(self):
        InfoBar.info(
            self.tr("About"),
            self.tr("NEVO Server Manager\nA management GUI for the NEVO VoIP server."),
            parent=self,
            position=InfoBarPosition.TOP,
            duration=4000,
        )

    def closeEvent(self, event):
        """Clean up on close - stop server if running."""
        self.dashboard_view.stop()
        self.sessions_view.stop()
        self.channels_view.stop()
        if self.server.server_running:
            self.server.stop_server()
        super().closeEvent(event)
