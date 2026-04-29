# NEVO 服务端现代化简约 GUI 设计文档

## 1. 需求概述

为 NEVO VoIP 服务端添加一个基于 Qt 6 Widgets 的现代化简约 GUI 管理面板，与客户端暗色主题风格保持一致。控制台版本 `nevo_server` 继续保留，新增 `nevo_server_ui` 图形化管理程序。

## 2. 架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                    nevo_server_ui (Qt 6 GUI)                │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │  ServerMain  │  │ SessionTable │  │ ChannelMonitor   │  │
│  │  Window      │  │   Model      │  │    Model         │  │
│  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘  │
│         │                 │                    │            │
│  ┌──────▼───────┐  ┌──────▼───────┐  ┌────────▼─────────┐  │
│  │ StatusBar    │  │ LogViewer    │  │ ServerController │  │
│  └──────────────┘  └──────────────┘  └──────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                    ServerCore (io_context 线程)              │
│         ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│         │ Database │  │ChannelMgr│  │AudioRelay│          │
│         └──────────┘  └──────────┘  └──────────┘          │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 线程模型

- **GUI 线程（主线程）**：运行 QApplication 事件循环，处理用户交互和 UI 更新
- **io_context 线程池（后台）**：运行 ServerCore 的网络 I/O、协程和音频中继
- **数据同步**：GUI 通过 `QTimer` 每秒轮询一次 `ServerCore` 状态快照，通过 `QMetaObject::invokeMethod` 将更新发布到 GUI 线程

## 3. 窗口布局设计

```
┌──────────────────────────────────────────────────────────────┐
│  [Start] [Stop] [Settings]                          NEVO    │  ← 工具栏
├──────────────┬──────────────────────────────┬────────────────┤
│              │                              │                │
│  Channels    │   Connected Clients          │   User Detail  │
│  ├─ Root     │   ┌────┬─────┬──────┬────┐  │   ┌──────────┐ │
│  │  ├─ Gen   │   │User│ IP  │Chan │NAT │  │   │ Avatar   │ │
│  │  ├─ Game  │   ├────┼─────┼──────┼────┤  │   │ Username │ │
│  │  └─ Music │   │Alice│...│Gen  │FC  │  │   │ IP: ...  │ │
│  └─ Lobby    │   │Bob  │...│Game │RC  │  │   │ Conn:... │ │
│     (3 users)│   └────┴─────┴──────┴────┘  │   │ Channel  │ │
│              │                              │   └──────────┘ │
├──────────────┴──────────────────────────────┴────────────────┤
│  [INFO] 2025-04-22 14:32:10 Client connected: user=1 ...     │  ← 日志面板
│  [INFO] 2025-04-22 14:32:15 UDP receive loop starting ...    │
├──────────────────────────────────────────────────────────────┤
│  TCP:24800 | UDP:24801 | Users: 3 | Uptime: 00:12:34  [●]   │  ← 状态栏
└──────────────────────────────────────────────────────────────┘
```

## 4. 新增/修改文件清单

### 4.1 新增文件（服务端 GUI 模块）

| 文件 | 类型 | 说明 |
|------|------|------|
| `src/server/ui/ServerMainWindow.h` | 头文件 | 主窗口类 |
| `src/server/ui/ServerMainWindow.cpp` | 实现 | 主窗口布局与逻辑 |
| `src/server/ui/SessionTableModel.h` | 头文件 | 在线会话表格模型 |
| `src/server/ui/SessionTableModel.cpp` | 实现 | 会话数据管理 |
| `src/server/ui/ChannelMonitorModel.h` | 头文件 | 频道监控树模型 |
| `src/server/ui/ChannelMonitorModel.cpp` | 实现 | 频道及用户分布数据 |
| `src/server/ui/LogViewer.h` | 头文件 | 日志查看器 |
| `src/server/ui/LogViewer.cpp` | 实现 | 日志文件实时监控 |
| `src/server/ui/ServerStatusBar.h` | 头文件 | 状态栏组件 |
| `src/server/ui/ServerStatusBar.cpp` | 实现 | 端口/用户/运行时间显示 |
| `src/server/ui/main_gui.cpp` | 实现 | GUI 程序入口点 |
| `src/server/CMakeLists.txt` | 修改 | 添加 GUI 目标和 Qt6 依赖 |

### 4.2 修改文件（服务端核心）

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `src/server/include/nevo/server/ServerCore.h` | 修改 | 添加状态查询方法、回调钩子 |
| `src/server/src/ServerCore.cpp` | 修改 | 实现状态快照方法 |
| `src/CMakeLists.txt` | 修改 | 条件编译 server_ui 模块 |

