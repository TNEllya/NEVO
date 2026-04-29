# Fix GUI Console Window on Windows — Summary

## Root Cause
Both GUI executables (`nevo_server_gui` and `nevo_client_ui`) used `add_executable()` without the `WIN32` keyword, causing CMake to build them as CONSOLE subsystem applications. Windows then allocates a console window when launching them.

## Changes Made

### `src/server/CMakeLists.txt`
- Changed `add_executable(nevo_server_gui ...)` to `add_executable(nevo_server_gui WIN32 ...)`
- Also re-applied previously lost changes: removed ChannelMonitorModel files, added ControlServer.cpp

### `src/ui/CMakeLists.txt`
- Changed `add_executable(nevo_client_ui ...)` to `add_executable(nevo_client_ui WIN32 ...)`

### Previously lost channel monitor removal changes re-applied
Since the context window was lost, all edits from the channel monitor removal task were no longer present. Re-applied:
- ServerMainWindow.h: Removed ChannelMonitorModel include, QTreeView forward decl, channel_model_/channel_tree_ members
- ServerMainWindow.cpp: Removed channel tree, simplified layout, removed channel count from title
- ServerStatusBar.h: Removed channels_label_
- ServerStatusBar.cpp: Removed channels_label_ usage
- SessionTableModel.h: Removed ColChannel enum
- SessionTableModel.cpp: Removed channel column, column count 5→4
- Deleted ChannelMonitorModel.cpp and ChannelMonitorModel.h (again)

## Verification
- PE subsystem check confirmed: `nevo_server_gui.exe` now uses **WINDOWS subsystem** (value 2), not CONSOLE (value 3)
- Both `nevo_server_gui.exe` and `nevo_client_ui.exe` built successfully
- The `WIN32` keyword is silently ignored on non-Windows platforms, so no cross-platform impact
