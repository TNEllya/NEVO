# Migrate GUI to PyQt-Fluent-Widgets - Task Plan

- [x] Task 1: Create Python project structure and dependencies
    - 1.1: Create `pygui/` directory tree with all `__init__.py` files
    - 1.2: Create `pygui/requirements.txt` with PyQt5, PyQt-Fluent-Widgets dependencies
    - 1.3: Create `pygui/common/__init__.py`, `pygui/common/theme.py` (dark/light theme setup via `setTheme`)
    - 1.4: Create `pygui/common/icons.py` (FluentIcon mapping for NEVO: Channel, Audio, Sessions, Logs, Config, Connect, Mute, Volume, etc.)

- [x] Task 2: Implement Python IPC bridge base class
    - 2.1: Create `pygui/common/bridge_base.py` with `BridgeBase` class (JSON-over-TCP client with request-response and event push)
    - 2.2: Implement connection management (connect, reconnect with backoff, disconnect)
    - 2.3: Implement command sending with request ID tracking and response matching
    - 2.4: Implement event reader thread with pyqtSignal emission for UI thread safety
    - 2.5: Implement subprocess lifecycle management (start/stop C++ process)

- [x] Task 3: Create server-side IPC bridge
    - 3.1: Create `pygui/server/bridge.py` with `ServerBridge(BridgeBase)` specializing for server commands
    - 3.2: Add server-specific commands: `get_status`, `get_sessions`, `get_channels`, `kick_user`, `disconnect_all`, `shutdown`
    - 3.3: Define server event signals: `client_connected`, `client_disconnected`, `log_message`, `status_changed`

- [x] Task 4: Create server GUI data models
    - 4.1: Create `pygui/server/models/session_model.py` with session data class and table model
    - 4.2: Create `pygui/server/models/channel_model.py` with channel tree data class and model

- [x] Task 5: Create server GUI widgets
    - 5.1: Create `pygui/server/widgets/status_bar.py` with `ServerStatusBar(CardWidget)` (status indicator, running label, client/channel count, uptime, start/stop buttons)

- [x] Task 6: Create server GUI pages
    - 6.1: Create `pygui/server/pages/sessions_page.py` with `SessionsPage(ScrollArea)` (TableWidget showing active clients, context menu for kick)
    - 6.2: Create `pygui/server/pages/channels_page.py` with `ChannelsPage(ScrollArea)` (TreeWidget showing channel hierarchy with user counts)
    - 6.3: Create `pygui/server/pages/logs_page.py` with `LogsPage(ScrollArea)` (PlainTextEdit with search filter, level filter ComboBox, auto-scroll)
    - 6.4: Create `pygui/server/pages/config_page.py` with `ConfigPage(ScrollArea)` (SettingCardGroup: server ports, thread count, database path, TLS toggle)

- [x] Task 7: Create server main window
    - 7.1: Create `pygui/server/main_window.py` with `ServerMainWindow(FluentWindow)`
    - 7.2: Add navigation items: Sessions, Channels, Logs, Config with FluentIcon icons
    - 7.3: Add ServerStatusBar to navigation bottom area
    - 7.4: Wire start/stop buttons to ServerBridge commands
    - 7.5: Wire refresh timer (1s polling) to update sessions, channels, status data
    - 7.6: Wire bridge events to page updates (client_connected -> refresh sessions, etc.)

- [x] Task 8: Create server GUI entry point
    - 8.1: Create `pygui/run_server.py` (QApplication setup, dark theme, launch ServerMainWindow)
    - 8.2: Add CLI argument parsing (--control-port, --no-subprocess for connecting to already-running server)

- [x] Task 9: Create client-side IPC bridge
    - 9.1: Create `pygui/client/bridge.py` with `ClientBridge(BridgeBase)` specializing for client commands
    - 9.2: Add client-specific commands: `connect`, `disconnect`, `join_channel`, `leave_channel`, `mute`, `deafen`, `ptt`, `set_volume`, `get_state`, `get_channels`, `get_audio_devices`, `set_audio_device`
    - 9.3: Define client event signals: `state_changed`, `user_joined`, `user_left`, `user_speaking`, `channel_list`, `latency_update`, `error`

- [x] Task 10: Create client GUI data models
    - 10.1: Create `pygui/client/models/channel_model.py` with channel tree node data class and tree model
    - 10.2: Create `pygui/client/models/user_model.py` with user data class and list model

- [x] Task 11: Create client GUI widgets
    - 11.1: Create `pygui/client/widgets/connection_bar.py` with `ConnectionBar(QWidget)` (status indicator, LineEdit for host:port, PrimaryPushButton connect/disconnect, latency/nat labels, Slider volume, ToggleToolButton mute/deafen)
    - 11.2: Create `pygui/client/widgets/channel_tree.py` with `ChannelTree(TreeWidget)` (custom items with user count badges, double-click to join)
    - 11.3: Create `pygui/client/widgets/user_list.py` with `UserList(ListWidget)` (custom items with speaking indicator, muted/deafened icons)

- [x] Task 12: Create client GUI pages
    - 12.1: Create `pygui/client/pages/channel_page.py` with `ChannelPage(ScrollArea)` (QSplitter with ChannelTree + UserList + ConnectionBar at bottom)
    - 12.2: Create `pygui/client/pages/audio_page.py` with `AudioPage(ScrollArea)` (SettingCardGroup: devices ComboBoxSettingCard, gain/volume RangeSettingCard, VAD SwitchSettingCard + RangeSettingCard, noise suppression SwitchSettingCard)

- [x] Task 13: Create client main window
    - 13.1: Create `pygui/client/main_window.py` with `ClientMainWindow(FluentWindow)`
    - 13.2: Add navigation items: Channel, Audio with FluentIcon icons
    - 13.3: Wire connection bar signals to ClientBridge commands
    - 13.4: Wire channel tree join/leave to ClientBridge commands
    - 13.5: Wire audio page settings to ClientBridge commands
    - 13.6: Wire bridge events to page updates (state_changed -> connection bar, channel_list -> tree, etc.)
    - 13.7: Add login dialog (MessageBoxBase subclass) for connect flow

- [x] Task 14: Create client GUI entry point
    - 14.1: Create `pygui/run_client.py` (QApplication setup, dark theme, launch ClientMainWindow)
    - 14.2: Add CLI argument parsing (--bridge-port, --no-subprocess)

- [x] Task 15: Add C++ ControlServer for server IPC
    - 15.1: Create `src/server/include/nevo/server/ControlServer.h` with ControlServer class (Boost.Asio TCP acceptor, JSON command handler)
    - 15.2: Create `src/server/src/ControlServer.cpp` implementing command dispatch (get_status, get_sessions, get_channels, kick_user, disconnect_all, shutdown) and event broadcasting
    - 15.3: Modify `src/server/include/nevo/server/ServerCore.h` to add ControlServer member and control port config
    - 15.4: Modify `src/server/src/ServerCore.cpp` to start/stop ControlServer with the server lifecycle, broadcast events on client connect/disconnect/log
    - 15.5: Modify `src/server/CMakeLists.txt` to add ControlServer.cpp to nevo_server target

- [x] Task 16: Verify and test
    - 16.1: Run server GUI standalone (python run_server.py) and verify Fluent Design renders correctly
    - 16.2: Run client GUI standalone (python run_client.py) and verify Fluent Design renders correctly
    - 16.3: Build C++ server with ControlServer and verify it starts the control socket
    - 16.4: Test server GUI -> C++ server IPC: start/stop, session listing, log streaming
