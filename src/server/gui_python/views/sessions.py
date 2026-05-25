"""Sessions view - Connected client management with kick/ban actions."""

from PyQt5.QtCore import Qt, QTimer, pyqtSignal
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QFrame, QHeaderView, QTableWidgetItem,
    QAbstractItemView,
)
from qfluentwidgets import (
    HeaderCardWidget, TitleLabel, CaptionLabel, TableWidget,
    PrimaryPushButton, PushButton, InfoBar, InfoBarPosition,
    FluentIcon, SearchLineEdit, Dialog, LineEdit, TextEdit,
    StrongBodyLabel,
)


class BanUserDialog(Dialog):
    """Dialog for banning a user."""

    def __init__(self, username: str = "", user_id: int = -1, parent=None):
        super().__init__(self.tr("Ban User"), "", parent)
        self.user_id = user_id

        self.yesButton.setText(self.tr("Ban"))
        self.cancelButton.setText(self.tr("Cancel"))

        layout = QVBoxLayout()
        layout.setSpacing(8)

        if username:
            layout.addWidget(StrongBodyLabel(self.tr("Banning user: {}").format(username)))

        layout.addWidget(StrongBodyLabel(self.tr("IP Address (optional):")))
        self.ip_edit = LineEdit()
        self.ip_edit.setPlaceholderText(self.tr("e.g., 192.168.1.100"))
        layout.addWidget(self.ip_edit)

        layout.addWidget(StrongBodyLabel(self.tr("Reason:")))
        self.reason_edit = LineEdit()
        self.reason_edit.setText(self.tr("Banned via control API"))
        layout.addWidget(self.reason_edit)

        layout.addWidget(StrongBodyLabel(self.tr("Expires at (epoch, 0=permanent):")))
        self.expires_edit = LineEdit()
        self.expires_edit.setText("0")
        layout.addWidget(self.expires_edit)

        self.widget.layout.insertLayout(1, layout)

    def get_params(self) -> dict:
        return {
            "user_id": self.user_id,
            "ip_address": self.ip_edit.text().strip(),
            "reason": self.reason_edit.text().strip(),
            "expires_at": int(self.expires_edit.text().strip() or "0"),
        }


