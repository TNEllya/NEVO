# Fix GUI Console Window on Windows

- [x] Task 1: Add WIN32 flag to server GUI executable
    - 1.1: Edit `src/server/CMakeLists.txt` — add `WIN32` to `add_executable(nevo_server_gui ...)`

- [x] Task 2: Add WIN32 flag to client UI executable
    - 2.1: Edit `src/ui/CMakeLists.txt` — add `WIN32` to `add_executable(nevo_client_ui ...)`

- [x] Task 3: Rebuild and verify
    - 3.1: Build both targets and confirm successful compilation
