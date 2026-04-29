# NEVO 客户端现代化简约 GUI 设计文档

## 1. 需求概述

为 NEVO VoIP 客户端添加一套**现代化的简约 GUI 风格**。当前客户端使用原生 Qt 默认样式，视觉效果较为陈旧。本方案通过引入全局 QSS 主题系统、优化控件样式与布局、改进图标绘制，打造具有现代感的深色简约界面，同时保持所有现有功能不变。

## 2. 设计原则

- **简约 (Minimalist)**：减少视觉噪音，使用干净的线条、充足的留白、克制的色彩
- **深色主题 (Dark Theme)**：采用现代深色配色方案，降低视觉疲劳，突出内容
- **一致性 (Consistency)**：所有控件遵循统一的圆角、间距、配色规范
- **功能保留 (Functional Preservation)**：不修改任何业务逻辑、信号槽连接和数据模型

## 3. 架构与技术方案

### 3.1 主题系统架构

引入一个集中式的 QSS 样式表加载机制：

```
ThemeManager (单例)
  ├── 加载并缓存 QSS 字符串
  ├── 应用到 QApplication
  └── 提供配色常量供代码绘制使用
```

### 3.2 配色方案 (Color Palette)

| 用途 | 色值 | 说明 |
|------|------|------|
| 背景主色 | `#1E1E2E` | 深紫灰，主窗口背景 |
| 背景次色 | `#252535` | 面板、停靠窗口背景 |
| 背景 tertiary | `#2A2A3C` | 输入框、按钮背景 |
| 表面高亮 | `#31314A` | Hover 状态、选中项背景 |
| 主强调色 | `#7AA2F7` | 蓝紫色，按钮、激活状态、连接指示 |
| 次强调色 | `#BB9AF7` | 淡紫色，次要交互元素 |
| 成功/在线 | `#9ECE6A` | 绿色，已连接状态 |
| 警告/连接中 | `#E0AF68` | 琥珀色，连接中状态 |
| 错误/断开 | `#F7768E` | 粉红，断开/错误状态 |
| 主文字 | `#C0CAF5` | 浅蓝白，主要文本 |
| 次文字 | `#565F89` | 灰蓝，次要文本、占位符 |
| 边框 | `#3B3B54` |  subtle 边框线 |

### 3.3 字体与排版

- 主字体：`Segoe UI` (Windows), `SF Pro Display` (macOS), `Inter` (Linux fallback)
- 全局基础字号：`13px`
- 标题/频道名：`14px`, weight 500
- 状态文本：`12px`

## 4. 受影响文件与修改类型

### 4.1 新增文件

| 文件 | 类型 | 说明 |
|------|------|------|
| `src/ui/include/nevo/ui/ThemeManager.h` | Header | 主题管理器单例类声明 |
| `src/ui/src/ThemeManager.cpp` | Source | 主题管理器实现，提供 QSS 生成与应用 |
| `src/ui/resources/themes/dark_theme.qss` | Resource | 深色主题 QSS 样式表文件 |
| `src/ui/resources/style.qrc` | Resource | Qt 资源文件，打包 QSS |

### 4.2 修改文件

| 文件 | 修改类型 | 修改内容 |
|------|----------|----------|
| `src/ui/src/main.cpp` | 编辑 | 在 `QApplication` 创建后初始化 `ThemeManager` |
| `src/ui/src/MainWindow.cpp` | 编辑 | 调整窗口默认尺寸、停靠窗口样式属性、菜单样式；改进 About 对话框样式 |
| `src/ui/include/nevo/ui/MainWindow.h` | 编辑 | 无结构变更，仅配合 cpp 调整 |
| `src/ui/src/ConnectionBar.cpp` | 编辑 | 重绘状态指示灯（更现代的发光环形），调整布局间距、输入框和按钮的 QSS class，改进颜色值匹配主题 |
| `src/ui/include/nevo/ui/ConnectionBar.h` | 编辑 | 无结构变更 |
| `src/ui/src/AudioSettingsWidget.cpp` | 编辑 | 调整 GroupBox 样式、滑块轨道与手柄样式、整体间距 |
| `src/ui/include/nevo/ui/AudioSettingsWidget.h` | 编辑 | 无结构变更 |
| `src/ui/src/UserListModel.cpp` | 编辑 | 重绘说话/静音/耳聋指示图标，使用主题配色替代硬编码颜色 |
| `src/ui/src/ChannelTreeModel.cpp` | 编辑 | 重绘频道图标，使用更现代的 SVG 风格绘制 |
| `src/ui/CMakeLists.txt` | 编辑 | 添加新源文件和资源文件到构建目标 |

## 5. 实现细节

### 5.1 ThemeManager

```cpp
class ThemeManager {
public:
    static ThemeManager& instance();
    void applyTheme(QApplication* app);
    QColor accentColor() const;
    QColor successColor() const;
    QColor warningColor() const;
    QColor errorColor() const;
    // ... 其他配色访问器
};
```

`applyTheme()` 将加载内嵌的 QSS 资源文件并调用 `app->setStyleSheet()`。同时提供配色常量供 C++ 代码中 `QPainter` 绘制使用，确保 QSS 和代码绘制颜色一致。

### 5.2 QSS 样式表关键规则

#### QWidget 全局基础
```css
QWidget {
    background-color: #1E1E2E;
    color: #C0CAF5;
    font-family: "Segoe UI", "SF Pro Display", "Inter", sans-serif;
    font-size: 13px;
    border: none;
}
```

