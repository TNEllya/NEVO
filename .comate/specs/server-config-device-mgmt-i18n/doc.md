# NEVO Update Plan: Server Config, Device Management & i18n

## Feature Name: `server-config-device-mgmt-i18n`

---

## 1. Requirement Analysis

### 1.1 Server Configuration Enhancement

**Current State:** ServerConfig already supports `tcp_port`/`udp_port` via ServerConfigPanel (QSpinBox). ServerMainWindow detects port changes and calls `recreateServerCore()` for hot-restart when running. However, the config UI only exposes 3 fields (name, tcp_port, udp_port), while ServerConfig has more fields (`max_users`, `welcome_message`, `log_level`, `threads`) that are not editable from the GUI.

**Requirements:**
- Add more configuration options to ServerConfigPanel (max_users, welcome_message, log_level)
- Ensure port changes are correctly applied with proper validation and user feedback
- Allow some config changes (non-port fields) to take effect without server restart

**Affected Files:**
- `src/server/include/nevo/server/ServerConfig.h` — Add validation method
- `src/server/src/ServerConfig.cpp` — Add validate()
- `src/server/ui/include/nevo/server/ui/ServerConfigPanel.h` — Add new UI widgets for additional config fields
- `src/server/ui/src/ServerConfigPanel.cpp` — Add new config field widgets, validation feedback
- `src/server/ui/src/ServerMainWindow.cpp` — Refine apply logic to distinguish restart-required vs hot-applicable changes

### 1.2 Client Device Management

**Current State:** AudioSettingsWidget has `input_device_combo_` and `output_device_combo_` QComboBox widgets with signals `inputDeviceChanged`/`outputDeviceChanged`, but:
- AudioEngine uses default audio devices only (no `ma_device_id` selection)
- No device enumeration API exists in AudioEngine
- No device testing functionality (test tone playback, input level meter)
- The combo box signals are not connected to actual device switching logic

