# Language Selection Feature Bug Analysis and Fix

## Requirement Scenario

The NEVO VoIP client has a language selection feature under `Settings > Language` with three options: English, Simplified Chinese, and Traditional Chinese. The feature has multiple bugs that prevent it from working correctly:

1. Language switching does not take effect after selecting a language
2. Some UI elements are not retranslated after a language switch
3. Missing translation entries in `.ts` files for certain strings

## Root Cause Analysis

### Bug 1 (Critical): `QTranslator::load()` fails to locate `.qm` files in Qt resources

**Files affected:**
- `src/ui/src/MainWindow.cpp:1160-1165` (`onLanguageChanged`)
- `src/ui/src/main.cpp:49-51` (initial translator loading)

**Root cause:**
The current code uses `QTranslator::load(QLocale, filename, prefix, directory, suffix)` with `directory=":/i18n"`. In Qt 6.11, this overload internally calls `QLocale::uiLanguages()` which returns BCP47 language tags (e.g., `"zh-Hans-CN"`, `"zh-Hans"`, `"zh"`) — these use **hyphens** as separators. However, the `.qm` resource files are named with POSIX/underscore format (e.g., `nevo_client_zh_CN.qm`). The constructed filename `nevo_client_zh-Hans-CN.qm` does not match `nevo_client_zh_CN.qm`, causing `load()` to fail.

The `.qrc` resource file (auto-generated) maps:
- `:/i18n/nevo_client_en.qm`
- `:/i18n/nevo_client_zh_CN.qm`
- `:/i18n/nevo_client_zh_TW.qm`

**Fix approach:**
Replace the locale-based `load()` call with explicit path construction + `QFile` + `load(const uchar*, qsizetype)`:

```cpp
// Construct the exact resource path
QString qm_path = QStringLiteral(":/i18n/nevo_client_%1.qm").arg(lang_code);
QFile qm_file(qm_path);
if (qm_file.open(QIODevice::ReadOnly)) {
    qm_data_ = qm_file.readAll();
    if (translator->load(reinterpret_cast<const uchar*>(qm_data_.constData()), qm_data_.size())) {
        // success
    }
}
```

This requires adding a `QByteArray qm_data_` member to `MainWindow` to ensure the data persists as long as the translator is active (Qt docs: "The data is not copied. The caller must guarantee that data will not be deleted or modified as long as the translator is installed.").

For `main.cpp`, use a heap-allocated `QByteArray` that persists for the app lifetime.

### Bug 2 (High): ConnectionBar `retranslateUi()` is incomplete

**File affected:** `src/ui/src/ConnectionBar.cpp:410-420`

**Issues:**
- Missing disconnected state: `connect_btn_->setText(tr("Connect"))` when not connected
- Missing connecting state: `connect_btn_->setText(tr("Cancel"))` when connecting
- Missing `volume_label_` retranslation: `tr("Vol:")` not updated
- Missing status indicator tooltips: `tr("Disconnected")`, `tr("Connecting")`, `tr("Connected")` not refreshed

**Fix approach:**
Complete the `retranslateUi()` method to handle all states and all translatable strings:

```cpp
void ConnectionBar::retranslateUi()
{
    if (server_edit_) server_edit_->setPlaceholderText(tr("Server address (host:port)"));
    
    // Re-apply button text based on current state
    if (connect_btn_) {
        if (is_connected_) {
            connect_btn_->setText(tr("Disconnect"));
        } else {
            connect_btn_->setText(tr("Connect"));
        }
    }
    
    if (volume_label_) volume_label_->setText(tr("Vol:"));
    if (latency_label_) latency_label_->setText(tr("Latency: --"));
    if (nat_type_label_) nat_type_label_->setText(tr("NAT: --"));
}
```

Note: Status indicator tooltips are set dynamically in `setStatusIndicatorColor()` based on the current state. They use `tr()` but are set at state change time, so they would pick up the current language. The `retranslateUi()` doesn't need to update them since they'll be refreshed at the next state change.

### Bug 3 (Medium): AudioSettingsWidget `retranslateUi()` is a no-op

**File affected:** `src/ui/src/AudioSettingsWidget.cpp:165-172`

