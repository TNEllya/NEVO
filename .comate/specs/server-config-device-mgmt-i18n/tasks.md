# Server Config, Device Management & i18n — Task Plan

- [x] Task 1: Server Config Validation
    - 1.1: Add `Result<void> validate() const` method to ServerConfig.h, checking port range (1-65535), TCP != UDP, max_users > 0, threads > 0, non-empty server name
    - 1.2: Implement validate() in ServerConfig.cpp
    - 1.3: Call validate() in ServerMainWindow::onApplyConfig() and onApplyConfig() in ServerMainWindow, show error dialog on failure

- [x] Task 2: Server Config Panel — Extended UI Fields
    - 2.1: Add new widget members to ServerConfigPanel.h: `QSpinBox* max_users_spin_`, `QLineEdit* welcome_edit_`, `QComboBox* log_level_combo_`, `QSpinBox* threads_spin_`, `QLabel* restart_hint_label_`
    - 2.2: Implement the "Advanced Settings" card group in ServerConfigPanel.cpp with all new widgets, styled consistently with existing UI
    - 2.3: Update `setConfig()` and `getConfig()` to read/write all new fields
    - 2.4: Update `setServerRunning()` to disable threads_spin_ when running (restart-required field), show restart hint for port + threads changes
    - 2.5: Add visual indicator (badge/asterisk) next to fields that require server restart

- [x] Task 3: Server Config Hot-Apply Logic
    - 3.1: Add hot-applicable setter methods to ServerCore.h: `setMaxUsers(int)`, `setWelcomeMessage(const std::string&)`, `setLogLevel(const std::string&)`
    - 3.2: Implement setters in ServerCore.cpp (update member variables + apply immediately where feasible, e.g., update spdlog level on setLogLevel)
    - 3.3: Refine ServerMainWindow::onApplyConfig() to classify changes into hot-applicable vs restart-required, apply hot changes via ServerCore setters, show appropriate status messages

- [x] Task 4: AudioEngine Device Enumeration API
    - 4.1: Add `DeviceInfo` struct and `enumerateInputDevices()` / `enumerateOutputDevices()` declarations to AudioEngine.h
    - 4.2: Implement device enumeration in AudioEngine.cpp using `ma_context_get_devices()`, wrap results into `DeviceInfo` vector
    - 4.3: Guard all device code with `#ifdef NEVO_HAS_MINIAUDIO` or existing guards, provide empty stubs otherwise

- [x] Task 5: AudioEngine Device Selection API
    - 5.1: Add `selectInputDevice(const ma_device_id&)`, `selectOutputDevice(const ma_device_id&)`, and by-name variants to AudioEngine.h
    - 5.2: Implement device selection in AudioEngine.cpp: stop current device → reinit ma_device with selected id → restart, preserving audio callback
    - 5.3: Store selected device IDs as members (`selected_input_id_`, `selected_output_id_`) so reinitialization preserves user choice

- [x] Task 6: AudioEngine Device Test & Level Meter API
    - 6.1: Add `playTestTone(float frequency, float duration_sec)`, `getCurrentInputLevel()`, `setInputLevelCallback()` to AudioEngine.h
    - 6.2: Implement input level tracking: compute peak level in audio capture callback, store in `std::atomic<float>`, invoke callback
    - 6.3: Implement test tone: generate sine wave buffer, play via a temporary ma_device or inject into output pipeline
    - 6.4: Add `getCurrentInputLevel()` returning the atomic float value

- [x] Task 7: Client Device Management — ClientCore Bridge
    - 7.1: Add forwarding methods to ClientCore.h: `enumerateInputDevices()`, `enumerateOutputDevices()`, `selectInputDeviceByName()`, `selectOutputDeviceByName()`, `playTestTone()`, `getCurrentInputLevel()`, `setInputLevelCallback()`
    - 7.2: Implement forwarding in ClientCore.cpp (delegate to `audio_engine_`)
    - 7.3: Guard with `#ifdef NEVO_HAS_BOOST` as ClientCore depends on Boost

