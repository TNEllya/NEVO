# NEVO 客户端及服务端编译

## 需求场景
用户要求分析 NEVO 项目架构后，成功编译客户端 (nevo_client_ui) 和服务端 (nevo_server)。

## 项目架构概述
NEVO 是一个 C++20 VoIP 语音聊天应用，采用客户端-服务端架构，CMake 3.21+ 构建。

### 模块依赖关系
```
nevo_client_ui → nevo_client → nevo_network → nevo_core
nevo_server   ──────────────→ nevo_network → nevo_core
```

### 关键模块
| 模块 | 目标 | 路径 | 说明 |
|------|------|------|------|
| core | nevo_core (库) | src/core/ | 音频引擎、协议编解码、数据模型 |
| network | nevo_network (库) | src/network/ | TCP/UDP/SSL连接，依赖 Boost |
| client | nevo_client (库) | src/client/ | 客户端核心，状态机 |
| server | nevo_server (可执行) | src/server/ | 服务端，SQLite数据库 |
| ui | nevo_client_ui (可执行) | src/ui/ | Qt6 Widgets GUI |

### 当前构建环境
- 编译器: MinGW GCC 13.1 (`C:/Qt/Tools/mingw1310_64/bin/c++.exe`)
- CMake: `C:/Program Files/CMake/bin/cmake.exe`
- 之前构建尝试中 Boost 未找到 (`Boost_DIR-NOTFOUND`)
- build/ 目录已存在，包含之前的 CMake 缓存

## 技术方案

### 编译步骤
1. 检查并确认构建依赖是否可用 (Boost, Qt6, Opus, OpenSSL, SQLite3, libsodium)
2. 清理旧的 CMake 缓存，避免残留配置干扰
3. 运行 CMake 配置，根据可用依赖启用/禁用可选功能
4. 执行编译，优先编译服务端 nevo_server，然后编译客户端 nevo_client_ui
5. 如遇编译错误，分析并修复

### 可选依赖处理策略
项目 CMake 已对缺失的可选依赖做了 stub 处理：
- **Opus**: 未找到时使用 stub 编码器
- **libsodium**: 未找到时使用 stub 加密
- **SQLite3**: 未找到时服务端使用 stub 数据库
- **Boost**: 必需 (network/client/server 模块依赖)
- **Qt 6**: 仅 UI 模块必需

### 受影响文件
- `c:\Users\yzd20\Desktop\NEVO\CMakeLists.txt` — 根构建配置 (只读分析)
- `c:\Users\yzd20\Desktop\NEVO\src\*\CMakeLists.txt` — 各子模块构建配置 (只读分析)
- `c:\Users\yzd20\Desktop\NEVO\build\` — 构建输出目录 (将被清理重建)
- 源代码文件 — 仅在编译错误需要修复时修改

## 边界条件与异常处理
- Boost 未安装: 需要安装或确认可用路径，network/client/server 模块无法缺少 Boost
- Qt6 未找到: UI 模块将不编译，但不影响服务端
- 编译错误: 逐个分析并修复，优先确保 core → network → server 编译链通过
- Windows 平台特殊: 需确保 WinSock 链接正确 (PlatformSetup.cmake 已处理)

## 预期结果
- `nevo_server` 可执行文件成功生成
- `nevo_client_ui` 可执行文件成功生成 (如 Qt6 可用)
- 所有必需模块 (nevo_core, nevo_network) 编译通过
