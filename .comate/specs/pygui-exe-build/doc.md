# PyGUI EXE Build - doc.md

## Requirement

将 NEVO 的 Python GUI 应用（Server GUI 和 Client GUI）使用 PyInstaller 打包为独立的 .exe 可执行文件，使用户无需安装 Python 环境即可运行。

## Architecture

- **工具**: PyInstaller 6.17.0（已安装）
- **模式**: `--onedir`（目录模式，启动更快、兼容性更好）
- **输出**: 两个独立的分发目录
  - `dist/nevo_server_gui/` → `nevo_server_gui.exe`
  - `dist/nevo_client_gui/` → `nevo_client_gui.exe`

## Key Technical Considerations

### 1. PyQt-Fluent-Widgets 隐式依赖
qfluentwidgets 大量使用延迟导入（lazy import），PyInstaller 的静态分析无法检测到这些模块。必须在 spec 文件中显式声明 hidden imports。

核心需要包含的模块：
- `qfluentwidgets._rc` — 资源文件（图标、样式等）
- `qfluentwidgets.common.*` — 动画、颜色、字体、图标、样式
- `qfluentwidgets.components.*` — 所有 UI 组件
- `qfluentwidgets.window.*` — FluentWindow 等

### 2. 路径处理
当 PyInstaller 打包后，`__file__` 和 `sys.executable` 的含义变化：
- `sys._MEIPASS` 指向解压临时目录（onefile）或应用目录（onedir）
- 需要修改 `run_server.py` 和 `run_client.py` 中的 C++ exe 自动检测逻辑，使用 `sys._MEIPASS` 或 exe 同级目录

### 3. UPX 压缩
如果系统有 UPX，PyInstaller 会自动使用。Windows 上通常没有，跳过即可。

### 4. 控制台窗口
GUI 应用不应显示控制台窗口，使用 `--noconsole` / `--windowed` 选项。

## Affected Files

| File | Modification |
|------|-------------|
| `pygui/run_server.py` | 修改路径检测逻辑，支持 PyInstaller frozen 状态 |
| `pygui/run_client.py` | 修改路径检测逻辑，支持 PyInstaller frozen 状态 |
| `pygui/build_server.spec` | 新建 - PyInstaller spec 文件 |
| `pygui/build_client.spec` | 新建 - PyInstaller spec 文件 |
| `pygui/build_exe.py` | 新建 - 一键构建脚本 |

## Implementation Details

### 1. 修改 run_server.py 路径检测

```python
def get_application_path():
    """Get the application root path, works both in dev and PyInstaller frozen mode."""
    if getattr(sys, 'frozen', False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(__file__))
```

自动检测 C++ server exe 逻辑：
- frozen 模式下：在 exe 同级目录查找 `nevo_server.exe`
- 开发模式下：在 `../../build/bin/` 查找

### 2. 修改 run_client.py 路径检测

同上，查找 `nevo_client_bridge.exe`。

### 3. PyInstaller spec 文件

**build_server.spec**:
```python
a = Analysis(
    ['run_server.py'],
    pathex=[],
    binaries=[],
    datas=[],
    hiddenimports=[
        'qfluentwidgets._rc',
        'qfluentwidgets.common',
        'qfluentwidgets.common.icon',
        'qfluentwidgets.common.config',
        'qfluentwidgets.common.style_sheet',
        'qfluentwidgets.common.translator',
        'qfluentwidgets.components',
        'qfluentwidgets.components.widgets',
        'qfluentwidgets.components.dialog_box',
        'qfluentwidgets.components.navigation',
        'qfluentwidgets.components.settings',
        'qfluentwidgets.components.tree',
        'qfluentwidgets.components.table',
        'qfluentwidgets.components.menu',
        'qfluentwidgets.window',
        'qfluentwidgets.window.fluent_window',
    ],
    ...
)
```

**build_client.spec**: 类似，hidden imports 相同。

### 4. 一键构建脚本 build_exe.py

提供简单的命令行接口：
```bash
python build_exe.py          # 构建两个 exe
python build_exe.py --server # 仅构建 server
python build_exe.py --client # 仅构建 client
python build_exe.py --clean  # 清理后重新构建
```

## Expected Outcomes

1. 运行 `python build_exe.py` 后，生成：
   - `pygui/dist/nevo_server_gui/nevo_server_gui.exe`
   - `pygui/dist/nevo_client_gui/nevo_client_gui.exe`
2. 双击 .exe 即可启动对应的 Fluent Design GUI
3. 将 C++ 编译的 `nevo_server.exe` 放在 server gui 同级目录即可自动关联
4. 不显示控制台窗口
