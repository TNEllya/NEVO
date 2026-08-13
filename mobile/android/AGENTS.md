# mobile/android — 模块 Agent 指令

## 模块职责

NEVO Android 客户端，使用 Kotlin + Jetpack Compose 构建的独立移动端应用：

- **core/** — 底层能力（音频引擎、加密、数据库、网络、协议序列化）
- **feature/connection/** — 服务器连接、认证、重连策略
- **feature/channel/** — 频道列表与交互
- **feature/chat/** — 文字聊天
- **feature/screen_share/** — 屏幕共享
- **feature/settings/** — 应用设置

## 技术栈

| 组件 | 技术 |
|------|------|
| UI | Jetpack Compose（Material 3） |
| DI | Hilt |
| 数据库 | Room（KSP 注解处理） |
| 网络 | 自定义 TCP/UDP（Protobuf 二进制协议） |
| 加密 | libsodium（通过 CryptoManager 封装） |
| 最低 SDK | 26（Android 8.0） |
| 目标 SDK | 35 |

## 构建

独立 Gradle 构建，**不依赖**主工程 CMake：

```bash
cd mobile/android
./gradlew assembleDebug        # 调试包
./gradlew assembleRelease      # 发布包（启用 R8 混淆）
```

## 测试

```bash
cd mobile/android
./gradlew test                 # 单元测试
./gradlew connectedAndroidTest # 设备/模拟器集成测试
```

## 模块约束

1. **协议兼容**：`core/protocol/` 中的消息类型和序列化逻辑必须与 `proto/*.proto` 定义保持一致；协议变更需先修改 proto 再同步到 Kotlin
2. **独立构建**：本模块不参与根目录 CMake 构建，不可引入对 `src/` 的编译时依赖
3. **加密安全**：`CryptoManager` / `VoiceCryptoState` 变更需安全审查，密钥轮换逻辑不可降级
4. **Compose 规范**：UI 层使用 Compose 声明式范式，不引入传统 View 体系（XML layout）
5. **ProGuard 规则**：Release 构建启用 R8，新增反射/序列化类需更新 `proguard-rules.pro`
6. **权限最小化**：`AndroidManifest.xml` 权限声明遵循最小必要原则，新增权限需说明理由
