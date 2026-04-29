# NEVO 客户端现代化简约 GUI 实现任务计划

- [x] 任务 1：创建全局 QSS 主题样式引擎
    - 1.1：创建 `ThemeManager` 单例类，管理 QSS 样式表加载与应用
    - 1.2：创建深色主题 QSS 样式表文件 `dark_theme.qss`
    - 1.3：在 `main.cpp` 中初始化 `ThemeManager` 并应用主题
    - 1.4：更新 `src/ui/CMakeLists.txt`，添加新文件到构建目标并嵌入 QSS 资源

- [x] 任务 2：重构 MainWindow 主窗口布局与视觉层次
    - 2.1：调整 `MainWindow::setupUi()`，设置窗口最小尺寸、背景色、去除多余状态栏边框
    - 2.2：重构 `MainWindow::setupDockWidgets()`，为 `QDockWidget` 设置 modern 样式（无边框标题栏、自定义标题字体、圆角）
    - 2.3：优化 `MainWindow::setupMenuBar()`，设置菜单栏背景透明或深色融合、菜单项悬停高亮样式
    - 2.4：修改 `MainWindow::onAboutAction()`，使用自定义样式对话框替代 `QMessageBox::about`

- [x] 任务 3：美化 ConnectionBar 连接状态栏
    - 3.1：重写 `ConnectionBar::setupUi()`，增大内边距至 `16px`，使用 `QFrame::StyledPanel` 添加圆角卡片效果
    - 3.2：将 `QLineEdit` 和 `QPushButton` 替换为自定义样式（圆角输入框、扁平渐变按钮）
    - 3.3：优化 `setStatusIndicatorColor()`，将 16px 圆形指示灯增大至 20px，添加发光/阴影效果（`QGraphicsDropShadowEffect`）
    - 3.4：美化 `QSlider` 音量滑块：自定义 groove 和 handle 样式，使用主题强调色

- [x] 任务 4：美化 LoginDialog 和 AudioSettingsWidget
    - 4.1：重写 `LoginDialog` 构造函数，添加图标、增大内边距至 `24px`，设置输入框圆角和焦点边框高亮
    - 4.2：重构 `AudioSettingsWidget::setupUi()`，去除 `QGroupBox` 默认边框，改用卡片式分隔布局（`QFrame` + 圆角 + 轻微背景色区分）
    - 4.3：为 `AudioSettingsWidget` 中的滑块应用与 `ConnectionBar` 一致的自定义样式
    - 4.4：美化 `QComboBox` 下拉框样式（圆角、下拉箭头、选项悬停高亮）

- [ ] 任务 5：优化图标系统与模型视图美化
    - 5.1：创建 `IconProvider` 工具类，使用 `QPainter` 绘制现代化 SVG 风格图标（频道文件夹、用户头像占位、麦克风、耳机、说话指示器等）
    - 5.2：修改 `ChannelTreeModel`，将 `SP_DirIcon` 替换为 `IconProvider` 的频道图标
    - 5.3：修改 `UserListModel`，将标准图标替换为 `IconProvider` 绘制的静音、耳聋、说话图标
    - 5.4：优化 `QTreeView` 和 `QListView` 样式：去除网格线、设置行高、选中行高亮圆角、悬停效果

- [x] 任务 6：整合测试与细节打磨
    - 6.1：运行 CMake 配置和构建，检查编译错误
    - 6.2：检查所有 UI 控件的焦点状态和 Tab 顺序
    - 6.3：验证深色主题在 Windows 11 下的渲染效果，调整对比度确保可读性
    - 6.4：清理未使用的旧代码，确保没有内存泄漏或悬垂指针
