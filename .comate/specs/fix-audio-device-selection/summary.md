# Fix Audio Device Selection — Summary

## Root Cause
`ma_context_` (required for device enumeration via miniaudio) was only initialized inside `AudioEngine::initialize()`, which was only called during `ClientCore::connect()`. Before connecting to a server, `enumerateInputDevices()`/`enumerateOutputDevices()` returned empty lists, making the device combo boxes empty and unselectable.

## Changes Made

### AudioEngine.h / AudioEngine.cpp
- Added `initContext()` — initializes only `ma_context_` without starting devices/codecs. Returns `Ok()` if already initialized.
- Modified `initialize()` to reuse existing `ma_context_` from `initContext()` instead of creating a new one
- Added `currentInputDeviceName()`/`currentOutputDeviceName()` getters
- Added `current_input_device_name_`/`current_output_device_name_` member variables
- Updated `selectInputDevice()`/`selectOutputDevice()` to store the device name after switching
- Fixed `ma_ret` variable declaration that was lost when moving context init code to `initContext()`

### ClientCore.h / ClientCore.cpp
- Added `initAudioContext()` forwarding method
- Added `currentInputDeviceName()`/`currentOutputDeviceName()` forwarding methods
- Call `audio_engine_->initContext()` in constructor so device enumeration works immediately

### AudioSettingsWidget.h / AudioSettingsWidget.cpp
- Modified `refreshInputDevices()`/`refreshOutputDevices()` to accept optional `current_device` parameter
- After populating combo box, sets current index to match the active device
- Added `selectedInputDevice()`/`selectedOutputDevice()` getter methods

### MainWindow.cpp
- Pass current device names from `client_core_->currentInputDeviceName()`/`currentOutputDeviceName()` to `refreshInputDevices()`/`refreshOutputDevices()`
- Removed immediate `inputDeviceChanged`/`outputDeviceChanged` signal connections that applied changes in real-time
- Device selection now only applies when dialog is Accepted (consistent with other settings like volume/VAD)
- Device selection results logged alongside other settings

## Build Result
`[100%] Built target nevo_client_ui` — compilation succeeded.