**Requirements:**
- Add device enumeration API to AudioEngine (list input/output devices via miniaudio's `ma_context_get_devices`)
- Add device selection API to AudioEngine (switch active device by `ma_device_id`)
- Connect AudioSettingsWidget device combos to AudioEngine's device APIs
- Add device test functionality:
  - Input: real-time input level meter (VU meter showing microphone level)
  - Output: play a test tone (short sine wave at 440Hz) to verify output device works

**Affected Files:**
- `src/core/include/nevo/core/audio/AudioEngine.h` — Add device enum/selection API, level meter API
- `src/core/src/audio/AudioEngine.cpp` — Implement device enumeration, selection, test tone, level meter
- `src/ui/include/nevo/ui/AudioSettingsWidget.h` — Add test buttons, level meter widget, connect to AudioEngine
- `src/ui/src/AudioSettingsWidget.cpp` — Implement device test UI, level meter display, device switching
- `src/ui/src/MainWindow.cpp` — Wire up device enumeration from AudioEngine to AudioSettingsWidget
- `src/client/include/nevo/client/ClientCore.h` — Expose device management API if needed
- `src/client/src/ClientCore.cpp` — Forward device API calls

### 1.3 Internationalization (i18n)

**Current State:** Zero i18n infrastructure. All strings hardcoded with `QStringLiteral()`. No `tr()` calls, no `.ts` files, no QTranslator, no language selection UI.

**Requirements:**
- Replace all user-facing `QStringLiteral()` with `tr()` calls across all UI code
- Create `.ts` translation files for: English (en), Simplified Chinese (zh_CN), Traditional Chinese (zh_TW)
- Configure CMake with `qt_add_translation()` for automated `.ts` → `.qm` compilation
- Add QTranslator loading in both client and server main.cpp
- Add language selection in settings UI
- Support dynamic language switching (ret translate all visible widgets)
- Design for extensibility: easy to add new languages later

**Affected Files:**
- `src/ui/src/AudioSettingsWidget.cpp` — Replace QStringLiteral with tr()
- `src/ui/src/MainWindow.cpp` — Replace QStringLiteral with tr()
- `src/ui/src/ConnectionBar.cpp` — Replace QStringLiteral with tr()
- `src/ui/src/ChannelTreeModel.cpp` — Replace QStringLiteral with tr()
- `src/ui/src/UserListModel.cpp` — Replace QStringLiteral with tr()
- `src/ui/include/nevo/ui/MainWindow.h` — Add language change slot, rettranslate helper
- `src/ui/include/nevo/ui/AudioSettingsWidget.h` — Add retranslateUi() method
- `src/ui/include/nevo/ui/ConnectionBar.h` — Add retranslateUi() method
- `src/ui/src/main.cpp` — Add QTranslator loading, language persistence
- `src/server/ui/src/ServerMainWindow.cpp` — Replace QStringLiteral with tr()
- `src/server/ui/src/ServerConfigPanel.cpp` — Replace QStringLiteral with tr()
- `src/server/ui/src/SessionTableModel.cpp` — Replace QStringLiteral with tr()
- `src/server/ui/src/main.cpp` — Add QTranslator loading
- `src/ui/CMakeLists.txt` — Add qt_add_translation(), .ts files
- `src/server/CMakeLists.txt` — Add qt_add_translation(), .ts files
- New files: translation .ts files for each language

---

## 2. Architecture & Technical Approach

### 2.1 Server Configuration Enhancement

**Approach:** Extend ServerConfigPanel with additional config fields. Distinguish between "hot-applicable" and "restart-required" config changes.

**Hot-applicable (no restart):** `server_name`, `welcome_message`, `max_users`, `log_level`
**Restart-required:** `tcp_port`, `udp_port`, `threads`, `db_path`

**Validation:** Add `ServerConfig::validate()` returning `Result<void>` with checks for:
- Port range 1-65535
- TCP != UDP port
- max_users > 0
- threads > 0
- Non-empty server name

**UI layout extension:** Add a second card-style group "Advanced Settings" below the existing "Network" group in ServerConfigPanel, containing:
- Max Users (QSpinBox, 1-10000)
- Welcome Message (QLineEdit with placeholder)
- Log Level (QComboBox: debug/info/warn/error)
- Threads (QSpinBox, 1-64, restart-required badge)

```cpp
// ServerConfigPanel.h additions
QSpinBox* max_users_spin_;
QLineEdit* welcome_edit_;
QComboBox* log_level_combo_;
QSpinBox* threads_spin_;
QLabel* restart_hint_label_;  // Shows which changes require restart
```

### 2.2 Client Device Management

**AudioEngine Device API:**

```cpp
// AudioEngine.h additions
struct DeviceInfo {
    std::string name;
    ma_device_id id;    // miniaudio device ID
    bool is_default;
};

std::vector<DeviceInfo> enumerateInputDevices();
std::vector<DeviceInfo> enumerateOutputDevices();
Result<void> selectInputDevice(const ma_device_id& id);
Result<void> selectOutputDevice(const ma_device_id& id);
Result<void> selectInputDeviceByName(const std::string& name);
Result<void> selectOutputDeviceByName(const std::string& name);

// Device test
Result<void> playTestTone(float frequency = 440.0f, float duration_sec = 1.0f);
float getCurrentInputLevel() const;  // 0.0 - 1.0 peak level

// Signal/callback for level meter
using InputLevelCallback = std::function<void(float level)>;
void setInputLevelCallback(InputLevelCallback cb);
```

**Device Enumeration:** Uses miniaudio's `ma_context_get_devices()` which returns arrays of `ma_device_info` (containing `name` and `id`). The existing `ma_context_` member in AudioEngine is already initialized.

**Device Selection:** To switch devices, stop the current `ma_device`, reinitialize with a `ma_device_config` that specifies the desired `ma_device_id`, then restart. The audio callback and data flow remain the same.

**Test Tone:** Generate a short sine wave buffer and play it through the output device using a separate temporary `ma_device` or by injecting the tone into the output buffer. Simpler approach: use `ma_decoder` from a memory buffer containing a generated WAV tone.

**Input Level Meter:** In the existing audio capture callback, compute the peak/RMS level of each frame. Store it in an atomic float, expose via `getCurrentInputLevel()`. The `InputLevelCallback` is invoked from the audio thread (consumer must dispatch to UI thread).

**AudioSettingsWidget Additions:**

```cpp
// AudioSettingsWidget.h additions
QPushButton* input_test_btn_;      // "Test Input" - starts level meter
QPushButton* output_test_btn_;     // "Test Output" - plays test tone
QProgressBar* input_level_bar_;    // VU meter bar (0-100%)
QTimer* level_meter_timer_;        // Polls level at ~30fps
bool testing_input_ = false;       // Whether input test is active

void startInputTest();
void stopInputTest();
void playTestTone();
void updateLevelMeter();           // QTimer handler
```

### 2.3 Internationalization (i18n)

**Approach:** Standard Qt i18n pipeline with `tr()` + `.ts` files + QTranslator.

**Step 1: Replace QStringLiteral with tr()**
All UI-facing strings in QWidgets subclasses change from `QStringLiteral("text")` to `tr("text")`. This enables Qt's translation mechanism. Classes must have `Q_OBJECT` macro (already present).

Example:
```cpp
// Before
label = new QLabel(QStringLiteral("Input Device:"));
// After
label = new QLabel(tr("Input Device:"));
```

**Step 2: Create .ts translation files**
Three files for each UI module:
- `src/ui/translations/nevo_client_en.ts`
- `src/ui/translations/nevo_client_zh_CN.ts`
- `src/ui/translations/nevo_client_zh_TW.ts`
- `src/server/ui/translations/nevo_server_en.ts`
- `src/server/ui/translations/nevo_server_zh_CN.ts`
- `src/server/ui/translations/nevo_server_zh_TW.ts`

**Step 3: CMake integration**
```cmake
# src/ui/CMakeLists.txt
set(TS_FILES
    translations/nevo_client_en.ts
    translations/nevo_client_zh_CN.ts
    translations/nevo_client_zh_TW.ts
)
qt_add_translation(QM_FILES ${TS_FILES})
# Add QM_FILES to resources or install step
```

**Step 4: QTranslator loading in main.cpp**
```cpp
QTranslator translator;
QSettings settings("NEVO", "NevoClient");
QString lang = settings.value("language", QLocale::system().name()).toString();
if (translator.load(":/translations/nevo_client_" + lang)) {
    app.installTranslator(&translator);
}
```

**Step 5: Language selection UI**
Add a language combo to MainWindow settings or a dedicated GeneralSettingsWidget:
```cpp
QComboBox* language_combo_;
// Items: "English", "简体中文", "繁體中文"
// Values: "en", "zh_CN", "zh_TW"
```

**Step 6: Dynamic language switching**
When language changes:
1. Save to QSettings
2. Remove old translator, load new translator
3. Call `retranslateUi()` on all visible widgets
4. Each widget class implements `retranslateUi()` that re-applies `tr()` strings

```cpp
void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi() {
    // Re-set all tr()-based strings
    menu_connection_->setTitle(tr("&Connection"));
    // ... for all menus, actions, labels
    audio_settings_->retranslateUi();
    connection_bar_->retranslateUi();
}
```

**Extensibility:** Adding a new language only requires:
1. Create a new `.ts` file
2. Add it to CMakeLists.txt `TS_FILES`
3. Add a combo box entry
4. Translate the strings in the `.ts` file

---

## 3. Data Flow

### 3.1 Server Config Apply Flow
```
User edits config in ServerConfigPanel
  → clicks Apply → applyRequested() signal
  → ServerMainWindow::onApplyConfig()
  → validate config via ServerConfig::validate()
  → if invalid: show error, abort
  → classify changes: hot-applicable vs restart-required
  → apply hot-applicable changes immediately (update ServerCore fields)
  → if restart-required and server running: prompt user, then recreateServerCore()
  → if restart-required and not running: show "will apply on next start"
```

### 3.2 Device Selection Flow
```
MainWindow populates device combos from AudioEngine::enumerateInputDevices/OutputDevices
  → User selects device in AudioSettingsWidget combo
  → inputDeviceChanged(name) signal
  → MainWindow::onInputDeviceChanged(name)
  → ClientCore::audioEngine().selectInputDeviceByName(name)
  → AudioEngine stops current input device
  → Reinitializes ma_device with selected ma_device_id
  → Restarts capture
```

### 3.3 Device Test Flow (Output)
```
User clicks "Test Output" button
  → AudioSettingsWidget::playTestTone()
  → AudioEngine::playTestTone(440.0f, 1.0f)
  → Generates sine wave buffer
  → Plays through current output device
  → Auto-stops after duration
```

### 3.4 Device Test Flow (Input)
```
User clicks "Test Input" button
  → startInputTest(): sets AudioEngine input level callback, starts QTimer
  → QTimer fires every 33ms → updateLevelMeter()
  → Reads AudioEngine::getCurrentInputLevel()
  → Updates QProgressBar value
  → User clicks "Stop" → stopInputTest(): removes callback, stops timer
```

### 3.5 Language Switch Flow
```
User selects language in settings combo
  → languageChanged(lang_code) signal
  → MainWindow::onLanguageChanged(lang_code)
  → Save to QSettings("language", lang_code)
  → Remove old QTranslator
  → Load new .qm file via QTranslator::load()
  → Install new translator
  → Qt sends LanguageChange event to all widgets
  → Each widget's changeEvent() calls retranslateUi()
  → All visible text updates to new language
```

---

## 4. Boundary Conditions & Exception Handling

### Server Config
- Port validation: reject ports < 1 or > 65535, reject TCP == UDP
- Config file not found: log warning, use defaults
- Hot-apply failure (e.g., max_users lower than current connected users): log warning, allow but show advisory message
- Concurrent config changes: GUI is single-threaded (Qt event loop), no race condition

### Device Management
- No devices available: show "No devices found" in combo, disable test buttons
- Device selection fails (device disconnected): show error, fall back to default
- Test tone while already testing: ignore (disable button during playback)
- Level meter: clip to 0-100%, handle NaN/Inf from audio computation
- AudioEngine not initialized: device API returns empty list or error
- Device switch while audio is active: must stop device first, brief audio interruption expected

### i18n
- Missing translation for a key: Qt falls back to source text (English)
- Corrupt .qm file: QTranslator::load() returns false, fall back to English
- Language change while modal dialog is open: dialog should also receive LanguageChange event
- First run with no saved language preference: use QLocale::system().name(), default to English if unsupported

---

## 5. Expected Outcomes

1. **Server Config Panel** shows 7 configurable fields with clear restart-required indicators and validation
2. **Device Selection** allows choosing specific input/output audio devices from enumerated list
3. **Device Testing** provides input level meter and output test tone for verification
4. **i18n** supports English, Simplified Chinese, and Traditional Chinese with dynamic switching
5. **Extensibility** for i18n: adding a new language requires only a `.ts` file and one combo box entry
6. **No regression** in existing core functionality (audio pipeline, network, channel system)
