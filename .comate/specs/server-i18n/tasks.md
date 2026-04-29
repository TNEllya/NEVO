# 服务端多语言功能 — 任务计划

- [x] Task 1: ServerStatusBar 硬编码字符串改为 tr()
    - 1.1: ServerStatusBar.h 添加 retranslateUi() 和 changeEvent() 声明
    - 1.2: ServerStatusBar.cpp 中 9 处 QStringLiteral 替换为 tr()
    - 1.3: 实现 retranslateUi() 和 changeEvent()

- [x] Task 2: ServerConfigPanel 添加 retranslateUi()
    - 2.1: ServerConfigPanel.h 添加 retranslateUi() 声明，保存需要重译的组件成员
    - 2.2: ServerConfigPanel.cpp 实现 retranslateUi()，添加 changeEvent()

- [x] Task 3: SessionTableModel 添加 invalidateHeaders()
    - 3.1: SessionTableModel.h 添加 invalidateHeaders() 声明
    - 3.2: SessionTableModel.cpp 实现invalidateHeaders()

- [x] Task 4: ServerMainWindow 添加语言菜单和切换功能
    - 4.1: ServerMainWindow.h 添加语言菜单成员和 onLanguageChanged() 槽
    - 4.2: setupMenuBar() 中创建 Settings → Language 子菜单
    - 4.3: 实现 onLanguageChanged() 运行时切换翻译
    - 4.4: 扩展 retranslateUi() 传播到子组件

- [x] Task 5: 修复 server_gui_main.cpp 翻译加载方式
    - 5.1: 改用 Qt 资源路径加载翻译文件

- [x] Task 6: 更新翻译文件
    - 6.1: 三个 .ts 文件添加 ServerStatusBar 上下文（9 条新消息）
    - 6.2: 三个 .ts 文件添加 ServerMainWindow 新条目（&Settings、&Language）

- [x] Task 7: 构建验证
    - 7.1: 编译全部目标确认无错误
