# Migrate GUI from C++ Qt Widgets to Python PyQt-Fluent-Widgets

## 1. Requirement Overview

Replace the existing C++ Qt6 Widgets GUI (both client `nevo_client_ui` and server `nevo_server_gui`) with Python GUIs using **PyQt-Fluent-Widgets** (Fluent Design System). The new GUIs must provide the same functionality with a modern Fluent Design aesthetic.

### Core Challenge

The current C++ GUI directly instantiates `ClientCore`/`ServerCore` and communicates via in-process callbacks. Since PyQt-Fluent-Widgets is a Python library, the GUI must be rewritten in Python. Communication with the C++ backends will use **subprocess + JSON-over-TCP IPC**.

## 2. Architecture

### 2.1 Project Structure

```
pygui/
  requirements.txt              # Python dependencies
  run_client.py                 # Client GUI entry point
  run_server.py                 # Server GUI entry point
  client/
    __init__.py
    main_window.py              # FluentWindow-based client main window
    pages/
      __init__.py
      channel_page.py           # Channel tree + user list page
      audio_page.py             # Audio settings page
    widgets/
      __init__.py
      connection_bar.py         # Bottom connection status bar
      channel_tree.py           # Channel tree view
      user_list.py              # User list view
    models/
      __init__.py
      channel_model.py          # Channel tree data model
      user_model.py             # User list data model
    bridge.py                   # IPC bridge to C++ client core
  server/
    __init__.py
    main_window.py              # FluentWindow-based server main window
    pages/
      __init__.py
      sessions_page.py          # Session table page
      channels_page.py          # Channel monitor page
      logs_page.py              # Server log page
      config_page.py            # Server configuration page
    widgets/
      __init__.py
      status_bar.py             # Server status indicator bar
    models/
      __init__.py
      session_model.py          # Session table data model
      channel_model.py          # Channel data model
    bridge.py                   # IPC bridge to C++ server core
  common/
    __init__.py
    theme.py                    # Theme configuration (dark/light)
    icons.py                    # FluentIcon mapping for NEVO
```

### 2.2 IPC Architecture

The Python GUI communicates with C++ backends through two mechanisms:

**Server GUI -> C++ Server**:
- Launch `nevo_server` as a subprocess
- C++ server exposes a **JSON-over-TCP control socket** on `localhost:24432`
- Protocol: Request-response with JSON frames (`\n`-delimited)
- Commands: `get_status`, `get_sessions`, `get_channels`, `kick_user`, `disconnect_all`, `shutdown`
- Server pushes events: `client_connected`, `client_disconnected`, `log_message`

**Client GUI -> C++ Client**:
- Launch a C++ bridge process (`nevo_client_bridge`) that wraps `ClientCore`
- Same JSON-over-TCP IPC pattern on `localhost:24433`
- Commands: `connect`, `disconnect`, `join_channel`, `leave_channel`, `mute`, `deafen`, `ptt`, `get_state`
- Bridge pushes events: `state_changed`, `user_joined`, `user_left`, `user_speaking`, `channel_list`, `latency_update`, `error`

### 2.3 C++ IPC Server Addition

Add a lightweight TCP control server to `ServerCore`:

```cpp
// In ServerCore.h, new class:
class ControlServer {
public:
    ControlServer(boost::asio::io_context& ioc, uint16_t port, ServerCore* core);
    void start();
    void stop();
private:
    void handleClient(tcp::socket socket);
    json handleCommand(const json& request);
    boost::asio::io_context& ioc_;
    tcp::acceptor acceptor_;
    ServerCore* core_;
};
```

Add a `nevo_client_bridge` executable that instantiates `ClientCore` and exposes the same JSON-over-TCP interface.

## 3. Client GUI Design (FluentWindow)

### 3.1 Window Layout

```
+--[Navigation]--+------------------------------------------+
|  [FluentIcon]  |  Channel Page / Audio Page                |
|  Channel       |                                           |
|  Audio         |  +--------------------------------------+|
|                |  | Channel Tree      | User List         ||
|  -----------   |  |  Root             |  Alice [Speaking] ||
|  [User Avatar] |  |  ├── General      |  Bob   [Muted]    ||
|  Username      |  |  │   ├── Chat     |  Charlie          ||
|                |  |  │   └── Gaming   |                   ||
|                |  +--------------------------------------+|
|                |  | Connection Bar                        ||
|                |  | [●] host:port [Connect] | 42ms | Vol  ||
|                |  +--------------------------------------+|
+----------------+------------------------------------------+
```

