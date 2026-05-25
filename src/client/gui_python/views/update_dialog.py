from PyQt5.QtCore import Qt, pyqtSignal, QThread
from PyQt5.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QProgressBar, QTextEdit, QSizePolicy,
)
from PyQt5.QtGui import QFont

from updater import Updater, UpdateState, CheckError, DownloadError, VerifyError


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
        self._connect_updater_signals()

    def _setup_ui(self):
        self.setWindowTitle("NEVO - Update")
        self.setMinimumSize(480, 380)
        self.setMaximumSize(600, 520)

        layout = QVBoxLayout(self)
        layout.setSpacing(12)
        layout.setContentsMargins(24, 20, 24, 20)

        self._title_label = QLabel("Checking for updates...")
        self._title_label.setFont(QFont("", 14, QFont.Bold))
        layout.addWidget(self._title_label)

        self._version_label = QLabel("")
        self._version_label.setWordWrap(True)
        layout.addWidget(self._version_label)

        self._changelog_label = QTextEdit()
        self._changelog_label.setReadOnly(True)
        self._changelog_label.setMaximumHeight(140)
        self._changelog_label.setVisible(False)
        layout.addWidget(self._changelog_label)

        self._progress_bar = QProgressBar()
        self._progress_bar.setRange(0, 100)
        self._progress_bar.setValue(0)
        self._progress_bar.setVisible(False)
        layout.addWidget(self._progress_bar)

        self._status_label = QLabel("")
        self._status_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self._status_label)

        layout.addStretch()

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()

        self._check_btn = QPushButton("Check for Updates")
        self._check_btn.clicked.connect(self._on_check_clicked)
        btn_layout.addWidget(self._check_btn)

        self._download_btn = QPushButton("Download Update")
        self._download_btn.clicked.connect(self._on_download_clicked)
        self._download_btn.setVisible(False)
        btn_layout.addWidget(self._download_btn)

        self._install_btn = QPushButton("Install & Restart")
        self._install_btn.clicked.connect(self._on_install_clicked)
        self._install_btn.setVisible(False)
        btn_layout.addWidget(self._install_btn)

        self._cancel_btn = QPushButton("Cancel")
        self._cancel_btn.clicked.connect(self._on_cancel_clicked)
        self._cancel_btn.setVisible(False)
        btn_layout.addWidget(self._cancel_btn)

        self._close_btn = QPushButton("Close")
        self._close_btn.clicked.connect(self.close)
        btn_layout.addWidget(self._close_btn)

        layout.addLayout(btn_layout)

    def _connect_updater_signals(self):
        pass

    def _set_state_idle(self):
        self._title_label.setText("No updates available")
        self._version_label.setText(
            f"Current version: {self._updater.current_version}")
        self._progress_bar.setVisible(False)
        self._changelog_label.setVisible(False)
        self._status_label.setText("")
        self._check_btn.setVisible(True)
        self._download_btn.setVisible(False)
        self._install_btn.setVisible(False)
        self._cancel_btn.setVisible(False)
        self._close_btn.setVisible(True)

    def _set_state_checking(self):
        self._title_label.setText("Checking for updates...")
        self._version_label.setText("")
        self._progress_bar.setVisible(False)
        self._changelog_label.setVisible(False)
        self._status_label.setText("Connecting to GitHub...")
        self._check_btn.setEnabled(False)
        self._download_btn.setVisible(False)
        self._install_btn.setVisible(False)
        self._cancel_btn.setVisible(False)
        self._close_btn.setVisible(True)

    def _set_state_available(self):
        info = self._updater.latest_info
        if not info:
            return
        self._title_label.setText("Update available!")
        self._version_label.setText(
            f"Current: v{self._updater.current_version}  →  "
            f"Latest: v{info.version}")
        if info.changelog:
            self._changelog_label.setVisible(True)
            self._changelog_label.setMarkdown(info.changelog)
        self._status_label.setText("")
        self._check_btn.setVisible(False)
        self._download_btn.setVisible(True)
        self._install_btn.setVisible(False)
        self._cancel_btn.setVisible(False)
        self._close_btn.setVisible(True)

    def _set_state_downloading(self):
        self._title_label.setText("Downloading update...")
        self._progress_bar.setVisible(True)
        self._progress_bar.setValue(0)
        self._check_btn.setVisible(False)
        self._download_btn.setVisible(False)
        self._install_btn.setVisible(False)
        self._cancel_btn.setVisible(True)
        self._close_btn.setEnabled(False)

    def _set_state_ready(self):
        self._title_label.setText("Update ready to install")
        self._progress_bar.setValue(100)
        self._status_label.setText(
            "Download complete. Click 'Install & Restart' to apply.")
        self._check_btn.setVisible(False)
        self._download_btn.setVisible(False)
        self._install_btn.setVisible(True)
        self._cancel_btn.setVisible(False)
        self._close_btn.setEnabled(True)

    def _set_state_error(self, message: str):
        self._title_label.setText("Update failed")
        self._version_label.setText(f"Error: {message}")
        self._progress_bar.setVisible(False)
        self._changelog_label.setVisible(False)
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
            speed_str = f"{speed / (1024 * 1024):.1f} MB/s"
        elif speed > 1024:
            speed_str = f"{speed / 1024:.1f} KB/s"
        else:
            speed_str = f"{speed:.0f} B/s"

        if total > 0:
            dl_mb = downloaded / (1024 * 1024)
            total_mb = total / (1024 * 1024)
            self._status_label.setText(
                f"{dl_mb:.1f} / {total_mb:.1f} MB  •  {speed_str}")
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
