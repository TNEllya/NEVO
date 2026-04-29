# 一客户端一密钥架构实施计划

**目标：** 为 NEVO 实现每个客户端独立的会话密钥，客户端生成非对称密钥对，服务端使用客户端公钥加密下发的会话密钥。

**架构：**
- 客户端首次运行时生成 Curve25519 密钥对（`crypto_box_keypair`），私钥本地持久化（QSettings），公钥注册到服务端
- 服务端在 `users` 表中存储客户端公钥，登录时为每个客户端生成独立的 32 字节对称会话密钥
- 会话密钥通过 `crypto_box_seal`（客户端公钥加密）下发，客户端通过 `crypto_box_seal_open` 解密
- 服务端 `AudioRelay` 解密发送者语音包后，使用每个接收者的独立密钥重新加密并转发
- 密钥轮换改为服务端为每个客户端单独生成新密钥并逐一推送

**技术栈：** libsodium (`crypto_box_keypair`, `crypto_box_seal`, `crypto_box_seal_open`), SQLite3, Protobuf, Qt QSettings

---

## 文件变更清单

| 文件 | 操作 | 说明 |
|---|---|---|
| `proto/control.proto` | 修改 | LoginResponse 新增 `encrypted_session_key` |
| `src/server/include/nevo/server/Database.h` | 修改 | UserRecord 新增 public_key，新增 updateUserPublicKey() |
| `src/server/src/Database.cpp` | 修改 | 表结构迁移，实现公钥读写方法 |
| `src/server/include/nevo/server/ServerCore.h` | 修改 | 新增 per-client 密钥管理 |
| `src/server/src/ServerCore.cpp` | 修改 | 密钥生成、轮换、查询接口 |
| `src/server/include/nevo/server/ClientSession.h` | 修改 | 新增 clientPublicKey() 访问 |
| `src/server/src/ClientSession.cpp` | 修改 | 处理 login 中的公钥交换 |
| `src/server/include/nevo/server/AudioRelay.h` | 修改 | 新增 per-client VoiceCrypto 管理 |
| `src/server/src/AudioRelay.cpp` | 修改 | 实现解密-重加密转发逻辑 |
| `src/client/include/nevo/client/ClientCore.h` | 修改 | 新增客户端密钥对管理 |
| `src/client/src/ClientCore.cpp` | 修改 | 生成/加载密钥对，发送公钥，解密会话密钥 |
| `src/network/include/nevo/network/VoiceCrypto.h` | 修改 | 新增 decryptWithKey 公开静态方法 |
| `src/network/src/VoiceCrypto.cpp` | 修改 | 公开 decryptWithKey |

---

## Task 1: 协议更新 — LoginResponse 新增加密会话密钥字段

**文件：** `proto/control.proto`

在 `LoginResponse` 中新增 `bytes encrypted_session_key = 6;`，用于服务端下发经 `crypto_box_seal` 加密的会话密钥。

## Task 2: 数据库层 — 用户表添加公钥字段

**文件：**
- `src/server/include/nevo/server/Database.h`
- `src/server/src/Database.cpp`

1. `users` 表添加 `public_key BLOB` 列（使用 `ALTER TABLE ADD COLUMN IF NOT EXISTS` 兼容旧数据库）
2. `UserRecord` 新增 `std::vector<uint8_t> public_key`
3. 新增 `Result<void> updateUserPublicKey(UserId user_id, const std::vector<uint8_t>& public_key)`
4. `getUser` / `getUserByName` 读取 public_key 字段

## Task 3: 客户端密钥对生成与本地存储

**文件：**
- `src/client/include/nevo/client/ClientCore.h`
- `src/client/src/ClientCore.cpp`

1. 新增成员：
   ```cpp
   std::array<uint8_t, crypto_box_SECRETKEYBYTES> client_secret_key_{};
   std::array<uint8_t, crypto_box_PUBLICKEYBYTES> client_public_key_{};
   bool has_identity_keypair_ = false;
   ```
2. `loadIdentityKeys()`：从 QSettings (`identity/secretKey`, `identity/publicKey`) 加载，Base64 编码存储
3. `generateIdentityKeys()`：调用 `crypto_box_keypair()`，保存到 QSettings
4. `ensureIdentityKeys()`：启动时调用，若不存在则生成
5. `clientPublicKey()`：返回公钥指针

## Task 4: 服务端登录流程 — 接收公钥、生成并加密会话密钥

**文件：**
- `src/server/src/ClientSession.cpp`
- `src/server/include/nevo/server/ServerCore.h`
- `src/server/src/ServerCore.cpp`

