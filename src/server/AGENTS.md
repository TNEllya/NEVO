# src/server — 模块 Agent 指令

## 模块职责

NEVO 服务端核心，承载所有在线用户的实时通信：

- **ServerCore** — 服务器生命周期管理、配置加载、组件编排
- **ClientSession** — 客户端会话管理（认证、状态机、心跳）
- **ChannelManager** — 频道创建/销毁/权限控制
- **AudioRelay / VideoRelay** — 音频/视频流中继
- **ControlServer** — TCP 控制通道（Protobuf 协议）
- **Database** — SQLite 持久化（用户、频道、权限）

## 依赖关系

```
nevo_core（编解码、数据模型）
nevo_network（TCP/UDP 传输、加密）
SQLite3、libsodium、argon2（密码哈希）
```

## 构建

服务端随主工程一起构建，无独立构建命令：

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --target nevo_server
```

## 测试

```bash
cd build && ctest --output-on-failure -C Release -R "server"
```

相关测试位于 `tests/server_tests/`：

- `TestServerArgParsing.cpp` — 启动参数解析
- `TestServerIntegration.cpp` — 服务端集成测试
- `TestVoiceRelayIntegration.cpp` — 语音中继集成测试

## 模块约束

1. **高保护路径**：`src/server/` 属于核心代码边界，公共接口（`include/nevo/server/*.h`）变更需要代码所有者审查
2. **会话兼容性**：`ClientSession` 状态机变更必须保持与现有客户端的协议兼容
3. **中继性能**：AudioRelay/VideoRelay 修改需关注延迟与内存，避免引入锁竞争
4. **数据库迁移**：`Database` schema 变更必须向后兼容，使用增量迁移（不可删列/改类型）
5. **加密逻辑**：涉及 libsodium/argon2 的变更需要安全审查
6. **GUI 子模块**：`gui_python/` 为 Python 管理界面（PyQt5），独立打包，不影响 C++ 构建