#### QMainWindow / QDockWidget
- 去掉 `QDockWidget::title` 的渐变背景，使用纯色 `#252535`
- 调整 `QDockWidget::close-button` 和 `float-button` 的 hover 颜色
- 主窗口无边框内阴影，保持扁平

#### QPushButton
```css
QPushButton {
    background-color: #2A2A3C;
    color: #C0CAF5;
    border: 1px solid #3B3B54;
    border-radius: 6px;
    padding: 6px 16px;
}
QPushButton:hover {
    background-color: #31314A;
    border-color: #7AA2F7;
}
QPushButton:pressed {
    background-color: #7AA2F7;
    color: #1E1E2E;
}
```

#### QLineEdit
```css
QLineEdit {
    background-color: #252535;
    color: #C0CAF5;
    border: 1px solid #3B3B54;
    border-radius: 6px;
    padding: 6px 10px;
}
QLineEdit:focus {
    border-color: #7AA2F7;
}
```

#### QTreeView / QListView
- 选中项：`background: #31314A; border-radius: 4px;`
- Hover 项：`background: #2A2A3C;`
- 去除焦点虚线框 (`outline: none`)
- 滚动条窄化、圆角、深色轨道

#### QSlider
```css
QSlider::groove:horizontal {
    height: 4px;
    background: #3B3B54;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    width: 14px;
    height: 14px;
    background: #7AA2F7;
    border-radius: 7px;
    margin: -5px 0;
}
QSlider::sub-page:horizontal {
    background: #7AA2F7;
    border-radius: 2px;
}
```

#### QGroupBox
- 标题使用 `#7AA2F7`，字号略小
- 去掉默认边框或改为 subtle 的 `1px solid #3B3B54`

#### QMenu / QMenuBar
- 菜单背景：`#252535`
- 选中项背景：`#31314A`
- 分隔线：`#3B3B54`

### 5.3 ConnectionBar 重绘细节

状态指示灯从简单的彩色圆点改为**发光环形指示灯**：

```cpp
// 外圈：低透明度的主色
// 内圈：高亮的主色
// 整体带有柔和的发光效果 (shadow/blur)
```

- Disconnected: `#F7768E` (粉红)
- Connecting: `#E0AF68` (琥珀)
- Connected: `#9ECE6A` (绿)

### 5.4 UserListModel 图标重绘

使用 ThemeManager 提供的配色：
- `speaking_icon_`: `#9ECE6A` 带柔和光晕的圆点
- `muted_icon_`: `#F7768E` 麦克风图标（使用 `QPainter` 绘制简洁的麦克风+斜线）
- `deafened_icon_`: `#F7768E` 耳机图标（绘制简洁的耳机+斜线）

### 5.5 ChannelTreeModel 图标重绘

替换 `SP_DirIcon` 为自定义绘制的简约频道图标：
- 使用 `#7AA2F7` 或 `#BB9AF7`
- 绘制一个简化的 "#" 或对话气泡形状

### 5.6 LoginDialog 样式

在 `MainWindow.cpp` 中，LoginDialog 当前使用 QFormLayout。通过设置 `dialog.setStyleSheet(...)` 或让全局 QSS 自动应用，使其具有统一的深色输入框和按钮样式。

## 6. 边界条件与异常处理

1. **Qt6 未找到**：本项目 UI 模块本身就在 `Qt6Widgets_FOUND` 时才构建，ThemeManager 作为 UI 模块的一部分，自然继承此条件。无需额外处理。
2. **QSS 加载失败**：`ThemeManager::applyTheme()` 若无法加载资源文件，将记录 warning 日志并回退到原生样式，不影响程序运行。
3. **平台字体差异**：`font-family` 使用 fallback 列表 (`Segoe UI`, `SF Pro Display`, `Inter`, sans-serif)，确保跨平台可用性。
4. **高 DPI 缩放**：Qt 6 默认启用高 DPI 支持，QSS 中的 `px` 单位会跟随设备像素比自动缩放。自定义绘制的图标使用 `devicePixelRatio` 适配。

## 7. 数据流路径

本方案为**纯表现层修改**，不涉及任何数据流或业务逻辑的变更。

```
[ClientCore callbacks] ---> [MainWindow slots] ---> [UI updates]
                                         ^
                                         |
                              (表现层样式变化，数据不变)
```

唯一新增的数据流是 `main.cpp` 初始化时调用 `ThemeManager::instance().applyTheme(&app)`，将 QSS 注入 QApplication。

## 8. 预期成果

1. 客户端启动后呈现统一的深色现代界面，所有控件（按钮、输入框、树形视图、列表、滑块、菜单）风格一致
2. 状态指示灯更精致，具有辨识度
3. 用户列表和频道列表的图标更符合现代审美
4. 所有现有功能（连接、断开、加入频道、音频设置、登录等）保持完整可用
5. 窗口尺寸和布局保持原有结构，用户学习成本为零
6. 不引入新的外部依赖，仅使用 Qt 6 内置功能

## 9. 兼容性说明

- 仅修改 `src/ui/` 模块，不触及 `src/core/`、`src/network/`、`src/client/`、`src/server/`
- 不修改任何 CMake 中的依赖查找逻辑
- 新增代码全部使用标准 C++20 和 Qt 6 API