### 3.2 Widget Mapping (C++ Qt -> PyQt-Fluent-Widgets)

| Current C++ Widget | PyQt-Fluent-Widgets Replacement |
|---|---|
| `QMainWindow` | `FluentWindow` with `NavigationInterface` |
| `QDockWidget` (channel) | Left panel in `ChannelPage` (`SplitFluentWindow` or manual `QSplitter`) |
| `QDockWidget` (users) | Right panel in `ChannelPage` |
| `QTreeView` (channels) | `TreeWidget` from qfluentwidgets |
| `QListView` (users) | `ListWidget` from qfluentwidgets |
| `QToolBar` | Top area with `CommandBar` or `PushButton`/`ToolButton` |
| `QLineEdit` (server) | `LineEdit` / `SearchLineEdit` |
| `QPushButton` (connect) | `PrimaryPushButton` |
| `QSlider` (volume) | `Slider` from qfluentwidgets |
| `QComboBox` (devices) | `ComboBox` / `EditableComboBox` |
| `QCheckBox` (VAD, PTT) | `CheckBox` |
| `QDialog` (login) | `MessageBoxBase` |
| `QDialog` (audio settings) | `AudioPage` as navigation page |
| `QPlainTextEdit` (log) | `PlainTextEdit` |
| `QTableView` (sessions) | `TableWidget` |
| `QLabel` (status LED) | Custom painted `QWidget` or `IconWidget` |
| `QAction` (mute/deafen) | `TogglePushButton` / `ToggleToolButton` |
| `QMenuBar` | FluentWindow built-in menu |
| QSS dark theme | `setTheme(Theme.DARK)` + `setThemeColor()` |

### 3.3 ChannelPage

Primary page with channel tree and user list:

```python
class ChannelPage(ScrollArea):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("channelPage")
        
        # Main splitter: channel tree | user list
        self.splitter = QSplitter(Qt.Horizontal)
        self.channel_tree = TreeWidget()
        self.user_list = ListWidget()
        self.splitter.addWidget(self.channel_tree)
        self.splitter.addWidget(self.user_list)
        
        # Connection bar at bottom
        self.connection_bar = ConnectionBar()
        
        # Layout
        self.vBoxLayout = VBoxLayout(self)
        self.vBoxLayout.addWidget(self.splitter, 1)
        self.vBoxLayout.addWidget(self.connection_bar)
```

### 3.4 AudioPage

Settings page with Fluent Design cards:

```python
class AudioPage(ScrollArea):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("audioPage")
        
        # SettingCardGroup for each section
        self.device_group = SettingCardGroup("Audio Devices")
        self.input_card = ComboBoxSettingCard(...)  # Input device
        self.output_card = ComboBoxSettingCard(...)  # Output device
        
        self.levels_group = SettingCardGroup("Levels")
        self.gain_card = RangeSettingCard(...)  # Input gain
        self.volume_card = RangeSettingCard(...)  # Output volume
        
        self.vad_group = SettingCardGroup("Voice Activation")
        self.vad_card = SwitchSettingCard(...)  # VAD enable
        self.sensitivity_card = RangeSettingCard(...)  # VAD sensitivity
```

### 3.5 ConnectionBar

Bottom status bar with Fluent Design:

```python
class ConnectionBar(QWidget):
    def __init__(self, parent=None):
        # Status indicator (custom painted circle with glow)
        self.status_indicator = IconWidget(FluentIcon.CONNECT)
        self.server_edit = LineEdit()        # host:port
        self.connect_btn = PrimaryPushButton()  # Connect/Disconnect
        self.latency_label = BodyLabel("Latency: --")
        self.volume_slider = Slider(Qt.Horizontal)
        self.mute_btn = ToggleToolButton(FluentIcon.MUTE)
        self.deafen_btn = ToggleToolButton(FluentIcon.VOLUME)
```

## 4. Server GUI Design (FluentWindow)

### 4.1 Window Layout

```
+--[Navigation]--+------------------------------------------+
|  [FluentIcon]  |  Sessions / Channels / Logs / Config      |
|  Sessions      |                                           |
|  Channels      |  [Current page content]                   |
|  Logs          |                                           |
|  Config        |                                           |
|  -----------   |                                           |
|  [Status Bar]  |                                           |
+----------------+------------------------------------------+
```

### 4.2 Pages