**Issues:**
- `retranslateUi()` does nothing — it's an empty method with just a comment
- No `changeEvent()` override — `QEvent::LanguageChange` is never handled
- All 13+ `tr()` strings (form labels, buttons, section titles) are never retranslated

**Fix approach:**
1. Store form labels as member variables so they can be updated in `retranslateUi()`
2. Implement proper `retranslateUi()` that updates all translatable strings
3. Add `changeEvent()` override to handle `QEvent::LanguageChange`

### Bug 4 (Low): Missing translation entries in `.ts` files

**Files affected:**
- `src/ui/translations/nevo_client_zh_CN.ts`
- `src/ui/translations/nevo_client_zh_TW.ts`
- `src/ui/translations/nevo_client_en.ts`

**Missing strings:**
- `&Language` (language menu title)
- `Vol:` (ConnectionBar volume label)
- LoginDialog strings: `NEVO - Login`, `Connect to Server`, `Enter your credentials to join the voice server`, `Enter username`, `Username:`, `Enter password`, `Password:`, `Connect` (LoginDialog context)

**Fix approach:**
Add the missing entries to all three `.ts` files. The `LoginDialog` strings need a new `<context><name>LoginDialog</name>` section.

## Architecture and Technical Approach

### Data flow for language switching:
```
User clicks language action
  → onLanguageChanged(lang_code)
    → Save to QSettings
    → Remove old translator (posts LanguageChange event)
    → Delete old translator
    → Read .qm from Qt resources via QFile
    → Store data in qm_data_ (QByteArray member)
    → Create new QTranslator, load from qm_data_
    → Install translator (posts LanguageChange event)
    → Store translator in qApp property

Event loop processes LanguageChange events
  → MainWindow::changeEvent()
    → retranslateUi()
      → Update all tr() strings in MainWindow
      → Forward to ConnectionBar::retranslateUi()
      → Forward to AudioSettingsWidget::retranslateUi()
  → ConnectionBar::changeEvent()
    → retranslateUi() (redundant but harmless)
```

### Key implementation details:
- `QTranslator::load(const uchar*, qsizetype)` does NOT copy the data — the caller must ensure the data persists
- `qm_data_` (QByteArray) is a member of MainWindow and outlives the translator (since children are destroyed before members in C++ destruction order)
- `removeTranslator()` and `installTranslator()` both post `LanguageChange` events, causing `retranslateUi()` to be called twice — this is harmless but could be optimized by batching

## Affected Files

| File | Modification Type | Description |
|------|------------------|-------------|
| `src/ui/src/MainWindow.cpp` | Modify | Replace locale-based `load()` with `QFile` approach; add `#include <QFile>` |
| `src/ui/include/nevo/ui/MainWindow.h` | Modify | Add `QByteArray qm_data_` member |
| `src/ui/src/main.cpp` | Modify | Replace locale-based `load()` with `QFile` approach; add `#include <QFile>` |
| `src/ui/src/ConnectionBar.cpp` | Modify | Complete `retranslateUi()` to handle all states and strings |
| `src/ui/src/AudioSettingsWidget.cpp` | Modify | Implement proper `retranslateUi()`, add `changeEvent()` |
| `src/ui/include/nevo/ui/AudioSettingsWidget.h` | Modify | Add `changeEvent()` override, add label member variables |
| `src/ui/translations/nevo_client_zh_CN.ts` | Modify | Add missing translation entries |
| `src/ui/translations/nevo_client_zh_TW.ts` | Modify | Add missing translation entries |
| `src/ui/translations/nevo_client_en.ts` | Modify | Add missing translation entries |

## Boundary Conditions and Exception Handling

1. **Empty language code**: If `lang_code` is empty, don't attempt to load a translation file
2. **Resource file not found**: If `QFile::open()` fails, log a warning and keep the current language (don't delete the old translator)
3. **Translator load failure**: If `load(const uchar*, qsizetype)` fails, log a warning and delete the new translator
4. **No old translator on first switch**: `qApp->property("nevoTranslator")` may return an invalid QVariant; handle null pointer gracefully
5. **Switching to same language**: The current code doesn't check if the selected language is already active; this is harmless but could be optimized

## Expected Outcomes

1. Language switching works correctly — selecting Chinese shows Chinese UI
2. All visible UI elements are retranslated after a language switch
3. The app persists the language preference across restarts
4. No crashes, freezes, or console errors when switching languages
