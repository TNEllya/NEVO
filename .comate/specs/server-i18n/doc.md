# 服务端多语言功能 — 设计文档

## 需求描述

为服务端 GUI（nevo_server_gui）添加完整的多语言支持（简体中文、繁体中文、英语），包括运行时语言切换。

## 现状分析

服务端 GUI 已有部分 i18n 基础设施：
- 3 个翻译文件已存在（en / zh_CN / zh_TW），覆盖 47 条消息
- CMakeLists.txt 已有 `qt_add_translation()` 
- ServerMainWindow 已有 `retranslateUi()` 和 `changeEvent()`
- ServerConfigPanel、SessionTableModel 中的字符串已用 `tr()` 包裹

**缺陷：**
1. **无语言菜单** — 无法在运行时切换语言
2. **ServerStatusBar** 有 9 处硬编码英文字符串使用 `QStringLiteral()` 而非 `tr()`
3. **ServerStatusBar** 无 `retranslateUi()` / `changeEvent()`，无法响应语言切换
4. **ServerConfigPanel** 无 `retranslateUi()`，语言切换后标签不更新
5. **SessionTableModel** 的表头在语言切换后不会刷新
6. **server_gui_main.cpp** 翻译加载方式不正确（文件系统而非 Qt 资源）
7. **ServerMainWindow::retranslateUi()** 未向子组件传播语言变更

## 受影响文件

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `src/server/ui/include/nevo/server/ui/ServerMainWindow.h` | 修改 | 添加语言菜单成员、`onLanguageChanged()` 槽 |
| `src/server/ui/src/ServerMainWindow.cpp` | 修改 | 添加语言菜单、`onLanguageChanged()`、完善 `retranslateUi()` |
| `src/server/ui/include/nevo/server/ui/ServerStatusBar.h` | 修改 | 添加 `retranslateUi()` 和 `changeEvent()` |
| `src/server/ui/src/ServerStatusBar.cpp` | 修改 | `QStringLiteral` → `tr()`，添加 `retranslateUi()` |
| `src/server/ui/include/nevo/server/ui/ServerConfigPanel.h` | 修改 | 添加 `retranslateUi()` 和 `changeEvent()` |
| `src/server/ui/src/ServerConfigPanel.cpp` | 修改 | 添加 `retranslateUi()` |
| `src/server/ui/src/SessionTableModel.cpp` | 修改 | 添加 `invalidateHeaders()` |
| `src/server/ui/src/server_gui_main.cpp` | 修改 | 修改翻译加载方式，使用 Qt 资源 |
| `src/server/ui/translations/nevo_server_en.ts` | 修改 | 添加 ServerStatusBar 上下文 + 语言菜单条目 |
| `src/server/ui/translations/nevo_server_zh_CN.ts` | 修改 | 同上 |
| `src/server/ui/translations/nevo_server_zh_TW.ts` | 修改 | 同上 |

## 详细实现

### 1. ServerMainWindow — 添加语言菜单

**ServerMainWindow.h** 新增成员：
```cpp
// --- 语言 ---
QMenu* language_menu_ = nullptr;
QActionGroup* language_action_group_ = nullptr;
QByteArray qm_data_;

private slots:
    void onLanguageChanged(const QString& lang_code);
```

**ServerMainWindow.cpp** `setupMenuBar()` 新增语言子菜单（在 Help 菜单前）：
```cpp
QMenu* settings_menu = menuBar()->addMenu(tr("&Settings"));
language_menu_ = settings_menu->addMenu(tr("&Language"));
language_action_group_ = new QActionGroup(this);
language_action_group_->setExclusive(true);

QSettings lang_settings(QStringLiteral("NEVO"), QStringLiteral("NevoServer"));
QString current_lang = lang_settings.value(QStringLiteral("language"), QStringLiteral("en")).toString();

struct LangEntry { QString code; QString label; };
LangEntry languages[] = {
    {QStringLiteral("en"),    QStringLiteral("English")},
    {QStringLiteral("zh_CN"), QStringLiteral("简体中文")},
    {QStringLiteral("zh_TW"), QStringLiteral("繁體中文")},
};

for (const auto& lang : languages) {
    QAction* action = language_menu_->addAction(lang.label);
    action->setData(lang.code);
    action->setCheckable(true);
    action->setChecked(lang.code == current_lang);
    language_action_group_->addAction(action);
    connect(action, &QAction::triggered, this, [this, action]() {
        onLanguageChanged(action->data().toString());
    });
}
```

**onLanguageChanged()** — 与客户端相同模式：
- 保存语言偏好到 QSettings
- 移除旧翻译器
- 从 Qt 资源 `:/i18n/nevo_server_{lang}.qm` 加载新翻译器
- `QCoreApplication::installTranslator()` 自动触发 `LanguageChange` 事件

