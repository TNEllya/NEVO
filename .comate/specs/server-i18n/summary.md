# 服务端多语言功能 — 完成总结

## 变更概要

为服务端 GUI 添加了完整的多语言支持（英语、简体中文、繁体中文），包括运行时语言切换菜单和所有 UI 文本的动态翻译。

## 修改文件

| 文件 | 变更 |
|------|------|
| `src/server/ui/include/nevo/server/ui/ServerStatusBar.h` | 添加公共 `retranslateUi()` 方法、`changeEvent()` 覆盖、`last_snapshot_` 成员 |
| `src/server/ui/src/ServerStatusBar.cpp` | 9 处 `QStringLiteral` → `tr()`；实现 `retranslateUi()` 和 `changeEvent()` |
| `src/server/ui/include/nevo/server/ui/ServerConfigPanel.h` | 添加 `retranslateUi()` 声明、`changeEvent()` 覆盖、`network_group_`/`adv_group_` 成员 |
| `src/server/ui/src/ServerConfigPanel.cpp` | 保存 QGroupBox 指针为成员；实现 `retranslateUi()` 和 `changeEvent()`；添加 `#include <QEvent>` |
| `src/server/ui/include/nevo/server/ui/SessionTableModel.h` | 添加 `invalidateHeaders()` 声明 |
| `src/server/ui/src/SessionTableModel.cpp` | 实现 `invalidateHeaders()`，emit `headerDataChanged` |
| `src/server/ui/include/nevo/server/ui/ServerMainWindow.h` | 添加 `QActionGroup` include、`language_menu_`/`language_action_group_`/`qm_data_` 成员、`onLanguageChanged()` 槽 |
| `src/server/ui/src/ServerMainWindow.cpp` | 添加 Settings → Language 菜单；实现 `onLanguageChanged()` 运行时切换翻译；扩展 `retranslateUi()` 传播到子组件 |
| `src/server/ui/src/server_gui_main.cpp` | 翻译加载改为 Qt 资源路径 `:/i18n/nevo_server_{lang}.qm` |
| `src/server/ui/translations/nevo_server_en.ts` | 添加 ServerStatusBar 上下文 9 条 + ServerMainWindow 3 条新消息 |
| `src/server/ui/translations/nevo_server_zh_CN.ts` | 同上，简体中文翻译 |
| `src/server/ui/translations/nevo_server_zh_TW.ts` | 同上，繁体中文翻译 |

## 构建验证

全部目标编译链接通过：`nevo_core`, `nevo_network`, `nevo_server`, `nevo_server_gui`, `nevo_client`, `nevo_client_ui`
