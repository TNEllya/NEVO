from PyQt5.QtCore import Qt, pyqtSignal, QThread
from PyQt5.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QProgressBar, QTextEdit,
)
from PyQt5.QtGui import QFont

from updater import Updater, UpdateState, CheckError, DownloadError, VerifyError
from theme_manager import ThemeManager


class UpdateCheckThread(QThread):
    finished = pyqtSignal(object)
    error = pyqtSignal(str)

    def __init__(self, updater: Updater, parent=None):
        super().__init__(parent)
        self._updater = updater

    def run(self):
        try:
            result = self._updater.check_for_updates(silent=False)
            self.finished.emit(result)
        except CheckError as e:
            self.error.emit(str(e))
        except Exception as e:
            self.error.emit(str(e))


class UpdateDownloadThread(QThread):
    progress = pyqtSignal(float, float, int, int)
    finished = pyqtSignal(object)
    error = pyqtSignal(str)

    def __init__(self, updater: Updater, parent=None):
        super().__init__(parent)
        self._updater = updater

    def run(self):
        self._updater.set_callbacks(on_progress=self._on_progress)
        try:
            path = self._updater.download_update()
            self.finished.emit(path)
        except (DownloadError, VerifyError) as e:
            self.error.emit(str(e))
        except Exception as e:
            self.error.emit(str(e))

    def _on_progress(self, percent, speed, downloaded, total):
        self.progress.emit(percent, speed, downloaded, total)