class SessionsView(QFrame):
    """Sessions management page."""

    def __init__(self, server_proc, parent=None):
        super().__init__(parent)
        self.server = server_proc
        self.setObjectName("sessionsView")

        self._setup_ui()

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh)
        self._timer.start(2000)
        self._refresh()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(36, 20, 36, 20)
        layout.setSpacing(16)

        # Title
        title = TitleLabel(self.tr("Sessions"))
        layout.addWidget(title)

        desc = CaptionLabel(self.tr("Manage connected client sessions"))
        layout.addWidget(desc)
        layout.addSpacing(8)

        # Sessions card
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Connected Clients"))

        # Toolbar
        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)

        self.search_edit = SearchLineEdit()
        self.search_edit.setPlaceholderText(self.tr("Search by username or address..."))
        self.search_edit.setFixedWidth(280)
        self.search_edit.textChanged.connect(self._filter_table)
        toolbar.addWidget(self.search_edit)

        toolbar.addStretch(1)

        self.btn_disconnect_all = PushButton(self.tr("Disconnect All"))
        self.btn_disconnect_all.setIcon(FluentIcon.CLOSE)
        self.btn_disconnect_all.clicked.connect(self._on_disconnect_all)
        toolbar.addWidget(self.btn_disconnect_all)

        card.viewLayout.insertLayout(0, toolbar)

        # Table
        self.table = TableWidget(self)
        self.table.setColumnCount(5)
        self.table.setHorizontalHeaderLabels([
            self.tr("Session ID"), self.tr("Username"),
            self.tr("Address"), self.tr("Channel"), self.tr("Status")
        ])
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setAlternatingRowColors(True)
        self.table.setMinimumHeight(300)
        self.table.setContextMenuPolicy(Qt.CustomContextMenu)
        self.table.customContextMenuRequested.connect(self._show_context_menu)

        card.viewLayout.addWidget(self.table)
        layout.addWidget(card)

        layout.addStretch(1)

    def _refresh(self):
        if not self.server.ipc_connected:
            self.table.setRowCount(0)
            return

        try:
            sessions = self.server.get_sessions()
            self._populate_table(sessions)
        except Exception:
            pass

    def _populate_table(self, sessions: list):
        self._all_sessions = sessions
        self._apply_filter(sessions)

    def _apply_filter(self, sessions: list):
        search = self.search_edit.text().strip().lower()
        filtered = sessions
        if search:
            filtered = [
                s for s in sessions
                if search in s.get("username", "").lower()
                or search in s.get("address", "").lower()
                or search in s.get("channel", "").lower()
            ]

        self.table.setRowCount(len(filtered))
        for row, s in enumerate(filtered):
            self.table.setItem(row, 0, QTableWidgetItem(str(s.get("session_id", ""))))
            self.table.setItem(row, 1, QTableWidgetItem(s.get("username", "")))
            self.table.setItem(row, 2, QTableWidgetItem(s.get("address", "")))
            self.table.setItem(row, 3, QTableWidgetItem(s.get("channel", "")))
            self.table.setItem(row, 4, QTableWidgetItem(s.get("status", "")))
            # Store user_id for context menu
            for col in range(5):
                item = self.table.item(row, col)
                if item:
                    item.setData(Qt.UserRole, s)

    def _filter_table(self):
        if hasattr(self, "_all_sessions"):
            self._apply_filter(self._all_sessions)

    def _show_context_menu(self, pos):
        item = self.table.itemAt(pos)
        if not item:
            return

        session_data = item.data(Qt.UserRole)
        if not session_data:
            return

        from qfluentwidgets import RoundMenu, Action, FluentIcon

        menu = RoundMenu(parent=self)

        kick_action = Action(FluentIcon.CLOSE, self.tr("Kick"))
        kick_action.triggered.connect(lambda: self._on_kick(session_data))
        menu.addAction(kick_action)

        ban_action = Action(FluentIcon.CANCEL, self.tr("Ban"))
        ban_action.triggered.connect(lambda: self._on_ban(session_data))
        menu.addAction(ban_action)

        menu.exec_(self.table.viewport().mapToGlobal(pos))

    def _on_kick(self, session_data: dict):
        session_id = session_data.get("session_id")
        username = session_data.get("username", "")
        try:
            result = self.server.kick_user(int(session_id))
            if result.get("kicked"):
                InfoBar.success(
                    self.tr("Kicked"),
                    self.tr("User '{}' has been kicked.").format(username),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=3000,
                )
            else:
                InfoBar.warning(
                    self.tr("Failed"),
                    result.get("message", self.tr("Session not found")),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=3000,
                )
        except Exception as e:
            InfoBar.error(
                self.tr("Error"), str(e),
                parent=self.window(), position=InfoBarPosition.TOP,
            )
        self._refresh()

    def _on_ban(self, session_data: dict):
        username = session_data.get("username", "")
        user_id = int(session_data.get("session_id", -1))

        dialog = BanUserDialog(username, user_id, self.window())
        if dialog.exec_():
            params = dialog.get_params()
            try:
                result = self.server.ban_user(**params)
                if result.get("banned"):
                    InfoBar.success(
                        self.tr("Banned"),
                        self.tr("User '{}' has been banned.").format(username),
                        parent=self.window(),
                        position=InfoBarPosition.TOP,
                        duration=3000,
                    )
                else:
                    InfoBar.warning(
                        self.tr("Failed"),
                        result.get("message", self.tr("Ban failed")),
                        parent=self.window(),
                        position=InfoBarPosition.TOP,
                        duration=3000,
                    )
            except Exception as e:
                InfoBar.error(
                    self.tr("Error"), str(e),
                    parent=self.window(), position=InfoBarPosition.TOP,
                )
            self._refresh()

    def _on_disconnect_all(self):
        from qfluentwidgets import MessageBox

        w = MessageBox(
            self.tr("Disconnect All"),
            self.tr("Are you sure you want to disconnect all clients?"),
            self.window(),
        )
        w.yesButton.setText(self.tr("Disconnect"))
        w.cancelButton.setText(self.tr("Cancel"))

        if w.exec_():
            try:
                result = self.server.disconnect_all()
                count = result.get("count", 0)
                InfoBar.success(
                    self.tr("Done"),
                    self.tr("Disconnected {} client(s).").format(count),
                    parent=self.window(),
                    position=InfoBarPosition.TOP,
                    duration=3000,
                )
            except Exception as e:
                InfoBar.error(
                    self.tr("Error"), str(e),
                    parent=self.window(), position=InfoBarPosition.TOP,
                )
            self._refresh()

    def stop(self):
        self._timer.stop()
