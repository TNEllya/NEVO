# NEVO — Agent 指令

## 项目概述

NEVO 是一个多平台 VoIP 语音通信系统，包含 C++ 核心引擎、Kotlin Android 客户端和 Electron Web 客户端（V3）。

## 构建与测试

```bash
# 构建（需要 vcpkg 工具链）
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# 运行测试
cd build && ctest --output-on-failure -C Release
```

## 核心代码边界（高保护）

以下路径为系统核心，变更需要额外谨慎，PR 需要代码所有者审查：

| 路径 | 职责 | 保护原因 |
|------|------|----------|
| `src/core/` | 音频编解码（Opus）、协议编解码、数据模型（Channel/User/Permission）、VAD | 所有平台共享的底层引擎，接口变更影响全局 |
| `src/server/` | 服务器核心、客户端会话管理、音频/视频中继 | 承载所有在线用户的实时通信，错误影响面广 |
| `src/network/` | TCP/UDP 连接管理、NAT 穿透、加密传输（libsodium） | 网络安全与连通性基础，变更可能导致全平台断连 |
| `proto/` | Protobuf 协议定义 | Schema 变更影响所有客户端/服务端兼容性 |

### 核心路径变更规则

1. **不得**在未经审查的情况下修改核心路径的公共接口（头文件中的 public API）
2. **不得**修改 `proto/*.proto` 中已有字段的编号或删除字段（仅可追加）
3. 修改核心路径时必须确认相关测试通过（`tests/core_tests`、`tests/network_tests`、`tests/audio_tests`）
4. 涉及加密或认证逻辑的变更需要安全审查

## 模块结构

```
src/core/       → 核心引擎（无外部依赖，纯逻辑）
src/network/    → 网络传输层（依赖 core）
src/server/     → 服务端（依赖 core + network）
src/client/     → C++ 客户端核心（依赖 core + network）+ Electron 网关 Python 协议库
webclient/      → Electron Web 客户端（V3：electron/ + js/ + gateway.py）
mobile/android/ → Kotlin Android 客户端（独立构建，Gradle）
proto/          → Protobuf 协议定义（所有平台共享）
```

## 约束

- `3rdparty/` 目录为第三方库（spdlog、miniaudio），**不可修改**
- 协议变更流程：修改 `proto/*.proto` → 运行 `python scripts/sync_proto_enums.py` → 重新生成各平台代码
- 桌面 GUI 客户端已下线，客户端仅保留 Electron（`webclient/`）；Electron 网关 `webclient/gateway.py` 复用
  `src/client/gui_python/` 下的 Python 协议库（`nevo_client.py`、`nevo_wire.py`、`voice_crypto.py`、`proto/`），
  删除或改动前必须确认网关打包（`webclient/nevo_gateway.spec`）与链路测试（`test/cross_device_repro/`）不受影响
- Android 客户端使用 Jetpack Compose，独立 Gradle 构建
