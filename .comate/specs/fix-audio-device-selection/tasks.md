# Fix Audio Device Selection

- [x] Task 1: Add AudioEngine::initContext() for early ma_context initialization
    - 1.1: Add `initContext()` declaration in AudioEngine.h
    - 1.2: Implement `initContext()` in AudioEngine.cpp — only create ma_context_, no devices/codecs
    - 1.3: Modify `AudioEngine::initialize()` to reuse existing ma_context_ if already initialized

- [x] Task 2: Initialize audio context in ClientCore at construction time
    - 2.1: Add `initAudioContext()` method in ClientCore.h
    - 2.2: Call `audio_engine_->initContext()` in ClientCore constructor
    - 2.3: Ensure `shutdown()` still properly resets ma_context_

- [x] Task 3: Fix AudioSettingsWidget to restore current device selection
    - 3.1: Modify `refreshInputDevices()`/`refreshOutputDevices()` signatures to accept current device name
    - 3.2: After populating combo box, set current index to match the active device
    - 3.3: Add `selectedInputDevice()`/`selectedOutputDevice()` getter methods
    - 3.4: Update AudioSettingsWidget.h with new method signatures

- [x] Task 4: Fix MainWindow to pass current device info and apply on Accept only
    - 4.1: In `onAudioSettingsAction()`, get current device names from AudioEngine and pass to refresh methods
    - 4.2: Remove immediate device change signal connections (inputDeviceChanged/outputDeviceChanged)
    - 4.3: Apply device selection in the dialog Accepted handler alongside other settings

- [x] Task 5: Build and verify
    - 5.1: Build nevo_client_ui target
    - 5.2: Verify compilation succeeds