**SessionsPage**: `TableWidget` showing connected clients
- Columns: ID, Username, Address, Channel, Connected Since, Status
- Context menu: Kick, View Details

**ChannelsPage**: `TreeWidget` showing channel hierarchy with user counts
- Columns: Channel Name, Users, ID

**LogsPage**: `PlainTextEdit` with auto-scroll, max line limit
- Filter bar with `SearchLineEdit`
- Level filter with `ComboBox` (All, Info, Warning, Error)

**ConfigPage**: `SettingCardGroup`-based configuration
- Server: TCP/UDP port, thread count
- Database: path, auto-save interval
- Security: TLS toggle, key file path

### 4.3 ServerStatusBar

Bottom bar in the navigation panel:

```python
class ServerStatusBar(CardWidget):
    def __init__(self, parent=None):
        self.status_indicator = QWidget()  # Custom painted circle
        self.running_label = BodyLabel("Stopped")
        self.clients_label = CaptionLabel("Clients: 0")
        self.channels_label = CaptionLabel("Channels: 0")
        self.uptime_label = CaptionLabel("Uptime: --")
        self.start_btn = PrimaryPushButton("Start")
        self.stop_btn = PushButton("Stop")
```

## 5. C++ IPC Implementation

### 5.1 Server Control Protocol

Add to `ServerCore` a `ControlServer` that listens on a configurable port (default 24432). Each connected client gets a line-delimited JSON stream.

**Request format:**
```json
{"id": 1, "command": "get_status"}
```

**Response format:**
```json
{"id": 1, "status": "ok", "data": {"running": true, "clients": 5, "channels": 3, "uptime_ms": 120000}}
```

**Event push format:**
```json
{"event": "client_connected", "data": {"session_id": 42, "username": "alice", "address": "192.168.1.5:52341"}}
```

**Supported commands:**
- `get_status` -> server status snapshot
- `get_sessions` -> list of active sessions
- `get_channels` -> channel tree with user counts
- `kick_user` -> disconnect a specific user (param: `session_id`)
- `disconnect_all` -> disconnect all users
- `shutdown` -> graceful server shutdown

### 5.2 Client Bridge Protocol

Create `nevo_client_bridge` executable that wraps `ClientCore` and exposes a JSON-over-TCP interface on port 24433.

**Commands:**
- `connect` (params: host, port, username, password)
- `disconnect`
- `join_channel` (param: channel_id)
- `leave_channel`
- `mute` / `deafen` / `ptt` (param: enabled)
- `set_volume` (param: value)
- `get_state`
- `get_channels`
- `get_audio_devices`
- `set_audio_device` (params: type, name)

**Events pushed:**
- `state_changed`, `user_joined`, `user_left`, `user_speaking`
- `channel_list`, `latency_update`, `error`, `log_message`

### 5.3 Bridge Base Class (Python)

```python
class BridgeBase:
    """Base class for JSON-over-TCP IPC with C++ backend"""
    
    def __init__(self, host="127.0.0.1", port=24432):
        self.sock = None
        self.host = host
        self.port = port
        self._request_id = 0
        self._pending = {}  # id -> Future
        self._event_handlers = {}
        self._reader_thread = None
    
    async def connect(self): ...
    async def send_command(self, command, params=None): ...
    def on_event(self, event_name, handler): ...
    def start_reader(self): ...
```

## 6. Files to Create/Modify

### 6.1 New Python Files

