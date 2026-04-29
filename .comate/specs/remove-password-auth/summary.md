# 移除密码认证功能 — 完成总结

## 变更概要

移除了客户端密码输入和服务端密码验证功能，用户只需输入用户名即可直接连接服务器。新用户自动注册。

## 修改文件

| 文件 | 变更 |
|------|------|
| `src/ui/include/nevo/ui/MainWindow.h` | 移除 `password_edit_` 成员和 `password()` 访问器，更新类注释 |
| `src/ui/src/MainWindow.cpp` | LoginDialog 移除密码输入框和 `password()` 方法；`onConnectAction()` 和 `onConnectRequested()` 移除 password 变量及传参；副标题改为 "Enter your username to join the voice server" |
| `src/client/include/nevo/client/ClientCore.h` | `connect()` 签名移除 `password` 参数 |
| `src/client/src/ClientCore.cpp` | `connect()` 实现移除 `password` 参数，`set_auth_credential("")` 发送空凭证 |
| `src/server/src/ClientSession.cpp` | `handleLogin()` 替换 `verifyUser()` 为 `getUserByName()` + 自动注册逻辑；已有用户跳过密码验证直接登录，新用户调用 `createUser()` 自动注册后登录 |
| `src/ui/translations/nevo_client_en.ts` | 移除 "Enter your credentials..." / "Password:" / "Enter password"；新增 "Enter your username to join the voice server" |
| `src/ui/translations/nevo_client_zh_CN.ts` | 同上，简体中文翻译 |
| `src/ui/translations/nevo_client_zh_TW.ts` | 同上，繁体中文翻译 |

## 构建验证

全部目标编译链接通过：`nevo_core`, `nevo_network`, `nevo_server`, `nevo_server_gui`, `nevo_client`, `nevo_client_ui`
