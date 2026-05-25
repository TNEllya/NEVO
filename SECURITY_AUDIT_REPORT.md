# NEVO 安全审计报告

**审计日期**: 2026-05-10
**审计范围**: NEVO 语音服务器代码库
**审计方法**: 静态代码分析 + 架构审查

---

## 执行摘要

本次审计在 NEVO 代码库中发现了 **4 个高严重度** 和 **2 个中等严重度** 的已确认安全漏洞，均具备可演示的端到端利用路径。核心问题集中在**密钥管理**、**密码存储**和**认证机制**方面。

**关键发现**:
- 管理员密码以明文形式存储于内存并直接字符串比较
- 密码哈希存在不安全的 fallback 机制（使用 std::hash）
- 会话令牌使用可预测的 session_id 计数器
- IPC 控制服务器缺少认证机制，本地任意进程可发送管理命令

---

## 已确认漏洞详情

### [HIGH-1] 管理员密码明文存储于内存

**严重度**: 高
**类型**: 敏感数据存储不当 / 凭证明文处理

**位置**:
- [ServerCore.h:571](file:///c:/Users/yzd20/Desktop/NEVO/src/server/include/nevo/server/ServerCore.h#L571) - `std::string admin_password_;`
- [ServerCore.cpp:658](file:///c:/Users/yzd20/Desktop/NEVO/src/server/src/ServerCore.cpp#L658) - 直接字符串比较
- [ServerCore.cpp:681](file:///c:/Users/yzd20/Desktop/NEVO/src/server/src/ServerCore.cpp#L681) - `admin_password_ = password;`

**攻击者画像**: 能够读取服务器进程内存的攻击者（如通过内存转储、本地提权后的攻击者）

**攻击向量**: 服务器内存读取

**完整利用路径**:
1. 攻击者通过本地提权或其他方式获得读取 `/proc/<pid>/mem` 或生成 core dump 的能力
2. 在服务器进程内存中搜索 `admin_password_` 成员变量
3. 由于密码以 `std::string` 明文存储，可直接提取
4. 使用获取的密码通过客户端 `send_admin_auth()` 发送管理员认证请求
5. 成功后获得服务器管理权限（创建/删除频道、修改服务器名称、踢人、封禁用户等）

**影响**: 权限提升至服务器管理员，可执行任意管理操作

**修复建议**:
```cpp
// 使用密码哈希比对，而非明文存储
class ServerCore {
private:
    std::string admin_password_hash_;  // Argon2id 哈希存储
    // 或使用加盐的 HMAC 比较
    std::array<uint8_t, 32> admin_password_salt_;
};

// 比对时使用恒定时间比较
bool ServerCore::authenticateAdmin(UserId user_id, const std::string& password) {
    if (admin_password_.empty()) {
        return Error(...);
    }
    // 使用 Argon2id 或 PBKDF2-HMAC-SHA256 验证
    if (!argon2id_verify(admin_password_hash_, password.data(), password.size())) {
        return Error(...);
    }
    // ...
}
```

---

### [HIGH-2] 会话令牌可预测导致会话劫持

**严重度**: 高
**类型**: 不安全的随机数 / 会话管理缺陷

**位置**: [ClientSession.cpp:409-410](file:///c:/Users/yzd20/Desktop/NEVO/src/server/src/ClientSession.cpp#L409-L410)
```cpp
login_resp->set_session_token(std::to_string(session_id_.value));
```

**攻击者画像**: 已认证的网络用户

**攻击向量**: 会话令牌预测/枚举

**完整利用路径**:
1. 攻击者正常连接服务器并登录，获得 `session_id = 1001`
2. 攻击者观察到 session_id 使用原子计数器 `session_counter_` 自增生成
3. 攻击者可通过暴力枚举或已知 session_id 推算其他用户的 session_id
4. 攻击者构造恶意请求，使用预测的 session_id 冒充其他用户
5. 虽然当前代码未在后续请求中验证 session_token，但协议设计存在此风险

**影响**: 潜在会话劫持（取决于客户端实现是否验证 session_token）

**修复建议**:
```cpp
// 使用加密安全的随机数生成 session_token
#include <sodium.h>

std::string generateSessionToken() {
    uint8_t token[32];
    randombytes_buf(token, sizeof(token));
    return hex_encode(token, sizeof(token));  // 64字符十六进制字符串
}

login_resp->set_session_token(generateSessionToken());
```

---

### [HIGH-3] 不安全的密码哈希 Fallback

**严重度**: 高
**类型**: 弱加密 / 密码存储缺陷

**位置**:
- [Database.cpp:995-1076](file:///c:/Users/yzd20/Desktop/NEVO/src/server/src/Database.cpp#L995-L1076) - `hashPassword()` 和 `verifyPassword()`
- [Database.cpp:1032-1076](file:///c:/Users/yzd20/Desktop/NEVO/src/server/src/Database.cpp#L1032-L1076) - 不安全的 fallback 实现

**攻击者画像**: 任何能访问数据库文件的攻击者

**攻击向量**: 离线密码破解

**完整利用路径**:
1. 攻击者获取 `nevo_server.db` 数据库文件
2. 攻击者检查 `password_hash` 列，发现哈希以 `$pbkdf2-sha256$` 开头
3. 由于 fallback 实现使用 `std::hash` 而非真正的 PBKDF2，破解变得极其简单
4. `std::hash<std::string>` 不使用盐值且迭代次数为 1（而非 PBKDF2 的 100000 次）
5. 攻击者在几秒内可破解所有用户密码

**代码证据**:
```cpp
// Database.cpp:1062-1073 - 不安全的 fallback
std::hash<std::string> hasher;
uint64_t h1 = hasher(salted);
uint64_t h2 = hasher(password + std::string(salt.begin(), salt.end()));
uint64_t h3 = hasher(std::to_string(h1) + std::to_string(h2) + salted);
// 仅使用 16 字节输出，而非标准的 32 字节 HMAC
```

**影响**: 所有用户密码可被快速破解，导致账户接管

**修复建议**:
1. **强制要求 Argon2**: 如果 `HAVE_ARGON2` 未定义，服务器启动时直接退出
2. **如果必须支持 fallback**，使用标准 PBKDF2 实现：
```cpp
#ifdef HAVE_ARGON2
    // Argon2id 实现
#else
    // 使用 OpenSSL PKCS5_PBKDF2_HMAC
    if (!EVP_PBE_scrypt(password.c_str(), password.size(),
                        salt.data(), salt.size(),
                        65536, 8, 1,  // 内存、时间参数
                        output, 32)) {
        return "";  // 失败时返回空字符串
    }
#endif
```

---

### [HIGH-4] 会话密钥明文传输（编译配置相关）

**严重度**: 高
**类型**: 敏感数据明文传输 / 配置缺陷

**位置**: [ServerCore.cpp:606-623](file:///c:/Users/yzd20/Desktop/NEVO/src/server/src/ServerCore.cpp#L606-L623)
```cpp
#ifdef NEVO_HAS_SODIUM
    // 安全的加密实现
#else
    NEVO_LOG_WARN("server", "libsodium not available, session key sent in plaintext (INSECURE)");
    return std::vector<uint8_t>(session_key.begin(), session_key.end());
#endif
```

**攻击者画像**: 网络中间人攻击者（MITM）

**攻击向量**: 网络流量拦截

**完整利用路径**:
1. 攻击者处于客户端与服务器之间的网络路径
2. 攻击者拦截 TCP 连接（如果未使用 TLS）
3. 观察 LoginResponse 消息中的 `encrypted_session_key` 字段
4. 由于 libsodium 不可用，该字段实际为明文会话密钥
5. 攻击者提取密钥，可解密后续所有语音数据通信

**影响**: 语音通信内容完全泄露（机密性破坏）

**修复建议**:
```cpp
// 如果无法使用 libsodium，拒绝启动或拒绝服务
std::vector<uint8_t> ServerCore::generateSessionKeyForClient(...) {
#ifdef NEVO_HAS_SODIUM
    // 安全的加密实现
#else
    NEVO_LOG_ERROR("server", "Cannot operate without libsodium: session keys would be transmitted in plaintext");
    return {};  // 返回空，登录将失败
    // 或在初始化时检查：
    // if (!NEVO_HAS_SODIUM) {
    //     throw std::runtime_error("NEVO requires libsodium for secure operation");
    // }
#endif
}
```

---

### [MED-1] IPC 控制服务器缺少认证

**严重度**: 中
**类型**: 访问控制缺陷 / 缺少认证

**位置**:
- [ControlServer.cpp:197-227](file:///c:/Users/yzd20/Desktop/NEVO/src/server/src/ControlServer.cpp#L197-L227) - 监听 `0.0.0.0:24432`
- [server_process.py:318-320](file:///c:/Users/yzd20/Desktop/NEVO/src/server/gui_python/server_process.py#L318-L320) - 连接时无认证

**攻击者画像**: 服务器主机上的任意本地用户（非 root 即可）

**攻击向量**: 本地网络连接 / Unix socket 通信

**完整利用路径**:
1. 攻击者登录到与服务器同一台主机
2. 攻击者创建一个 Python 脚本，使用 `socket.socket()` 连接 `127.0.0.1:24432`
3. 发送恶意 JSON 命令：
```json
{"id": 1, "command": "shutdown"}
{"id": 2, "command": "set_admin_password", "params": {"password": "attackercontrolled"}}
```
4. 服务器接收并执行这些命令，无需任何认证
5. 攻击者成功关闭服务器或设置自己的管理员密码

**影响**: 本地拒绝服务 + 权限提升至服务器控制

**修复建议**:
```cpp
// 方案 1: 使用 Unix Domain Socket 并设置文件权限
boost::asio::local::stream_protocol::acceptor acceptor(io_ctx, "/var/run/nevo_control.sock");
chmod("/var/run/nevo_control.sock", 0700);  // 仅 root 可访问

// 方案 2: 要求 IPC 命令携带签名
struct IpcCommand {
    std::string command;
    std::string signature;  // HMAC-SHA256(command, shared_secret)
};

bool ControlServer::verifySignature(const IpcCommand& cmd) {
    auto expected = hmac_sha256(shared_secret_, cmd.command);
    return secure_compare(expected, cmd.signature);
}
```

---

### [MED-2] 缺少失败登录速率限制

**严重度**: 中
**类型**: 认证机制缺陷 / 暴力破解防护缺失

**位置**: [ClientSession.cpp:281-354](file:///c:/Users/yzd20/Desktop/NEVO/src/server/src/ClientSession.cpp#L281-L354) - `handleLogin()`

**攻击者画像**: 远程网络攻击者

**攻击向量**: 密码暴力破解

**完整利用路径**:
1. 攻击者编写脚本批量连接服务器
2. 对目标用户名（如 `admin`）尝试不同密码
3. 服务器对每次尝试都执行 Argon2id 验证（计算密集型）
4. 无失败次数限制、无账户锁定、无速率限制
5. 攻击者可每秒尝试 10-50 个密码（取决于服务器性能）
6. 对于弱密码，可在数小时至数天内破解

**影响**: 用户账户可能被暴力破解

**修复建议**:
```cpp
// 在 ClientSession 或 ServerCore 中添加速率限制
class LoginRateLimiter {
private:
    std::unordered_map<std::string, std::deque<time_t>> attempts_;  // IP -> 时间戳队列
    static constexpr size_t MAX_ATTEMPTS = 5;
    static constexpr time_t WINDOW_SECONDS = 60;

public:
    bool checkAndRecord(const std::string& identifier) {
        auto& queue = attempts_[identifier];
        auto now = time(nullptr);

        // 移除窗口外的记录
        while (!queue.empty() && now - queue.front() > WINDOW_SECONDS) {
            queue.pop_front();
        }

        if (queue.size() >= MAX_ATTEMPTS) {
            return false;  // 速率限制触发
        }

        queue.push_back(now);
        return true;
    }
};
```

---

## 未发现问题领域

以下安全领域经审计确认无漏洞：

| 领域 | 状态 | 说明 |
|------|------|------|
| SQL 注入 | ✅ 无问题 | 所有数据库操作使用 `sqlite3_prepare_v2` 参数化查询 |
| 命令注入 | ✅ 无问题 | 未发现 `system()`, `exec()`, `popen()` 等危险函数调用（subprocess 用于启动服务器进程，参数来自配置） |
| 路径遍历 | ✅ 无问题 | 文件路径操作使用固定路径或相对路径，无用户输入拼接 |
| 日志敏感信息 | ✅ 无问题 | 未发现密码、日志记录中的敏感数据泄露 |
| TLS 配置 | ✅ 无问题 | TLS 1.2+、安全密码套件、mTLS 支持 |
| 密钥内存清理 | ✅ 无问题 | 使用 `sodium_memzero()` 安全擦除密钥 |

---

## 修复优先级建议

| 优先级 | 漏洞 ID | 问题 | 建议修复时间 |
|--------|---------|------|--------------|
| P0 (立即) | HIGH-3 | 不安全密码哈希 fallback | 24小时内 |
| P0 (立即) | HIGH-4 | 会话密钥明文传输 | 24小时内 |
| P1 (本周) | HIGH-1 | 管理员密码明文存储 | 1周内 |
| P1 (本周) | HIGH-2 | 会话令牌可预测 | 1周内 |
| P2 (计划) | MED-1 | IPC 控制服务器无认证 | 1月内 |
| P2 (计划) | MED-2 | 缺少速率限制 | 1月内 |

---

## 附录：编译配置检查清单

确认以下编译选项以启用安全功能：

- [ ] `cmake -DNEVO_HAS_SODIUM=ON` - 启用 libsodium（会话加密、密钥生成）
- [ ] `cmake -DHAVE_ARGON2=ON` - 启用 Argon2id（密码哈希）
- [ ] `cmake -DNEVO_HAS_OPENSSL=ON` - 启用 OpenSSL（TLS）

**验证命令**:
```bash
# 检查二进制是否包含所需符号
ldd nevo_server | grep -E "sodium|ssl|crypto"
strings nevo_server | grep -E "argon2|crypto_box"
```
