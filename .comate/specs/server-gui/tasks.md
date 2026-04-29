# NEVO 服务端现代化简约 GUI 实现任务计划

- [ ] 任务 1：扩展 ServerCore 以支持 GUI 状态查询
    - 1.1：在 `ServerCore.h` 中新增 `getServerStatus()` 方法，返回 `ServerStatus` 快照（在线人数、运行状态、端口信息）
    - 1.2：在 `ServerCore.h` 中新增 `getAllSessions()` 方法，返回当前所有 `ClientSession` 的共享指针列表（线程安全）
    - 1.3：在 `ServerCore.h` 中新增 `getChannelStats()` 方法，返回频道及用户分布统计
    - 1.4：在 `ServerCore.cpp` 中实现上述方法，确保正确加锁

- [ ] 任务 2：创建服务端 GUI 模型与视图组件
    - 2.1：创建 `SessionTableModel`（`QAbstractTableModel`），显示会话列表（用户名、IP、连接时间、频道、NAT类型）
    - 2.2：创建 `ChannelMonitorModel`（`QAbstractItemModel`），显示频道树及每个频道的在线人数
    - 2.3：创建 `ServerLogView`（基于 `QPlainTextEdit`），支持追加日志、自动滚动、最大行数限制
    - 2.4：创建 `ServerStatusBar`（底部状态栏），显示服务器状态、端口、在线人数、运行时间

- [ ] 任务 3：创建 ServerMainWindow 主窗口
    - 3.1：创建 `ServerMainWindow.h`，定义主窗口类、工具栏动作、停靠窗口布局
    - 3.2：实现 `ServerMainWindow::setupUi()`，设置窗口标题、尺寸、中央部件为会话列表、左右停靠窗口
    - 3.3：实现工具栏和菜单栏（启动、停止、设置、清空日志、关于）
    - 3.4：实现 `refreshData()` 定时轮询方法，从 `ServerCore` 获取状态并更新所有视图
    - 3.5：实现启动/停止服务器逻辑，在后台线程运行 `io_context`

- [ ] 任务 4：创建服务端 GUI 入口与主题系统
    - 4.1：创建 `src/server/gui/main_gui.cpp`，`QApplication` 入口，初始化 `ThemeManager` 并应用暗色主题
    - 4.2：复用客户端 `ThemeManager` 和 `IconProvider`（通过共享头文件）
    - 4.3：创建服务端专用的 QSS 样式文件 `server_dark_theme.qss`
    - 4.4：修改根 `CMakeLists.txt`，在 Qt6 存在时构建 `nevo_server_ui` 目标

- [ ] 任务 5：CMake 整合与构建验证
    - 5.1：修改 `src/server/CMakeLists.txt` 或创建新的 UI 构建配置，添加 GUI 源文件和 Qt6 链接
    - 5.2：运行 CMake 配置和构建，修复所有编译错误
    - 5.3：验证服务端 GUI 可正常启动并与 ServerCore 交互
    - 5.4：确认原有 `nevo_server` 控制台目标不受影响
