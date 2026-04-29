# Fix: GUI executables show console window on Windows

## Problem
When launching `nevo_server_gui.exe` on Windows, an unwanted PowerShell/console window appears behind the Qt GUI. The same issue exists for `nevo_client_ui.exe`.

## Root Cause
Both GUI targets use `add_executable(target_name ...)` without the `WIN32` keyword. Without it, CMake defaults the PE subsystem to `CONSOLE`, causing Windows to allocate a console window at launch.

The `nevo_platform_setup()` function only handles compile definitions and link libraries — it does not set the Windows subsystem.

## Solution
Add the `WIN32` keyword to `add_executable()` for both GUI targets:

- `src/server/CMakeLists.txt` line 50: `add_executable(nevo_server_gui WIN32 ...)`
- `src/ui/CMakeLists.txt` line 20: `add_executable(nevo_client_ui WIN32 ...)`

The `WIN32` keyword tells CMake to set `WIN32_EXECUTABLE TRUE`, which produces:
- MSVC: `/SUBSYSTEM:WINDOWS` linker flag
- MinGW: `-mwindows` linker flag

On non-Windows platforms, `WIN32` is silently ignored.

Qt6 already provides a `WinMain` entry point shim (`Qt6EntryPoint`) that calls the standard `main()`, so no source code changes are needed.

## Affected Files
| File | Modification |
|------|-------------|
| `src/server/CMakeLists.txt` | Add `WIN32` to `add_executable(nevo_server_gui ...)` |
| `src/ui/CMakeLists.txt` | Add `WIN32` to `add_executable(nevo_client_ui ...)` |

## Expected Outcome
- No console window appears when launching either GUI executable on Windows
- Entry points remain `int main(int argc, char* argv[])` — no source changes needed
- Non-Windows builds unaffected