## 5. 核心组件详细设计

### 5.1 ServerMainWindow

- 继承 `QMainWindow`，暗色主题风格
- 顶部工具栏：`Start Server` / `Stop Server` / `Settings` / `Clear Log`
- 左侧 `QDockWidget`：频道监控树（`ChannelMonitorModel` + `QTreeView`）
- 中央区域：`QTableView`（`SessionTableModel`），显示在线用户
- 底部 `QDockWidget`：日志查看器（`LogViewer`）
- 底部状态栏：`ServerStatusBar`
- 定时器：每秒刷新一次会话和频道数据

### 5.2 SessionTableModel

- 继承 `QAbstractTableModel`
- 列：用户名、用户ID、IP地址、所在频道、连接时间、NAT类型
- 数据从 `ServerCore::getSessionSnapshot()` 获取
- 通过 `beginResetModel/endResetModel` 刷新

### 5.3 ChannelMonitorModel

- 继承 `QAbstractItemModel`（树形）
- 节点：频道名称 + 括号内用户数量
- 子节点：该频道中的用户名列表
- 数据来源：`ChannelManager::getChannelsWithUsers()`

### 5.4 LogViewer

- 继承 `QPlainTextEdit`（只读）
- 通过 `QFileSystemWatcher` 监控 `nevo_server.log`
- 或使用 spdlog 的自定义 sink 直接接收日志（方案二：更实时）
- **采用方案一（文件监控）**：实现更简单，不侵入服务端日志系统

### 5.5 ServerStatusBar

- 继承 `QWidget`，卡片式设计
- 显示：TCP端口、UDP端口、在线用户数、服务器运行时间、状态指示灯

## 6. ServerCore 扩展

### 6.1 新增状态快照结构

```cpp
struct SessionSnapshot {
    UserId user_id;
    std::string username;
    std::string ip_address;
    std::string channel_name;
    std::string nat_type;
    int64_t connect_time_ms;  // 连接时间戳
};

struct ServerStatusSnapshot {
    bool running;
    uint16_t tcp_port;
    uint16_t udp_port;
    size_t client_count;
    int64_t start_time_ms;
    std::vectorvector<SessionSnapshot> sessions;
};
```

### 6.2 新增查询方法

```cpp
// 线程安全地获取服务器状态快照
ServerStatusSnapshot getStatusSnapshot() const;

// 状态变更回调（供 GUI 连接）
std::function<void()> onStatusChanged;
```

## 7. 数据流与边界条件

### 7.1 数据流

1. `ServerCore` 在 io_context 线程上运行，处理客户端连接/断开
2. `ServerMainWindow` 的 `QTimer`（1秒间隔）触发 `refreshData()`
3. `refreshData()` 调用 `ServerCore::getStatusSnapshot()`（线程安全，内部加锁）
4. 获取的快照数据更新到 `SessionTableModel` 和 `ChannelMonitorModel`
5. 视图自动刷新显示

### 7.2 边界条件

- **服务器未启动**：显示 "Server Stopped"，所有面板为空，状态灯红色
- **零用户在线**：会话表格为空，频道树显示 0 用户
- **大量用户（>1000）**：表格使用 `uniformRowHeights` 优化，`QTimer` 间隔保持 1 秒避免卡顿
- **日志文件过大**：`LogViewer` 只显示最后 1000 行，启动时从文件末尾读取
- **关闭窗口**：自动调用 `ServerCore::shutdown()` 和 `io_context.stop()`，优雅关闭

## 8. 主题与样式

- 复用客户端暗色主题 QSS（`dark_theme.qss`）
- 背景色 `#1e1e2e`，表面色 `#252526`，强调色 `#5c8aff`
- 工具栏、状态栏采用卡片式设计，圆角 8px
- 表格交替行背景色、选中行高亮
- 状态指示灯：绿色（运行中）、红色（已停止）、黄色（启动中）

## 9. 依赖与构建

- **新增依赖**：Qt6::Widgets（可选，不存在时跳过 GUI 构建）
- **构建目标**：`nevo_server_ui`（可执行文件）
- **保留目标**：`nevo_server`（纯控制台，不变）
- **AUTOMOC**：启用 Qt Meta-Object Compiler

## 10. 预期效果

- 服务端管理员可以通过图形界面直观监控服务器运行状态
- 实时查看在线用户、频道分布、连接详情
- 一键启动/停止服务器
- 实时查看日志输出，无需打开日志文件
- 整体视觉风格与 NEVO 客户端统一，专业且现代
