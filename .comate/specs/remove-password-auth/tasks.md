# 移除密码认证功能 — 任务计划

- [x] Task 1: 移除 LoginDialog 密码输入框及相关成员
    - 1.1: MainWindow.h 中删除 `password_edit_` 成员和 `password()` 访问器
    - 1.2: MainWindow.cpp LoginDialog 构造函数中删除密码输入框创建代码
    - 1.3: 删除 `LoginDialog::password()` 方法实现
    - 1.4: 修改副标题文字为 "Enter your username to join the voice server"

- [x] Task 2: 更新 MainWindow 连接流程（移除 password 传递）
    - 2.1: `onConnectAction()` 中移除 password 变量和 lambda 捕获
    - 2.2: `onConnectRequested()` 中移除 password 变量和 lambda 捕获

- [x] Task 3: 修改 ClientCore::connect() 签名和实现
    - 3.1: ClientCore.h 中移除 `password` 参数
    - 3.2: ClientCore.cpp 中移除 `password` 参数，发送空 credential

- [x] Task 4: 修改服务端 handleLogin() 逻辑
    - 4.1: 替换 `verifyUser()` 为 `getUserByName()` + 自动注册逻辑
    - 4.2: 保留 IP 封禁和用户级封禁检查

- [x] Task 5: 更新翻译文件
    - 5.1: 移除 "Enter your credentials..." / "Password:" / "Enter password" 翻译条目
    - 5.2: 新增 "Enter your username to join the voice server" 翻译条目

- [x] Task 6: 构建验证
    - 6.1: 编译全部目标确认无错误
