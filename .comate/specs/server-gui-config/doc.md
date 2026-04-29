# Server GUI Configuration & Client Monitoring Enhancement

## Requirement Scenario

The existing `nevo_server_gui` already provides a Qt6-based management interface with session table, channel tree, log view, and status bar. However, it lacks the ability to **dynamically modify server configuration** (server name, TCP/UDP ports) and **persist those changes**. The user wants:

1. GUI controls to view and modify "Server Name" and "Listen Ports" (TCP/UDP)
2. A "Save Config" mechanism to persist changes to a JSON file
3. Clear real-time display of connected client information (name + IP) — already partially met by `SessionTableModel`
4. The ability to apply port changes (requires server restart since ports are bound at startup)

## Architecture & Technical Approach

### Approach Overview

- **Extend existing GUI** rather than creating new windows. Add a "Server Configuration" panel at the top of the main window.
- **Config persistence**: Add `saveToFile()` to `nevo::ServerConfig` struct for writing config back to JSON.
- **Port change mechanism**: Since `ServerCore` binds ports at construction, changing ports requires destroying and recreating the `ServerCore` instance. The "Apply" action will: stop server -> destroy old ServerCore -> create new ServerCore with updated ports -> (optionally) reinitialize and start.
- **Server name**: Stored in `ServerConfig::server_name`, displayed in the status bar and window title. Does not require server restart to change — can be applied immediately.
- **Client monitoring**: Already implemented via `SessionTableModel` (columns: User ID, Username, Address, Channel, Status). No fundamental changes needed, but we ensure the column headers clearly indicate "Client Name" and "IP Address".

### Data Flow

```
User edits config in GUI -> ServerConfigPanel reads input
  -> On "Apply":
       - If server running: stopServer() -> update config -> recreate ServerCore -> startServer()
       - If server stopped: just update config
  -> On "Save Config":
       - ServerConfig::saveToFile() writes JSON
```

## Affected Files

### 1. NEW: `src/server/ui/include/nevo/server/ui/ServerConfigPanel.h`
- Type: **New file**
- Purpose: Header for the configuration panel widget

### 2. NEW: `src/server/ui/src/ServerConfigPanel.cpp`
- Type: **New file**
- Purpose: Implementation of the configuration panel widget with input fields for server name, TCP port, UDP port, and Apply/Save buttons

### 3. MODIFY: `src/server/include/nevo/server/ServerConfig.h`
- Type: **Modification**
- Changes: Add `saveToFile(const std::string& path)` method declaration
- Affected function: New method `saveToFile`

### 4. MODIFY: `src/server/src/ServerConfig.cpp`
- Type: **Modification**
- Changes: Implement `saveToFile()` method that writes current config as JSON
- Affected function: New method `saveToFile`

### 5. MODIFY: `src/server/ui/include/nevo/server/ui/ServerMainWindow.h`
- Type: **Modification**
- Changes:
  - Add `ServerConfigPanel*` member
  - Add slot `onApplyConfig()`
  - Add slot `onSaveConfig()`
  - Store `ServerConfig` as a member (instead of just individual port fields)
  - Add method `recreateServerCore()` to handle port changes
  - Store config file path member

### 6. MODIFY: `src/server/ui/src/ServerMainWindow.cpp`
- Type: **Modification**
- Changes:
  - Integrate `ServerConfigPanel` into the UI layout (top area above the splitter)
  - Implement `onApplyConfig()` — read config from panel, apply server name immediately, restart server if ports changed
  - Implement `onSaveConfig()` — save current config to JSON file
  - Implement `recreateServerCore()` — destroy old ServerCore, create new one with updated ports
  - Update `onStartServer()` to use config from panel
  - Update `onRefreshSnapshot()` to reflect server name in title
  - Modify constructor to use `ServerConfig` and config path

### 7. MODIFY: `src/server/ui/src/server_gui_main.cpp`
- Type: **Modification**
- Changes:
  - Replace the local `ServerConfig` struct with `nevo::ServerConfig`
  - Pass `nevo::ServerConfig` and config file path to `ServerMainWindow`

### 8. MODIFY: `src/server/CMakeLists.txt`
- Type: **Modification**
- Changes: Add `ServerConfigPanel.cpp` and `ServerConfigPanel.h` to the `nevo_server_gui` target source list

## Implementation Details

### ServerConfigPanel Widget