- [x] Task 8: AudioSettingsWidget — Device Test UI
    - 8.1: Add `input_test_btn_`, `output_test_btn_`, `input_level_bar_`, `level_meter_timer_`, `testing_input_` to AudioSettingsWidget.h; add `retranslateUi()` method
    - 8.2: Implement "Test Output" button: calls playTestTone(), disables button during playback, re-enables after
    - 8.3: Implement "Test Input" toggle button: starts/stops level meter timer, shows QProgressBar, polls getCurrentInputLevel() at ~30fps
    - 8.4: Add level meter layout below input device combo (label + progress bar + test button in a row)
    - 8.5: Add test output button row below output device combo

- [x] Task 9: MainWindow — Wire Up Device Management
    - 9.1: In MainWindow::onAudioSettingsAction(), populate device combos from AudioEngine enumeration before showing dialog
    - 9.2: Connect AudioSettingsWidget::inputDeviceChanged / outputDeviceChanged signals to AudioEngine device selection
    - 9.3: Handle device selection errors (show QMessageBox on failure)

- [x] Task 10: i18n — Replace QStringLiteral with tr() in Client UI
    - 10.1: AudioSettingsWidget.cpp: replace all QStringLiteral with tr() for user-facing strings
    - 10.2: MainWindow.cpp: replace all QStringLiteral with tr() for menus, actions, labels, dialogs
    - 10.3: ConnectionBar.cpp: replace all QStringLiteral with tr()
    - 10.4: ChannelTreeModel.cpp: replace all QStringLiteral with tr()
    - 10.5: UserListModel.cpp: replace all QStringLiteral with tr()

- [x] Task 11: i18n — Replace QStringLiteral with tr() in Server UI
    - 11.1: ServerConfigPanel.cpp: replace all QStringLiteral with tr()
    - 11.2: ServerMainWindow.cpp: replace all QStringLiteral with tr()
    - 11.3: SessionTableModel.cpp: replace all QStringLiteral with tr()

- [x] Task 12: i18n — Add retranslateUi() Methods
    - 12.1: AudioSettingsWidget: add retranslateUi() — re-apply all tr()-based label/button text
    - 12.2: MainWindow: add retranslateUi() — re-apply menu titles, action text, status messages; forward to child widgets
    - 12.3: ConnectionBar: add retranslateUi() — re-apply button labels, tooltips
    - 12.4: Override changeEvent(QEvent*) in each widget to call retranslateUi() on LanguageChange event

- [x] Task 13: i18n — Translation Files & CMake Setup
    - 13.1: Create `src/ui/translations/` directory and empty .ts files: nevo_client_en.ts, nevo_client_zh_CN.ts, nevo_client_zh_TW.ts
    - 13.2: Create `src/server/ui/translations/` directory and empty .ts files: nevo_server_en.ts, nevo_server_zh_CN.ts, nevo_server_zh_TW.ts
    - 13.3: Add `qt_add_translation()` to src/ui/CMakeLists.txt with TS_FILES list, embed .qm files as resources
    - 13.4: Add `qt_add_translation()` to src/server/CMakeLists.txt with server TS_FILES
    - 13.5: Fill English .ts files with source strings (copy source text as translation for en)
    - 13.6: Fill zh_CN .ts files with Simplified Chinese translations
    - 13.7: Fill zh_TW .ts files with Traditional Chinese translations

- [x] Task 14: i18n — QTranslator Loading & Language Selection UI
    - 14.1: Add QTranslator loading logic to src/ui/src/main.cpp: read language from QSettings, load .qm, install translator
    - 14.2: Add QTranslator loading logic to src/server/ui/src/main.cpp
    - 14.3: Add language selection combo to MainWindow settings area (or a new GeneralSettingsWidget): "English", "简体中文", "繁體中文"
    - 14.4: Implement language switch: save to QSettings, remove old translator, load new .qm, install translator (Qt auto-sends LanguageChange events)
    - 14.5: Persist language choice in QSettings("NEVO", "NevoClient") / QSettings("NEVO", "NevoServer")

- [x] Task 15: Build Verification
    - 15.1: Run full build and fix any compilation errors
    - 15.2: Verify server UI starts and shows extended config panel
    - 15.3: Verify client UI starts and shows device test controls
    - 15.4: Verify language switching works (at least UI text changes)
