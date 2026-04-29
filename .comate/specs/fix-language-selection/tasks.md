# Fix Language Selection Feature

- [x] Task 1: Fix QTranslator loading from Qt resources (MainWindow.cpp & main.cpp)
    - 1.1: Add `QByteArray qm_data_` member to `MainWindow.h`
    - 1.2: Add `#include <QFile>` to `MainWindow.cpp`
    - 1.3: Rewrite `onLanguageChanged()` — construct explicit resource path `":/i18n/nevo_client_{lang}.qm"`, open via `QFile`, read into `qm_data_`, load with `QTranslator::load(const uchar*, qsizetype)`
    - 1.4: Rewrite translator loading in `main.cpp` — same explicit path + `QFile` approach, use heap-allocated `QByteArray` for data persistence
    - 1.5: Add `#include <QFile>` to `main.cpp`

- [x] Task 2: Fix ConnectionBar retranslateUi() incomplete handling
    - 2.1: Add disconnected state: `connect_btn_->setText(tr("Connect"))` when `!is_connected_`
    - 2.2: Add `volume_label_` retranslation: `volume_label_->setText(tr("Vol:"))`
    - 2.3: Verify `latency_label_` and `nat_type_label_` retranslation (already present)

- [x] Task 3: Fix AudioSettingsWidget retranslateUi() no-op and missing changeEvent()
    - 3.1: Add `QLabel*` member variables in `AudioSettingsWidget.h` for all form layout labels that use `tr()`
    - 3.2: Add `void changeEvent(QEvent* event) override` declaration in `AudioSettingsWidget.h`
    - 3.3: Implement `changeEvent()` in `AudioSettingsWidget.cpp` to handle `QEvent::LanguageChange`
    - 3.4: Implement `retranslateUi()` to update all stored label texts with `tr()` calls
    - 3.5: Update `setupUi()` to store label pointers in the new member variables

- [x] Task 4: Add missing translation entries to .ts files
    - 4.1: Add `&Language` entry to MainWindow context in all three .ts files
    - 4.2: Add `Vol:` entry to ConnectionBar context in all three .ts files
    - 4.3: Add LoginDialog context with all missing strings in all three .ts files

- [x] Task 5: Fix context name mismatch in .ts files (namespace prefix missing)
    - 5.1: Changed `MainWindow` → `nevo::MainWindow` in all three .ts files
    - 5.2: Changed `LoginDialog` → `nevo::LoginDialog` in all three .ts files
    - 5.3: Changed `AudioSettingsWidget` → `nevo::AudioSettingsWidget` in all three .ts files
    - 5.4: Changed `ConnectionBar` → `nevo::ConnectionBar` in all three .ts files
