# PyGUI EXE Build - 任务计划

- [x] Task 1: 修改入口文件路径检测逻辑
    - 1.1: 修改 `run_server.py` — 添加 `get_application_path()` 函数，支持 PyInstaller frozen 模式下的路径检测
    - 1.2: 修改 `run_client.py` — 同上，添加 frozen 模式路径支持

- [x] Task 2: 创建 PyInstaller spec 文件
    - 2.1: 创建 `build_server.spec` — Server GUI 的 PyInstaller 配置，包含 hidden imports、noconsole、onedir 模式
    - 2.2: 创建 `build_client.spec` — Client GUI 的 PyInstaller 配置，同上

- [x] Task 3: 创建一键构建脚本
    - 3.1: 创建 `build_exe.py` — 支持 `--server`/`--client`/`--clean` 参数的构建脚本

- [x] Task 4: 执行构建并验证
    - 4.1: 运行 PyInstaller 构建 Server GUI exe
    - 4.2: 运行 PyInstaller 构建 Client GUI exe
    - 4.3: 验证生成的 .exe 可以正常启动
