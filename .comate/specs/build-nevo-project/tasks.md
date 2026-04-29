# NEVO 客户端及服务端编译任务计划

- [x] Task 1: 安装 MinGW 兼容的 Boost 依赖
    - 1.1: 通过 vcpkg 安装 boost-asio:x64-mingw-dynamic (自动拉取 boost-system 等依赖)
    - 1.2: 通过 vcpkg 安装 boost-lockfree:x64-mingw-dynamic
    - 1.3: 确认 Boost 安装成功

- [x] Task 2: 安装其他 MinGW 兼容的可选依赖
    - 2.1: 安装 opus:x64-mingw-dynamic
    - 2.2: 安装 openssl:x64-mingw-dynamic
    - 2.3: 安装 sqlite3:x64-mingw-dynamic
    - 2.4: 确认所有依赖安装成功

- [x] Task 3: 清理旧构建缓存并重新配置 CMake
    - 3.1: 清理 build/ 目录下的旧 CMake 缓存
    - 3.2: 使用 vcpkg 工具链和 MinGW 生成器配置 CMake，指定 Qt6 路径和 Boost 路径
    - 3.3: 验证 CMake 配置输出中所有依赖状态正确

- [x] Task 4: 编译服务端 nevo_server
    - 4.1: 执行 cmake --build 编译 nevo_server 目标
    - 4.2: 验证 nevo_server 可执行文件生成

- [x] Task 5: 编译客户端 nevo_client_ui
    - 5.1: 执行 cmake --build 编译 nevo_client_ui 目标
    - 5.2: 验证 nevo_client_ui 可执行文件生成

- [ ] Task 6: 修复编译错误（如有）
    - 6.1: 分析编译错误信息
    - 6.2: 修复源代码或构建配置问题
    - 6.3: 重新编译直到成功
