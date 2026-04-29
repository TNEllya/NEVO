# Server Config, Device Management & i18n — Summary

## Completed Tasks (15/15)

### Task 1-3: Server Config Optimization
- Added `Result<void> validate()` to `ServerConfig` with comprehensive field checks
- Extended `ServerConfigPanel` with Advanced Settings card: max_users, welcome_message, log_level, threads
- Added restart-required badges on port/thread fields
- Implemented hot-apply setters in `ServerCore`: `setMaxUsers()`, `setWelcomeMessage()`, `setLogLevel()`
- `setLogLevel()` immediately updates spdlog logging level

### Task 4-6: AudioEngine Device Management APIs
- `DeviceInfo` struct with name, id, is_default fields
- `enumerateInputDevices()` / `enumerateOutputDevices()` via `ma_context_get_devices()`
- `selectInputDevice(id)` / `selectOutputDevice(id)`: stop-reinit-restart pattern with `pDeviceID`
- `selectInputDeviceByName()` / `selectOutputDeviceByName()`: enumerate + match by name
- `playTestTone()`: sine wave generation into output FIFO
- `getCurrentInputLevel()` / `setInputLevelCallback()`: peak detection in capture callback via `std::atomic<float>`

### Task 7: ClientCore Bridge
- All device management methods forwarded from `ClientCore` to `AudioEngine` with null checks

### Task 8-9: AudioSettingsWidget & MainWindow Wiring
- Added Test Input (toggle), Test Output buttons and input level progress bar
- Level meter polling at ~30fps via QTimer
- MainWindow connects device enumeration, selection, and test signals

### Task 10-11: tr() Replacement
- Replaced all `QStringLiteral()` with `tr()` across 7 UI source files (client + server)
- Total: ~60 string replacements

### Task 12: retranslateUi() & changeEvent()
- Added `retranslateUi()` and `changeEvent(QEvent*)` override to MainWindow, ConnectionBar, ServerMainWindow
- Dynamic language switching via `QEvent::LanguageChange`

### Task 13: Translation Files & CMake
- Created 6 `.ts` files (en, zh_CN, zh_TW for client and server)
- Added `qt_add_translation()` to both CMakeLists.txt with `Qt6LinguistTools`

### Task 14: QTranslator & Language Selection UI
- QTranslator loading in client `main.cpp` and server `server_gui_main.cpp`
- Language menu in MainWindow with 3 options (English, Simplified Chinese, Traditional Chinese)
- Language persistence via `QSettings("NEVO", "NevoClient")`

### Task 15: Build Verification
- Fixed multiple compilation errors encountered during build:
  - spdlog stub: added `sink` base class, `sinks()` accessor on `logger`, made `basic_file_sink_mt` inherit from `sink`
  - Added missing `<cmath>` include for `std::sin` in AudioEngine.cpp
  - Fixed `ServerConfig` namespace qualifier in server main.cpp `parseArgs()`
  - Fixed `thread_count` -> `threads` field name mismatch
  - Added `ServerConfig.cpp` to `nevo_server` CMake target
  - Moved `ChannelInfo` struct from `ChannelTreeModel.h` to `nevo/core/model/Channel.h` to resolve cross-module dependency
  - Removed duplicate function closing braces in ClientCore.cpp
  - Fixed protobuf API usage: `has_user_info()` -> `user_info().id() != 0`, `channels(i)` -> range-for iteration
  - Fixed `ResultCode::InternalError` -> `ResultCode::Unknown`
  - Added missing method/signal declarations to `ConnectionBar.h`
  - Added `#include <QPointer>` and `#include <QEvent>` where needed
  - Fixed garbled comment in MainWindow.cpp
- All 4 build targets compile successfully: `libnevo_core.a`, `nevo_server.exe`, `nevo_server_gui.exe`, `nevo_client_ui.exe`

## Key Files Modified

| Category | Files |
|----------|-------|
| Server Config | `ServerConfig.h/cpp`, `ServerConfigPanel.h/cpp`, `ServerCore.h/cpp`, `ServerMainWindow.cpp` |
| Audio Device | `AudioEngine.h/cpp`, `ClientCore.h/cpp`, `AudioSettingsWidget.h/cpp`, `MainWindow.cpp` |
| i18n Client | `MainWindow.h/cpp`, `ConnectionBar.h/cpp`, `ChannelTreeModel.cpp`, `UserListModel.cpp`, `AudioSettingsWidget.cpp`, `main.cpp` |
| i18n Server | `ServerMainWindow.h/cpp`, `ServerConfigPanel.cpp`, `SessionTableModel.cpp`, `server_gui_main.cpp` |
| Translation | 6 `.ts` files (client + server, en/zh_CN/zh_TW) |
| Build System | `src/ui/CMakeLists.txt`, `src/server/CMakeLists.txt` |
| Stub Fixes | `spdlog.h`, `basic_file_sink.h`, `Logger.h` |
| Core Model | `Channel.h` (moved `ChannelInfo` here) |
