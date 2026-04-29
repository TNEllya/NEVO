# Mic Monitor (Local Loopback) Task Plan

- [x] Task 1: Add monitor FIFO and control members to AudioEngine.h
    - 1.1: Add `monitor_enabled_` atomic bool and `monitor_volume_` atomic float private members
    - 1.2: Add `monitor_fifo_` SPSC queue (Boost) / mutex+deque (fallback) private member
    - 1.3: Add `setMonitorEnabled(bool)` and `setMonitorVolume(float)` public method declarations

- [x] Task 2: Implement monitor push in maInputCallback (AudioEngine.cpp)
    - 2.1: After pushing to `input_fifo_`, check `monitor_enabled_` and push the same gain-applied frame to `monitor_fifo_`

- [x] Task 3: Implement monitor mix in maOutputCallback (AudioEngine.cpp)
    - 3.1: After popping from `output_fifo_`, also pop from `monitor_fifo_`
    - 3.2: Mix both frames into the output buffer with respective volumes, clamping to [-1.0, 1.0]

- [x] Task 4: Implement setMonitorEnabled/setMonitorVolume and cleanup (AudioEngine.cpp)
    - 4.1: Implement `setMonitorEnabled()` — store to atomic, drain `monitor_fifo_` when disabling
    - 4.2: Implement `setMonitorVolume()` — store to atomic
    - 4.3: In `shutdown()`, drain `monitor_fifo_` alongside existing FIFO cleanup

- [x] Task 5: Add forwarding methods to ClientCore
    - 5.1: Declare `setMonitorEnabled(bool)` and `setMonitorVolume(float)` in ClientCore.h
    - 5.2: Implement forwarding to `audio_engine_` in ClientCore.cpp

- [x] Task 6: Wire monitor enable/disable in MainWindow testInputToggled handler
    - 6.1: Add `client_core_->setMonitorEnabled(true)` when test input is activated
    - 6.2: Add `client_core_->setMonitorEnabled(false)` when test input is deactivated

- [x] Task 7: Build and verify
    - 7.1: Full build of all targets
    - 7.2: Fix any compilation errors
