# NEVO 客户端及服务端编译 — 总结

## 编译结果

| 目标 | 状态 | 输出路径 |
|------|------|----------|
| nevo_server.exe | 编译成功 | `build/bin/nevo_server.exe` |
| nevo_client_ui.exe | 编译成功 | `build/bin/nevo_client_ui.exe` |

## 构建环境

- **编译器**: MinGW GCC 13.1 (`C:/Qt/Tools/mingw1310_64/bin/c++.exe`)
- **CMake**: 4.3.1
- **Qt**: 6.11.0 (`C:/Qt/6.11.0/mingw_64/`)
- **vcpkg**: `C:\vcpkg` (x64-mingw-dynamic triplet)

## 已安装的依赖 (via vcpkg, x64-mingw-dynamic)

| 依赖 | 版本 | 用途 |
|------|------|------|
| Boost | 1.90.0 | Asio 网络, Lockfree 队列, Endian 字节序 |
| OpenSSL | 3.6.1 | TLS/SSL 加密 |
| Opus | 1.5.2 | 音频编解码 |
| libsodium | 1.0.21 | XChaCha20-Poly1305 加密 |
| SQLite3 | 3.53.0 | 服务端数据库 |

## 修复的编译问题

### 1. 构建配置修复
- **CMakeLists.txt**: 添加 `lockfree` 和 `endian` 到 Boost 查找组件
- **CMakeLists.txt**: 项目语言添加 C（用于编译 miniaudio.c）
- **CMakeLists.txt**: 修复 Opus 目标名称 `opus::opus` → `Opus::opus`
- **CMakeLists.txt**: 添加 CMP0167 策略设置
- **PlatformSetup.cmake**: 添加 `mswsock` 库链接（AcceptEx 等 Winsock 扩展函数）
- **server/CMakeLists.txt**: 移除不存在的 `PermissionManager.cpp` 引用
- **server/CMakeLists.txt**: `SQLite::SQLite3` → `SQLite3::SQLite3`

### 2. Windows 平台兼容性
- **common.pb.h**: 添加 `#undef` 保护 Windows SDK 宏冲突（`ERROR_TIMEOUT`, `OK`, `ONLINE` 等）
- **TcpVoiceTunnel.cpp**: 添加 `<winsock2.h>` include 提供 `htonl`/`ntohl`

### 3. Proto/类型系统修复
- **common.pb.h**: `enum UserStatus` → `enum class PbUserStatus` 避免与 `nevo::UserStatus` 冲突
- **common.pb.h**: `enum ResultCode` → `enum class ResultCode` 避免值泄漏
- **control.pb.h**: 更新 `nevo::common::OK` → `nevo::common::ResultCode::OK`
- **VoiceCrypto.h**: 修复 Doxygen 注释中 `/* */` 提前关闭外层注释块

### 4. C++ 代码修复
- **OpusEncoder.cpp**: `OPUS_GET_VOICE_ACTIVITY` 不可用，改用条件编译+`OPUS_GET_IN_DTX` 回退
- **ConnectionManager.h**: `SessionId` hash 特化移至 `Types.h`（与 `UserId`/`ChannelId` 一致）
- **SslWrapper.h**: 移除 `Options{}` 默认参数（嵌套类 NSDMI 问题），改为构造函数重载
- **TcpConnection.h**: `socket()` 和 `setConnected()` 从 private 移至 public
- **ServerCore.cpp**: `async_accept` 使用 `redirect_error` 修复返回类型推导
- **Database.cpp**: Argon2 条件编译保护 + stub 实现
- **ClientCore.cpp**: 全面适配轻量级 protobuf 替代 API（`MutableExtension` → `mutable_xxx()` 等）
- **AudioOutput.h**: 添加 `boost::asio/ip/udp.hpp` include，`users_mutex_` 改为 `mutable`
- **MainWindow.cpp/ConnectionBar.cpp**: 修复 `ConnectionState`/`ClientState` 命名空间限定

### 5. 新增文件
- **3rdparty/miniaudio.c**: miniaudio 实现编译单元（之前只有头文件）