1. `ClientSession::handleLogin()`：
   - 提取 `login_request.client_public_key()`
   - 若长度 == 32，存入数据库 `db_->updateUserPublicKey(user_id, pubkey)`
   - 调用 `server_core_->generateSessionKeyForClient(user_id)`
   - 从 ServerCore 获取加密后的会话密钥，写入 `login_resp->set_encrypted_session_key()`
   - 不再直接发送原始 `server_public_key`

2. `ServerCore` 新增：
   ```cpp
   std::unordered_map<UserId, std::array<uint8_t, CRYPTO_KEY_SIZE>> client_session_keys_;
   mutable std::mutex client_keys_mutex_;
   ```
3. `ServerCore::generateSessionKeyForClient(UserId user_id, const std::vector<uint8_t>& client_public_key)`：
   - 生成随机 32 字节会话密钥（`randombytes_buf`）
   - 使用 `crypto_box_seal` 加密会话密钥
   - 保存明文密钥到 `client_session_keys_[user_id]`
   - 返回加密后的密文（vector<uint8_t>）
4. `ServerCore::getClientSessionKey(UserId)`：查询客户端明文密钥

## Task 5: 客户端登录流程 — 发送公钥、解密会话密钥

**文件：** `src/client/src/ClientCore.cpp`

1. `connect()` 构建 `LoginRequest` 时：
   - 调用 `ensureIdentityKeys()`
   - 填充 `login_request.set_client_public_key(client_public_key_.data(), client_public_key_.size())`
2. `handleLoginResponse()`：
   - 若 `login_response.encrypted_session_key()` 存在且长度 > 0：
     - 调用 `crypto_box_seal_open` 解密（需要 client_public_key_ + client_secret_key_）
     - 将解密后的 32 字节传入 `network_mgr_->setSessionKey()`
   - 兼容旧服务端：若 `encrypted_session_key` 为空但 `server_public_key` 存在，按旧逻辑处理

## Task 6: VoiceCrypto — 公开 decryptWithKey

**文件：**
- `src/network/include/nevo/network/VoiceCrypto.h`
- `src/network/src/VoiceCrypto.cpp`

将 `decryptWithKey` 从 `private` 改为 `public static`，供 AudioRelay 直接使用（无需构造 VoiceCrypto 实例即可用任意密钥解密）。

## Task 7: AudioRelay — 实现逐客户端解密重加密

**文件：**
- `src/server/include/nevo/server/AudioRelay.h`
- `src/server/src/AudioRelay.cpp`

1. 新增成员：
   ```cpp
   std::unordered_map<UserId, std::unique_ptr<VoiceCrypto>> client_cryptos_;
   std::shared_ptr_ptr<ServerCore> server_core_; // 或仅保存密钥查询回调
   ```
2. 新增 `setServerCore(std::shared_ptr_ptr<ServerCore> core)` 或改为保存 `std::function<std::optional<std::array<uint8_t, CRYPTO_KEY_SIZE>>(UserId)> key_query_`
3. `addClientMapping()`：为该用户创建 `VoiceCrypto` 实例并设置其会话密钥
4. `removeClientMapping()`：销毁该用户的 `VoiceCrypto`
5. `handleVoicePacket()` 逻辑更新：
   - 解析 VoicePacketHeader，获取 sender_id 和 channel_id
   - 用 `VoiceCrypto::decryptWithKey(sender_key, ...)` 解密载荷
   - 若解密成功，遍历同频道其他用户：
     - 用 `client_cryptos_[receiver_id]->encrypt(plaintext, ...)` 重新加密
     - 组装新数据包并发送
   - 若解密失败，丢弃包（防篡改）

## Task 8: 密钥轮换适配

**文件：**
- `src/server/src/ServerCore.cpp`
- `proto/control.proto`
- `src/client/src/ClientCore.cpp`

1. `KeyRotationRequest` 新增 `bytes encrypted_session_key = 3;`
2. `ServerCore::rotateSessionKey()`：
   - 对每个已认证客户端，生成新密钥并用其公钥加密
   - 通过 `ClientSession::sendControl()` 逐一发送（非广播）
3. `ClientCore::handleKeyRotation()`：
   - 若存在 `encrypted_session_key`，用私钥解密后调用 `rotateKey()`
   - 兼容旧服务端：若只有 `new_server_public_key`，按旧逻辑处理

## Task 9: 构建与验证

1. 重新生成 Protobuf：`cmake --build build --target protobuf_generate`
2. 完整构建
3. 运行服务端/客户端测试连接流程