**retranslateUi()** — 扩展传播到子组件：
```cpp
void ServerMainWindow::retranslateUi() {
    // 菜单动作
    if (start_action_) start_action_->setText(tr("&Start"));
    if (stop_action_) stop_action_->setText(tr("S&top"));
    if (disconnect_all_action_) disconnect_all_action_->setText(tr("Disconnect &All"));
    if (about_action_) about_action_->setText(tr("&About"));
    if (quit_action_) quit_action_->setText(tr("&Quit"));

    // 菜单标题
    QList<QMenu*> menus = menuBar()->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
    if (menus.size() >= 3) {
        menus[0]->setTitle(tr("&Server"));
        menus[1]->setTitle(tr("&Settings"));
        menus[2]->setTitle(tr("&Help"));
    }

    // 语言菜单标题
    if (language_menu_) language_menu_->setTitle(tr("&Language"));

    // 传播到子组件
    if (config_panel_) config_panel_->retranslateUi();
    if (session_model_) session_model_->invalidateHeaders();
    if (status_bar_) status_bar_->retranslateUi();
}
```

### 2. ServerStatusBar — `QStringLiteral` → `tr()`

替换所有 9 处硬编码字符串：
- `"NEVO Server"` → `tr("NEVO Server")`（构造函数默认值）
- `"Running"` / `"Stopped"` → `tr("Running")` / `tr("Stopped")`
- `"Clients: %1 / %2 auth"` → `tr("Clients: %1 / %2 auth")`
- `"Relayed: %1"` → `tr("Relayed: %1")`
- `"Uptime: %1:%2:%3"` → `tr("Uptime: %1:%2:%3")`
- 初始标签文本同理

添加 `retranslateUi()` 方法：
```cpp
void ServerStatusBar::retranslateUi() {
    setRunning(is_running_);  // 重新设置 Running/Stopped tooltip
    // 重新设置当前快照数据（如果有的话，会自动使用新的 tr()）
}
```

添加 `changeEvent()` 覆盖：
```cpp
void ServerStatusBar::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QFrame::changeEvent(event);
}
```

### 3. ServerConfigPanel — 添加 retranslateUi()

需要将 `setupUi()` 中创建的标签保存为成员变量，或在 `retranslateUi()` 中通过布局查找。

**方案：** 使用 `findChildren` 查找 QGroupBox 和 QLabel 进行重新翻译（避免增加大量成员变量）。

**更简洁的方案：** 将需要重新翻译的标签存为成员变量。

在 ServerConfigPanel.h 新增：
```cpp
void retranslateUi() override;  // QFrame 没有虚 retranslateUi，直接声明
QGroupBox* network_group_;
QGroupBox* adv_group_;
```

在 retranslateUi() 中更新组标题和按钮文字。

### 4. SessionTableModel — 添加表头刷新

```cpp
void SessionTableModel::invalidateHeaders() {
    emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
}
```

### 5. server_gui_main.cpp — 修复翻译加载

将文件系统加载改为 Qt 资源加载：
```cpp
QString qm_path = QStringLiteral(":/i18n/nevo_server_%1.qm").arg(lang);
QFile qm_file(qm_path);
if (qm_file.open(QIODevice::ReadOnly)) {
    // read and load from data
}
```

## 翻译文件新增条目

### ServerStatusBar 上下文（新增）
| Source | en | zh_CN | zh_TW |
|--------|----|-------|-------|
| NEVO Server | NEVO Server | NEVO 服务器 | NEVO 伺服器 |
| Running | Running | 运行中 | 運行中 |
| Stopped | Stopped | 已停止 | 已停止 |
| Clients: %1 / %2 auth | Clients: %1 / %2 auth | 客户端：%1 / %2 已认证 | 用戶端：%1 / %2 已認證 |
| Clients: 0 / 0 auth | Clients: 0 / 0 auth | 客户端：0 / 0 已认证 | 用戶端：0 / 0 已認證 |
| Relayed: %1 | Relayed: %1 | 已中继：%1 | 已中繼：%1 |
| Relayed: 0 | Relayed: 0 | 已中继：0 | 已中繼：0 |
| Uptime: %1:%2:%3 | Uptime: %1:%2:%3 | 运行时间：%1:%2:%3 | 運行時間：%1:%2:%3 |
| Uptime: 00:00:00 | Uptime: 00:00:00 | 运行时间：00:00:00 | 運行時間：00:00:00 |

### ServerMainWindow 新增条目
| Source | en | zh_CN | zh_TW |
|--------|----|-------|-------|
| &Settings | &Settings | 设置(&S) | 設定(&S) |
| &Language | &Language | 语言(&L) | 語言(&L) |

## 预期结果

- Server GUI 的 Server 菜单下新增 Settings → Language 子菜单
- 可在运行时切换英语、简体中文、繁体中文
- 所有 UI 文本（状态栏、配置面板、表头、菜单）随语言切换实时更新
