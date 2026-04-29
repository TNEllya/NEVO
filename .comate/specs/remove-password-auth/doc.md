# 移除密码认证功能 — 仅用户名即可加入服务器

## 需求描述

移除客户端的密码输入功能，使用户只需输入用户名即可直接连接并进入服务器。服务端同步修改，对新用户自动注册，对已有用户跳过密码验证直接放行。

## 数据流变更

### 当前流程
```
用户点击连接 → LoginDialog(用户名+密码) → ClientCore::connect(host, port, username, password)
  → 发送 LoginRequest{username, auth_credential=password}
  → 服务端 db_->verifyUser(username, credential) 验证密码
  → 验证失败返回 AuthFailed
```

### 目标流程
```
用户点击连接 → LoginDialog(仅用户名) → ClientCore::connect(host, port, username)
  → 发送 LoginRequest{username, auth_credential=""}
  → 服务端通过 getUserByName(username) 查找用户
     - 用户存在 → 直接登录（跳过密码验证）
     - 用户不存在 → 自动注册（createUser(username, "")）后登录
```

## 受影响文件及修改类型

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `src/ui/include/nevo/ui/MainWindow.h` | 修改 | 移除 `password_edit_` 成员和 `password()` 访问器 |
| `src/ui/src/MainWindow.cpp` | 修改 | LoginDialog 移除密码输入框；两处 connect 调用移除 password 参数 |
| `src/client/include/nevo/client/ClientCore.h` | 修改 | `connect()` 移除 password 参数 |
| `src/client/src/ClientCore.cpp` | 修改 | `connect()` 实现移除 password，发送空 credential |
| `src/server/src/ClientSession.cpp` | 修改 | `handleLogin()` 改为按用户名查找+自动注册，跳过密码验证 |
| `src/ui/translations/nevo_client_en.ts` | 修改 | 移除密码相关翻译条目 |
| `src/ui/translations/nevo_client_zh_CN.ts` | 修改 | 移除密码相关翻译条目 |
| `src/ui/translations/nevo_client_zh_TW.ts` | 修改 | 移除密码相关翻译条目 |

## 详细实现

### 1. LoginDialog 修改 (MainWindow.h + MainWindow.cpp)

**MainWindow.h** — 移除成员和访问器：
- 删除 `QLineEdit* password_edit_` 成员
- 删除 `QString password() const` 方法

**MainWindow.cpp** — LoginDialog 构造函数：
- 删除 `password_edit_(nullptr)` 初始化
- 删除密码输入框创建代码（第89-93行）
- 副标题文字从 "Enter your credentials to join the voice server" 改为 "Enter your username to join the voice server"
- 删除 `LoginDialog::password()` 方法

### 2. MainWindow 连接流程修改 (MainWindow.cpp)

**`onConnectAction()`**：
- 移除 `QString password = dialog.password();`
- lambda 中移除 `password` 捕获和参数传递
- `client_core_->connect(host, port, username)` 只传3个参数

**`onConnectRequested()`**：
- 同上处理，移除 password 相关代码

### 3. ClientCore::connect() 修改 (ClientCore.h + ClientCore.cpp)

**ClientCore.h** — 签名变更：
```cpp
boost::asio::awaitable<Result<void>> connect(
    const std::string& host,
    uint16_t tcp_port,
    const std::string& username);
```

**ClientCore.cpp** — 实现：
- 移除 `const std::string& password` 参数
- `login_req->set_auth_credential(password)` 改为 `login_req->set_auth_credential("")`
- 日志中移除 password 相关信息

### 4. 服务端 handleLogin() 修改 (ClientSession.cpp)

将 `verifyUser(username, credential)` 替换为以下逻辑：

```cpp
// 按用户名查找用户
auto user_record = db_->getUserByName(username);
UserId user_id;

if (user_record) {
    // 用户已存在，直接登录（跳过密码验证）
    user_id = user_record->id;
    NEVO_LOG_INFO("server", "User logged in (no password): {} (id={})", username, user_id.value);
} else {
    // 用户不存在，自动注册
    auto create_result = db_->createUser(username, "");
    if (!create_result) {
        // 返回注册失败响应
        NEVO_LOG_ERROR("server", "Auto-register failed for user: {}", username);
        // ... 发送 ERROR_AUTH_FAILED 响应
        return;
    }
    user_id = create_result.value();
    user_record = db_->getUser(user_id);
    NEVO_LOG_INFO("server", "Auto-registered and logged in: {} (id={})", username, user_id.value);
}
```

后续代码保持不变（检查封禁、设置会话状态、加入默认频道、发送响应）。

### 5. 翻译文件修改

移除以下翻译条目：
- "Enter your credentials to join the voice server" / "Password:" / "Enter password"

新增翻译条目：
- "Enter your username to join the voice server"（替换原副标题）

## 边界条件与异常处理

- 用户名为空：客户端仍检查并拒绝空用户名
- 数据库不可用：服务端返回 ERROR_UNKNOWN（与现有逻辑一致）
- 自动注册失败（如用户名冲突）：返回 AuthFailed
- IP 被封禁：仍检查 IP 封禁逻辑
- 用户级封禁：仍检查用户级封禁逻辑
- 并发同一用户名：createUser 内部有 username 唯一性检查，冲突时返回 InvalidRequest

## 预期结果

- 客户端 LoginDialog 仅显示用户名输入框
- 输入用户名后点击 Connect 即可连接
- 已有用户直接登录，新用户自动注册后登录
- 不再有任何密码相关 UI 和验证逻辑
