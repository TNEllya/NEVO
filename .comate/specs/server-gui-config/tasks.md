# Server GUI Configuration & Client Monitoring Enhancement - Task Plan

- [x] Task 1: Add `saveToFile()` to ServerConfig
    - 1.1: Add `saveToFile(const std::string& path) const` declaration to `src/server/include/nevo/server/ServerConfig.h`
    - 1.2: Implement `saveToFile()` in `src/server/src/ServerConfig.cpp` — write all fields as formatted JSON

- [x] Task 2: Create ServerConfigPanel widget
    - 2.1: Create header `src/server/ui/include/nevo/server/ui/ServerConfigPanel.h` with QFrame subclass containing QLineEdit (server name), QSpinBox (TCP/UDP ports), QPushButton (Apply/Save)
    - 2.2: Create implementation `src/server/ui/src/ServerConfigPanel.cpp` — setupUi with styled dark-theme layout, setConfig/getConfig methods, setServerRunning to toggle port spinbox enablement

- [x] Task 3: Integrate ServerConfigPanel into ServerMainWindow
    - 3.1: Update `ServerMainWindow.h` — add ServerConfigPanel* member, ServerConfig member, config_path_ member, onApplyConfig()/onSaveConfig()/recreateServerCore() slots, update constructor signature to accept ServerConfig and config path
    - 3.2: Update `ServerMainWindow.cpp` constructor — store config, create config panel, insert into layout above the splitter
    - 3.3: Implement `onApplyConfig()` — read config from panel, detect name/port changes, apply name immediately, call recreateServerCore() if ports changed while running
    - 3.4: Implement `onSaveConfig()` — call ServerConfig::saveToFile(), show error on failure
    - 3.5: Implement `recreateServerCore()` — stop server, destroy old ServerCore, create new with updated ports, re-register callbacks, restart if was running
    - 3.6: Update `onStartServer()` to use `config_` member instead of fixed constructor params
    - 3.7: Update `onRefreshSnapshot()` to reflect server name in window title/status bar

- [x] Task 4: Update server_gui_main.cpp to use unified ServerConfig
    - 4.1: Replace local ServerConfig struct with `nevo::ServerConfig`, extract config_path from --config CLI arg
    - 4.2: Pass `nevo::ServerConfig` and config_path to ServerMainWindow constructor

- [x] Task 5: Update CMakeLists.txt for new source files
    - 5.1: Add `ui/src/ServerConfigPanel.cpp` and `ui/include/nevo/server/ui/ServerConfigPanel.h` to `nevo_server_gui` target in `src/server/CMakeLists.txt`

- [x] Task 6: Update ServerStatusBar to display server name
    - 6.1: Add `setServerName(const QString&)` method to ServerStatusBar
    - 6.2: Update server_label_ text in setServerName and setRunning to show the configured name