```cpp
// ServerConfigPanel.h
#pragma once

#include <QFrame>
#include <QString>

class QLineEdit;
class QSpinBox;
class QPushButton;

namespace nevo {

struct ServerConfig;

class ServerConfigPanel : public QFrame {
    Q_OBJECT

public:
    explicit ServerConfigPanel(QWidget* parent = nullptr);
    ~ServerConfigPanel() override;

    void setConfig(const ServerConfig& config);
    ServerConfig getConfig() const;

    void setServerRunning(bool running);

signals:
    void applyRequested();
    void saveRequested();

private:
    void setupUi();

    QLineEdit* name_edit_;
    QSpinBox* tcp_port_spin_;
    QSpinBox* udp_port_spin_;
    QPushButton* apply_btn_;
    QPushButton* save_btn_;
};

} // namespace nevo
```

### ServerConfig::saveToFile() Implementation

```cpp
bool ServerConfig::saveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "    \"tcp_port\": " << tcp_port << ",\n";
    file << "    \"udp_port\": " << udp_port << ",\n";
    file << "    \"db_path\": \"" << db_path << "\",\n";
    file << "    \"threads\": " << threads << ",\n";
    file << "    \"log_level\": \"" << log_level << "\",\n";
    file << "    \"server_name\": \"" << server_name << "\",\n";
    file << "    \"max_users\": " << max_users << ",\n";
    file << "    \"welcome_message\": \"" << welcome_message << "\"\n";
    file << "}\n";

    return true;
}
```

### ServerMainWindow Modifications

The config panel will be placed as a collapsible group box at the top of the main window layout. The layout structure becomes:

```
QMainWindow
  └── Central Widget (QVBoxLayout)
        ├── ServerConfigPanel (QGroupBox "Server Configuration")
        │     ├── Server Name: [QLineEdit]
        │     ├── TCP Port: [QSpinBox 1-65535]
        │     ├── UDP Port: [QSpinBox 1-65535]
        │     ├── [Apply] [Save Config]
        │     └── Status hint label
        └── QSplitter (existing: session table | channel tree + log)
```

### Apply Config Logic

```cpp
void ServerMainWindow::onApplyConfig() {
    auto new_config = config_panel_->getConfig();

    bool ports_changed = (new_config.tcp_port != config_.tcp_port ||
                          new_config.udp_port != config_.udp_port);
    bool name_changed = (new_config.server_name != config_.server_name);

    config_ = new_config;

    if (name_changed) {
        // Update window title and status bar immediately
        updateWindowTitle();
        status_bar_->setServerName(QString::fromStdString(config_.server_name));
    }

    if (ports_changed && running_) {
        // Need to restart server with new ports
        recreateServerCore();
    } else if (ports_changed && !running_) {
        // Ports will be used on next start, no action needed
    }
}
```

### recreateServerCore Logic

```cpp
void ServerMainWindow::recreateServerCore() {
    bool was_running = running_;
    if (was_running) {
        stopServer();
    }

    // Recreate ServerCore with new ports
    server_core_ = std::make_unique<ServerCore>(*io_ctx_, config_.tcp_port, config_.udp_port);
    setupCallbacks(); // Re-register callbacks

    if (was_running) {
        onStartServer(); // Re-initialize and start
    }
}
```

### server_gui_main.cpp Changes

Replace the local `ServerConfig` struct with `nevo::ServerConfig`:

```cpp
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // ... theme setup ...

    nevo::ServerConfig config;
    // Parse CLI args using existing fromArgs or manually
    std::string config_path;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
            config.loadFromFile(config_path);
        }
        // ... other CLI args ...
    }

    // Create main window with full config
    nevo::ServerMainWindow window(config, config_path);
    window.show();

    return app.exec();
}
```

## Boundary Conditions & Exception Handling

1. **Invalid port range**: `QSpinBox` with range 1-65535 prevents invalid input at the GUI level
2. **Port conflict (EADDRINUSE)**: If the new port is already in use, `ServerCore::initialize()` will fail. Catch this and show `QMessageBox::critical()`, then revert to the old config.
3. **Config file write failure**: `saveToFile()` returns `false` on failure. Show error message to user.
4. **Empty server name**: Allow it but default to "NEVO Server" if left blank
5. **TCP == UDP port**: Show a warning if user sets the same port for both TCP and UDP, but don't prevent it (they use different protocols)
6. **Server running state during apply**: If server is running and ports change, must stop -> recreate -> restart. If only name changes, no restart needed.
7. **Concurrent access**: Config reading/writing is always done on the Qt main thread, no mutex needed. ServerCore is accessed from the main thread for start/stop which is safe.

## Expected Outcomes

1. Server configuration panel visible at top of the GUI window with editable fields for server name, TCP port, UDP port
2. Clicking "Apply" immediately updates server name in the title/status bar, and restarts the server if ports changed
3. Clicking "Save Config" persists the current configuration to a JSON file
4. Client monitoring table continues to show connected clients with their names and IP addresses in real-time
5. No disruption to existing server communication logic — all core networking code remains unchanged
6. Server can be started with new ports after applying config changes