| File | Purpose |
|------|---------|
| `pygui/requirements.txt` | PyQt5, PyQt-Fluent-Widgets, qasync |
| `pygui/run_client.py` | Client entry point |
| `pygui/run_server.py` | Server entry point |
| `pygui/client/__init__.py` | Package init |
| `pygui/client/main_window.py` | `ClientMainWindow(FluentWindow)` |
| `pygui/client/pages/__init__.py` | Package init |
| `pygui/client/pages/channel_page.py` | Channel+User page |
| `pygui/client/pages/audio_page.py` | Audio settings page |
| `pygui/client/widgets/__init__.py` | Package init |
| `pygui/client/widgets/connection_bar.py` | Connection bar |
| `pygui/client/widgets/channel_tree.py` | Channel tree |
| `pygui/client/widgets/user_list.py` | User list |
| `pygui/client/models/__init__.py` | Package init |
| `pygui/client/models/channel_model.py` | Channel data |
| `pygui/client/models/user_model.py` | User data |
| `pygui/client/bridge.py` | IPC bridge |
| `pygui/server/__init__.py` | Package init |
| `pygui/server/main_window.py` | `ServerMainWindow(FluentWindow)` |
| `pygui/server/pages/__init__.py` | Package init |
| `pygui/server/pages/sessions_page.py` | Sessions table page |
| `pygui/server/pages/channels_page.py` | Channels monitor page |
| `pygui/server/pages/logs_page.py` | Log viewer page |
| `pygui/server/pages/config_page.py` | Server config page |
| `pygui/server/widgets/__init__.py` | Package init |
| `pygui/server/widgets/status_bar.py` | Server status bar |
| `pygui/server/models/__init__.py` | Package init |
| `pygui/server/models/session_model.py` | Session table model |
| `pygui/server/models/channel_model.py` | Channel model |
| `pygui/server/bridge.py` | IPC bridge |
| `pygui/common/__init__.py` | Package init |
| `pygui/common/theme.py` | Theme setup |
| `pygui/common/icons.py` | Icon mapping |

### 6.2 New C++ Files

| File | Purpose |
|------|---------|
| `src/server/include/nevo/server/ControlServer.h` | JSON-over-TCP control server |
| `src/server/src/ControlServer.cpp` | Implementation |

### 6.3 Modified C++ Files

| File | Modification |
|------|-------------|
| `src/server/include/nevo/server/ServerCore.h` | Add `ControlServer` member, start/stop with server |
| `src/server/src/ServerCore.cpp` | Initialize control server in `start()`, stop in `shutdown()` |
| `src/server/CMakeLists.txt` | Add `ControlServer.cpp` to both targets |
| `src/client/CMakeLists.txt` | Add `nevo_client_bridge` target |

### 6.4 Files to Keep (Unchanged)

| File | Reason |
|------|--------|
| All `src/core/` files | Core data models unchanged |
| All `src/network/` files | Network layer unchanged |
| `src/server/src/main.cpp` | CLI server entry still needed |
| `src/client/src/main.cpp` | CLI client still needed |

### 6.5 Files to Deprecate (Keep for Reference)

| File | Reason |
|------|--------|
| `src/ui/**` | Replaced by `pygui/client/` |
| `src/server/ui/**` | Replaced by `pygui/server/` |
| `src/ui/resources/themes/*.qss` | Replaced by Fluent theme system |

## 7. Boundary Conditions

- **Server not running**: GUI shows "Not Connected" state, Start button enabled
- **IPC socket unavailable**: Retry with exponential backoff, show error in status
- **C++ process crash**: Detect via subprocess exit code, show error dialog, offer restart
- **Multiple GUI instances**: Only one GUI should connect to a control socket
- **Windows firewall**: Control socket is localhost only, no firewall issue
- **Unicode**: All strings UTF-8 throughout IPC

## 8. Data Flow

### 8.1 Server GUI -> C++ Server

```
User clicks "Start" 
  -> ServerMainWindow.onStartServer()
  -> subprocess.Popen(["nevo_server", ...])
  -> Bridge.connect(port=24432)
  -> Bridge.send_command("get_status")
  <- {"status": "ok", "data": {"running": true, ...}}
  -> Timer: every 1s poll get_status + get_sessions + get_channels
  -> Update TableWidget / TreeWidget / StatusBar
```

### 8.2 C++ Server -> Server GUI (events)

```
Client connects to C++ server
  -> ServerCore::onClientConnected() 
  -> ControlServer broadcasts: {"event": "client_connected", "data": {...}}
  -> Bridge reader thread receives event
  -> Signal emitted via pyqtSignal
  -> UI updated on main thread
```

### 8.3 Client GUI -> C++ Client Bridge

```
User clicks "Connect"
  -> ClientMainWindow.onConnect(host, port)
  -> Bridge.send_command("connect", {host, port, username, password})
  <- {"status": "ok"} or {"status": "error", "message": "..."}
  -> Update UI state
```

## 9. Expected Outcomes

1. Both client and server GUIs use Fluent Design with Mica effect on Windows 11
2. Navigation via `FluentWindow` sidebar with `FluentIcon` icons
3. All existing functionality preserved (connect, channels, audio, logging, config)
4. C++ cores unchanged in behavior, only IPC control layer added
5. Python GUIs are standalone and can be launched independently
6. Dark/light theme support via `setTheme(Theme.DARK)` / `setTheme(Theme.LIGHT)`
