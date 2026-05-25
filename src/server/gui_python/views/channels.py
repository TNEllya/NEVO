"""Channels view - Channel tree display and management."""

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtWidgets import (
    QVBoxLayout, QHBoxLayout, QFrame, QHeaderView, QTreeWidgetItem,
)
from qfluentwidgets import (
    HeaderCardWidget, TitleLabel, CaptionLabel, TreeWidget,
    PushButton, PrimaryPushButton, InfoBar, InfoBarPosition,
    FluentIcon, StrongBodyLabel,
)


class ChannelsView(QFrame):
    """Channel management page."""

    def __init__(self, server_proc, parent=None):
        super().__init__(parent)
        self.server = server_proc
        self.setObjectName("channelsView")

        self._setup_ui()

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh)
        self._timer.start(3000)
        self._refresh()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(36, 20, 36, 20)
        layout.setSpacing(16)

        # Title
        title = TitleLabel(self.tr("Channels"))
        layout.addWidget(title)

        desc = CaptionLabel(self.tr("View and manage voice channels"))
        layout.addWidget(desc)
        layout.addSpacing(8)

        # Channel tree card
        card = HeaderCardWidget(self)
        card.setTitle(self.tr("Channel Tree"))

        self.tree = TreeWidget(self)
        self.tree.setHeaderLabels([
            self.tr("Channel"), self.tr("ID"), self.tr("Users"), self.tr("Parent ID")
        ])
        self.tree.header().setSectionResizeMode(0, QHeaderView.Stretch)
        self.tree.header().setSectionResizeMode(1, QHeaderView.ResizeToContents)
        self.tree.header().setSectionResizeMode(2, QHeaderView.ResizeToContents)
        self.tree.header().setSectionResizeMode(3, QHeaderView.ResizeToContents)
        self.tree.setAlternatingRowColors(True)
        self.tree.setMinimumHeight(300)
        self.tree.setIndentation(24)

        card.viewLayout.addWidget(self.tree)
        layout.addWidget(card)

        layout.addStretch(1)

    def _refresh(self):
        if not self.server.ipc_connected:
            self.tree.clear()
            return

        try:
            channels = self.server.get_channels()
            self._build_tree(channels)
        except Exception:
            pass

    def _build_tree(self, channels: list):
        self.tree.clear()

        if not channels:
            return

        # Build a map: parent_id -> [channel_data]
        children_map: dict[int, list] = {}
        root_channels = []

        for ch in channels:
            parent_id = int(ch.get("parent_id", 0))
            if parent_id == 0:
                root_channels.append(ch)
            else:
                children_map.setdefault(parent_id, []).append(ch)

        # Recursive tree builder
        def add_children(parent_item, parent_id: int):
            for ch in children_map.get(parent_id, []):
                item = QTreeWidgetItem(parent_item)
                item.setText(0, ch.get("channel_name", ""))
                item.setText(1, str(int(ch.get("channel_id", 0))))
                item.setText(2, str(int(ch.get("user_count", 0))))
                item.setText(3, str(int(ch.get("parent_id", 0))))
                ch_id = int(ch.get("channel_id", 0))
                add_children(item, ch_id)

        # Add root channels first
        for ch in root_channels:
            item = QTreeWidgetItem(self.tree)
            item.setText(0, ch.get("channel_name", ""))
            item.setText(1, str(int(ch.get("channel_id", 0))))
            item.setText(2, str(int(ch.get("user_count", 0))))
            item.setText(3, str(int(ch.get("parent_id", 0))))
            ch_id = int(ch.get("channel_id", 0))
            add_children(item, ch_id)

        self.tree.expandAll()

    def stop(self):
        self._timer.stop()