class UpdateDialog(QDialog):
    update_accepted = pyqtSignal()
    update_declined = pyqtSignal()

    def __init__(self, updater: Updater, parent=None):
        super().__init__(parent)
        self._updater = updater
        self._check_thread = None
        self._download_thread = None
        self._downloaded_file = None
        self._setup_ui()
        self._apply_theme()
        tm = ThemeManager.instance()
        tm.theme_changed.connect(self._on_theme_changed)

    def _setup_ui(self):
        self.setWindowTitle(self.tr("NEVO - Update"))
        self.setMinimumSize(500, 420)
        self.setMaximumSize(620, 560)

        layout = QVBoxLayout(self)
        layout.setSpacing(12)
        layout.setContentsMargins(24, 20, 24, 20)

        self._title_label = QLabel(self.tr("Checking for updates..."))
        self._title_label.setFont(QFont("Segoe UI", 14, QFont.Bold))
        layout.addWidget(self._title_label)

        self._version_label = QLabel("")
        self._version_label.setWordWrap(True)
        self._version_label.setFont(QFont("Segoe UI", 10))
        layout.addWidget(self._version_label)

        self._changelog_text = QTextEdit()
        self._changelog_text.setReadOnly(True)
        self._changelog_text.setMaximumHeight(150)
        self._changelog_text.setFont(QFont("Segoe UI", 9))
        self._changelog_text.setVisible(False)
        layout.addWidget(self._changelog_text)

        self._progress_bar = QProgressBar()
        self._progress_bar.setRange(0, 100)
        self._progress_bar.setValue(0)
        self._progress_bar.setFixedHeight(8)
        self._progress_bar.setTextVisible(True)
        self._progress_bar.setFont(QFont("Segoe UI", 8))
        self._progress_bar.setVisible(False)
        layout.addWidget(self._progress_bar)

        self._status_label = QLabel("")
        self._status_label.setAlignment(Qt.AlignCenter)
        self._status_label.setWordWrap(True)
        self._status_label.setFont(QFont("Segoe UI", 9))
        layout.addWidget(self._status_label)

        layout.addStretch()

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()

        self._check_btn = QPushButton(self.tr("Check for Updates"))
        self._check_btn.setFont(QFont("Segoe UI", 9))
        self._check_btn.clicked.connect(self._on_check_clicked)
        btn_layout.addWidget(self._check_btn)

        self._download_btn = QPushButton(self.tr("Download Update"))
        self._download_btn.setFont(QFont("Segoe UI", 9))
        self._download_btn.clicked.connect(self._on_download_clicked)
        self._download_btn.setVisible(False)
        btn_layout.addWidget(self._download_btn)

        self._install_btn = QPushButton(self.tr("Install & Restart"))
        self._install_btn.setFont(QFont("Segoe UI", 9))
        self._install_btn.clicked.connect(self._on_install_clicked)
        self._install_btn.setVisible(False)
        btn_layout.addWidget(self._install_btn)

        self._cancel_btn = QPushButton(self.tr("Cancel"))
        self._cancel_btn.setFont(QFont("Segoe UI", 9))
        self._cancel_btn.clicked.connect(self._on_cancel_clicked)
        self._cancel_btn.setVisible(False)
        btn_layout.addWidget(self._cancel_btn)

        self._close_btn = QPushButton(self.tr("Close"))
        self._close_btn.setFont(QFont("Segoe UI", 9))
        self._close_btn.clicked.connect(self.close)
        btn_layout.addWidget(self._close_btn)

        layout.addLayout(btn_layout)

    def _apply_theme(self):
        tm = ThemeManager.instance()
        pal = tm.palette()

        self.setStyleSheet(
            f"UpdateDialog {{"
            f"  background-color: {pal['bg_primary']};"
            f"  border: 1px solid {pal['bg_hover']};"
            f"  border-radius: 12px;"
            f"}}"
        )

        self._title_label.setStyleSheet(
            f"color: {pal['text_primary']}; background: transparent; border: none;"
        )
        self._version_label.setStyleSheet(
            f"color: {pal['text_secondary']}; background: transparent; border: none;"
        )
        self._status_label.setStyleSheet(
            f"color: {pal['text_secondary']}; background: transparent; border: none;"
        )

        self._changelog_text.setStyleSheet(
            f"QTextEdit {{"
            f"  background-color: {pal['bg_inner_card']};"
            f"  color: {pal['text_secondary']};"
            f"  border: 1px solid {pal['bg_hover']};"
            f"  border-radius: 8px;"
            f"  padding: 8px;"
            f"  font-family: 'Segoe UI';"
            f"}}"
        )

        self._progress_bar.setStyleSheet(
            f"QProgressBar {{"
            f"  background-color: {pal['bg_inner_card']};"
            f"  border: none;"
            f"  border-radius: 4px;"
            f"  text-align: center;"
            f"  color: {pal['text_primary']};"
            f"  font-family: 'Segoe UI';"
            f"}}"
            f"QProgressBar::chunk {{"
            f"  background-color: {pal['text_accent']};"
            f"  border-radius: 4px;"
            f"}}"
        )

        btn_active_style = (
            f"QPushButton {{"
            f"  background-color: {pal['bg_input']};"
            f"  color: {pal['text_primary']};"
            f"  border: none;"
            f"  border-radius: 6px;"
            f"  padding: 8px 20px;"
            f"  font-family: 'Segoe UI';"
            f"}}"
            f"QPushButton:hover {{"
            f"  background-color: {pal['bg_input_hover']};"
            f"}}"
            f"QPushButton:pressed {{"
            f"  background-color: {pal['bg_input_pressed']};"
            f"}}"
            f"QPushButton:disabled {{"
            f"  background-color: {pal['bg_input']};"
            f"  color: {pal['text_muted']};"
            f"}}"
        )

        for btn in [self._check_btn, self._download_btn, self._install_btn,
                     self._cancel_btn, self._close_btn]:
            btn.setStyleSheet(btn_active_style)

    def _on_theme_changed(self, _):
        self._apply_theme()

    def _set_state_idle(self):
        self._title_label.setText(self.tr("No updates available"))
        self._version_label.setText(
            self.tr("Current version: %s") % self._updater.current_version)
        self._progress_bar.setVisible(False)
        self._changelog_text.setVisible(False)
        self._status_label.setText("")
        self._check_btn.setVisible(True)
        self._download_btn.setVisible(False)
        self._install_btn.setVisible(False)
        self._cancel_btn.setVisible(False)
        self._close_btn.setVisible(True)

    def _set_state_checking(self):
        self._title_label.setText(self.tr("Checking for updates..."))
        self._version_label.setText("")
        self._progress_bar.setVisible(False)
        self._changelog_text.setVisible(False)
        self._status_label.setText(self.tr("Connecting to GitHub..."))
        self._check_btn.setEnabled(False)
        self._download_btn.setVisible(False)
        self._install_btn.setVisible(False)
        self._cancel_btn.setVisible(False)
        self._close_btn.setVisible(True)

    def _set_state_available(self):
        info = self._updater.latest_info
        if not info:
            return
        self._title_label.setText(self.tr("Update available!"))
        self._version_label.setText(
            self.tr("Current: v%s  →  Latest: v%s") % (
                self._updater.current_version, info.version))
        if info.changelog:
            self._changelog_text.setVisible(True)
            self._changelog_text.setMarkdown(info.changelog)
        self._status_label.setText("")
        self._check_btn.setVisible(False)
        self._download_btn.setVisible(True)
        self._install_btn.setVisible(False)
        self._cancel_btn.setVisible(False)
        self._close_btn.setVisible(True)

    def _set_state_downloading(self):
        self._title_label.setText(self.tr("Downloading update..."))
        self._progress_bar.setVisible(True)
        self._progress_bar.setValue(0)
        self._check_btn.setVisible(False)
        self._download_btn.setVisible(False)
        self._install_btn.setVisible(False)
        self._cancel_btn.setVisible(True)
        self._close_btn.setEnabled(False)

    def _set_state_ready(self):
        self._title_label.setText(self.tr("Update ready to install"))
        self._progress_bar.setValue(100)
        self._status_label.setText(
            self.tr("Download complete. Click 'Install & Restart' to apply."))
        self._check_btn.setVisible(False)
        self._download_btn.setVisible(False)
        self._install_btn.setVisible(True)
        self._cancel_btn.setVisible(False)
        self._close_btn.setEnabled(True)

    def _set_state_error(self, message: str):
        self._title_label.setText(self.tr("Update failed"))
        self._version_label.setText(self.tr("Error: %s") % message)
        self._progress_bar.setVisible(False)
        self._changelog_text.setVisible(False)
        self._status_label.setText("")
        self._check_btn.setVisible(True)
        self._check_btn.setEnabled(True)
        self._download_btn.setVisible(False)
        self._install_btn.setVisible(False)
        self._cancel_btn.setVisible(False)
        self._close_btn.setEnabled(True)

    def _on_check_clicked(self):
        self._set_state_checking()
        self._check_thread = UpdateCheckThread(self._updater, self)
        self._check_thread.finished.connect(self._on_check_finished)
        self._check_thread.error.connect(self._on_check_error)
        self._check_thread.start()

    def _on_check_finished(self, result):
        self._check_btn.setEnabled(True)
        if result:
            self._set_state_available()
        else:
            self._set_state_idle()

    def _on_check_error(self, error_msg: str):
        self._set_state_error(error_msg)

    def _on_download_clicked(self):
        self._set_state_downloading()
        self._download_thread = UpdateDownloadThread(self._updater, self)
        self._download_thread.progress.connect(self._on_download_progress)
        self._download_thread.finished.connect(self._on_download_finished)
        self._download_thread.error.connect(self._on_download_error)
        self._download_thread.start()

    def _on_download_progress(self, percent, speed, downloaded, total):
        self._progress_bar.setValue(int(percent))
        if speed > 1024 * 1024:
            speed_str = self.tr("%.1f MB/s") % (speed / (1024 * 1024))
        elif speed > 1024:
            speed_str = self.tr("%.1f KB/s") % (speed / 1024)
        else:
            speed_str = self.tr("%.0f B/s") % speed

        if total > 0:
            dl_mb = downloaded / (1024 * 1024)
            total_mb = total / (1024 * 1024)
            self._status_label.setText(
                self.tr("%.1f / %.1f MB  |  %s") % (dl_mb, total_mb, speed_str))
        else:
            self._status_label.setText(speed_str)

    def _on_download_finished(self, path):
        self._downloaded_file = path
        self._set_state_ready()

    def _on_download_error(self, error_msg: str):
        self._set_state_error(error_msg)

    def _on_install_clicked(self):
        if self._downloaded_file:
            try:
                self._updater.install_update(self._downloaded_file)
            except Exception as e:
                self._set_state_error(str(e))

    def _on_cancel_clicked(self):
        self._updater.cancel_download()
        if self._updater.latest_info:
            self._set_state_available()
        else:
            self._set_state_idle()
        self._close_btn.setEnabled(True)

    def check_on_open(self):
        self._on_check_clicked()

    def closeEvent(self, event):
        if self._updater.state == UpdateState.DOWNLOADING:
            self._updater.cancel_download()
        event.accept()