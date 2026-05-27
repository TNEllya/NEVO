# IPv6/IPv4 双栈改造实施方案

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 使 NEVO 的 C++ 服务端、C++ 客户端网络层、Python 客户端全面支持 IPv6/IPv4 双栈，所有网络接口在支持 IPv6 的系统上同时接受 IPv4 和 IPv6 连接，在仅支持 IPv4 的系统上自动降级。

**Architecture:** 采用单一 IPv6 socket + `IPV6_V6ONLY=0` 策略实现双栈。服务端监听单个 IPv6 socket，IPv4 连接作为 IPv4-mapped IPv6 地址（`::ffff:x.x.x.x`）到达；客户端通过 `getaddrinfo(AF_UNSPEC)` 自动选择地址族。所有改动为局部替换，不改变对外 API 签名。

**Tech Stack:** C++20 + Boost.Asio, Python 3 + socket / getaddrinfo

---

## 总览：改动清单

| 序号 | 文件 | 改动性质 | 涉及行号 |
|------|------|----------|----------|
| 1 | `src/network/src/UdpSocket.cpp` | 修改 `bind()` | :45-65 |
| 2 | `src/server/src/ServerCore.cpp` | 修改 `acceptTcpLoop()` | :753-756 |
| 3 | `src/network/src/NatTraversal.cpp` | 修改 `sendAndReceive()` | :1011-1018 |
| 4 | `src/client/gui_python/voice_engine.py` | 修改 `pre_create_udp_socket()` + `_create_udp_socket()` | :158, :445 |
| 5 | `src/client/gui_python/video_engine.py` | 修改 `pre_create_udp_socket()` | :144 |
| 6 | `src/client/gui_python/nevo_client.py` | 修改 `connect()` | :250 |

---

### Task 1: C++ UdpSocket — Dual-Stack Bind

**Files:**
- Modify: `src/network/src/UdpSocket.cpp:45-65`

**说明:** `UdpSocket` 是服务端和客户端共用的网络层基类，所有 UDP 语音/视频通信均通过它。当前硬编码 `udp::v4()`，需改为优先尝试 IPv6 双栈，失败后降级 IPv4。

- [ ] **Step 1: 修改 UdpSocket::bind() — 优先 IPv6 双栈，降级 IPv4**

将 `src/network/src/UdpSocket.cpp` 的 `bind()` 方法（第 45-65 行）：

```cpp
boost::system::error_code UdpSocket::bind(uint16_t local_port)
{
    boost::system::error_code ec;

    // 打开 UDP socket（IPv4）
    socket_.open(boost::asio::ip::udp::v4(), ec);
    if (ec) {
        NEVO_LOG_ERROR("network", "Failed to open UDP socket: {}", ec.message());
        return ec;
    }

    // 绑定到指定端口（0 表示 OS 自动分配）
    auto bind_endpoint = boost::asio::ip::udp::endpoint(
        boost::asio::ip::udp::v4(), local_port);
    socket_.bind(bind_endpoint, ec);
    if (ec) {
        NEVO_LOG_ERROR("network", "Failed to bind UDP socket to port {}: {}",
                       local_port, ec.message());
        socket_.close();
        return ec;
    }

    // 设置 socket 缓冲区大小（语音场景需要较大缓冲区减少丢包）
    boost::asio::socket_base::receive_buffer_size recv_buf_opt(256 * 1024);
    socket_.set_option(recv_buf_opt, ec);
    if (ec) {
        NEVO_LOG_WARN("network", "Failed to set UDP receive buffer size: {}",
                      ec.message());
        // 非致命错误，继续执行
    }

    open_.store(true);

    NEVO_LOG_INFO("network", "UDP socket bound to port {}", localPort());
    return boost::system::error_code{};
}
```

替换为：

```cpp
boost::system::error_code UdpSocket::bind(uint16_t local_port)
{
    boost::system::error_code ec;

    // 优先尝试 IPv6 双栈（同时接受 IPv4/IPv6）
    socket_.open(boost::asio::ip::udp::v6(), ec);
    if (!ec) {
        boost::asio::ip::v6_only v6_only_opt(false);
        socket_.set_option(v6_only_opt, ec);
        if (ec) {
            NEVO_LOG_WARN("network", "Failed to set IPV6_V6ONLY=0: {}", ec.message());
            ec.clear();
        }

        auto bind_endpoint = boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v6(), local_port);
        socket_.bind(bind_endpoint, ec);
        if (!ec) {
            NEVO_LOG_INFO("network", "UDP socket bound to port {} (dual-stack)", local_port);
            goto configure_socket;
        }
        NEVO_LOG_WARN("network", "IPv6 UDP bind failed ({}), falling back to IPv4", ec.message());
        socket_.close(ec);
    } else {
        NEVO_LOG_WARN("network", "IPv6 UDP not available ({}), falling back to IPv4", ec.message());
    }

    // 降级：纯 IPv4
    socket_.open(boost::asio::ip::udp::v4(), ec);
    if (ec) {
        NEVO_LOG_ERROR("network", "Failed to open UDP socket: {}", ec.message());
        return ec;
    }

    {
        auto bind_endpoint = boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(), local_port);
        socket_.bind(bind_endpoint, ec);
        if (ec) {
            NEVO_LOG_ERROR("network", "Failed to bind UDP socket to port {}: {}",
                           local_port, ec.message());
            socket_.close();
            return ec;
        }
    }

configure_socket:
    boost::asio::socket_base::receive_buffer_size recv_buf_opt(256 * 1024);
    socket_.set_option(recv_buf_opt, ec);
    if (ec) {
        NEVO_LOG_WARN("network", "Failed to set UDP receive buffer size: {}",
                      ec.message());
    }

    open_.store(true);

    NEVO_LOG_INFO("network", "UDP socket bound to port {}", localPort());
    return boost::system::error_code{};
}
```

- [ ] **Step 2: 编译验证**

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build --config Release
```

预期：编译通过，无错误。

- [ ] **Step 3: 运行网络层测试**

```bash
ctest --test-dir build -R network
```

预期：`network_tests` 全部通过。

- [ ] **Step 4: 提交**

```bash
git add src/network/src/UdpSocket.cpp
git commit -m "feat(network): UdpSocket dual-stack IPv6/IPv4 bind with IPv4 fallback"
```

---

### Task 2: C++ ServerCore — TCP Acceptor Dual-Stack

**Files:**
- Modify: `src/server/src/ServerCore.cpp:753-756`

**说明:** 服务端 TCP acceptor 当前硬编码 `tcp::v4()`，需改为 IPv6 双栈监听，降级 IPv4 fallback。注意 `reuse_address(true)` 需要放在 `bind()` 之前。

- [ ] **Step 1: 修改 acceptTcpLoop() — TCP acceptor 双栈绑定**

将 `src/server/src/ServerCore.cpp` 中第 751-780 行：

```cpp
boost::asio::awaitable<void> ServerCore::acceptTcpLoop() {

    // Open TCP acceptor
    boost::asio::ip::tcp::endpoint tcp_endpoint(
        boost::asio::ip::tcp::v4(), tcp_port_);

    boost::system::error_code ec;
    tcp_acceptor_.open(tcp_endpoint.protocol(), ec);
    if (ec) {
        NEVO_LOG_ERROR("server", "Failed to open TCP acceptor: {}", ec.message());
        co_return;
    }

    // Set SO_REUSEADDR option
    tcp_acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);

    tcp_acceptor_.bind(tcp_endpoint, ec);
    if (ec) {
        NEVO_LOG_ERROR("server", "Failed to bind TCP acceptor: {}", ec.message());
        co_return;
    }

    tcp_acceptor_.listen(boost::asio::ip::tcp::acceptor::max_listen_connections, ec);
    if (ec) {
        NEVO_LOG_ERROR("server", "Failed to listen on TCP acceptor: {}", ec.message());
        co_return;
    }

    NEVO_LOG_INFO("server", "TCP acceptor listening on port {}", tcp_port_);
```

替换为：

```cpp
boost::asio::awaitable<void> ServerCore::acceptTcpLoop() {

    boost::system::error_code ec;

    // 优先尝试 IPv6 双栈（同时接受 IPv4/IPv6 连接）
    tcp_acceptor_.open(boost::asio::ip::tcp::v6(), ec);
    if (!ec) {
        boost::asio::ip::v6_only v6_only_opt(false);
        tcp_acceptor_.set_option(v6_only_opt, ec);
        if (ec) {
            NEVO_LOG_WARN("server", "Failed to set IPV6_V6ONLY=0: {}", ec.message());
            ec.clear();
        }

        tcp_acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);

        boost::asio::ip::tcp::endpoint tcp_endpoint(
            boost::asio::ip::tcp::v6(), tcp_port_);
        tcp_acceptor_.bind(tcp_endpoint, ec);
        if (!ec) {
            NEVO_LOG_INFO("server", "TCP acceptor listening on port {} (dual-stack)", tcp_port_);
            goto start_accept;
        }
        NEVO_LOG_WARN("server", "IPv6 TCP bind failed ({}), falling back to IPv4", ec.message());
        tcp_acceptor_.close(ec);
    } else {
        NEVO_LOG_WARN("server", "IPv6 TCP not available ({}), falling back to IPv4", ec.message());
    }

    // 降级：纯 IPv4
    tcp_acceptor_.open(boost::asio::ip::tcp::v4(), ec);
    if (ec) {
        NEVO_LOG_ERROR("server", "Failed to open TCP acceptor: {}", ec.message());
        co_return;
    }

    tcp_acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);

    {
        boost::asio::ip::tcp::endpoint tcp_endpoint(
            boost::asio::ip::tcp::v4(), tcp_port_);
        tcp_acceptor_.bind(tcp_endpoint, ec);
        if (ec) {
            NEVO_LOG_ERROR("server", "Failed to bind TCP acceptor: {}", ec.message());
            co_return;
        }
    }

start_accept:
    tcp_acceptor_.listen(boost::asio::ip::tcp::acceptor::max_listen_connections, ec);
    if (ec) {
        NEVO_LOG_ERROR("server", "Failed to listen on TCP acceptor: {}", ec.message());
        co_return;
    }

    NEVO_LOG_INFO("server", "TCP acceptor listening on port {}", tcp_port_);
```

- [ ] **Step 2: 编译验证**

```bash
cmake --build build --config Release
```

预期：编译通过。

- [ ] **Step 3: 运行服务端测试**

```bash
ctest --test-dir build -R server
```

预期：`server_tests` 全部通过。

- [ ] **Step 4: 提交**

```bash
git add src/server/src/ServerCore.cpp
git commit -m "feat(server): TCP acceptor dual-stack IPv6/IPv4 with IPv4 fallback"
```

---

### Task 3: C++ NatTraversal — Dual-Stack STUN

**Files:**
- Modify: `src/network/src/NatTraversal.cpp:1011-1018`

**说明:** `sendAndReceive()` 用于向 STUN/TURN 服务器发送 UDP 请求。当前硬编码 `udp::v4()` 创建 socket。需改为根据解析结果的目标地址族动态选择，IPv4-only fallback。

- [ ] **Step 1: 修改 sendAndReceive() — 自适应地址族创建 socket**

将 `src/network/src/NatTraversal.cpp` 中第 1011-1018 行：

```cpp
        // 创建 UDP socket
        boost::asio::ip::udp::socket socket(executor,
            boost::asio::ip::udp::v4());

        // 绑定到任意端口
        socket.open(boost::asio::ip::udp::v4());
        socket.bind(boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(), 0));
```

替换为：

```cpp
        // 根据解析结果的地址族创建 UDP socket
        boost::system::error_code open_ec;
        if (server_endpoint.address().is_v6()) {
            socket.open(boost::asio::ip::udp::v6(), open_ec);
            if (!open_ec) {
                boost::asio::ip::v6_only v6_only_opt(false);
                socket.set_option(v6_only_opt, open_ec);
                open_ec.clear();
            }
        }
        if (!socket.is_open() || open_ec) {
            socket.open(boost::asio::ip::udp::v4(), open_ec);
            if (open_ec) {
                NEVO_LOG_ERROR("network", "sendAndReceive: failed to open UDP socket: {}", open_ec.message());
                co_return std::nullopt;
            }
        }

        // 绑定到任意端口
        boost::system::error_code bind_ec;
        socket.bind(boost::asio::ip::udp::endpoint(
            server_endpoint.protocol(), 0), bind_ec);
        if (bind_ec) {
            NEVO_LOG_ERROR("network", "sendAndReceive: failed to bind UDP socket: {}", bind_ec.message());
            co_return std::nullopt;
        }
```

注意：上述改动需要将第 1012 行的 socket 声明方式改为先声明再 open 的形式。完整替换后，`sendAndReceive()` 中 1011-1020 区域最终变成：

```cpp
        // 根据解析结果的地址族创建 UDP socket
        boost::asio::ip::udp::socket socket(executor);
        boost::system::error_code open_ec;
        if (server_endpoint.address().is_v6()) {
            socket.open(boost::asio::ip::udp::v6(), open_ec);
            if (!open_ec) {
                boost::asio::ip::v6_only v6_only_opt(false);
                socket.set_option(v6_only_opt, open_ec);
                open_ec.clear();
            }
        }
        if (!socket.is_open() || open_ec) {
            socket.open(boost::asio::ip::udp::v4(), open_ec);
            if (open_ec) {
                NEVO_LOG_ERROR("network", "sendAndReceive: failed to open UDP socket: {}", open_ec.message());
                co_return std::nullopt;
            }
        }

        // 绑定到任意端口
        boost::system::error_code bind_ec;
        socket.bind(boost::asio::ip::udp::endpoint(
            server_endpoint.protocol(), 0), bind_ec);
        if (bind_ec) {
            NEVO_LOG_ERROR("network", "sendAndReceive: failed to bind UDP socket: {}", bind_ec.message());
            co_return std::nullopt;
        }
```

- [ ] **Step 2: 编译验证**

```bash
cmake --build build --config Release
```

预期：编译通过。`server_endpoint.address().is_v6()` 和 `server_endpoint.protocol()` 都是 Boost.Asio 标准 API，无需额外头文件。

- [ ] **Step 3: 运行网络层测试**

```bash
ctest --test-dir build -R network
```

预期：全部通过。

- [ ] **Step 4: 提交**

```bash
git add src/network/src/NatTraversal.cpp
git commit -m "feat(network): NatTraversal sendAndReceive auto-detect address family for STUN"
```

---

### Task 4: Python voice_engine.py — Dual-Stack UDP

**Files:**
- Modify: `src/client/gui_python/voice_engine.py:158` (`pre_create_udp_socket()`)
- Modify: `src/client/gui_python/voice_engine.py:445` (`_create_udp_socket()`)

**说明:** 两处均硬编码 `AF_INET` + `bind("0.0.0.0", 0)`。改为 `AF_INET6` 双栈 + `IPV6_V6ONLY=0`，IPv4 fallback。

- [ ] **Step 1: 添加双栈 socket 创建辅助函数**

在 `voice_engine.py` 顶部（import 区域之后、`class VoiceEngine` 之前或类内）添加一个辅助方法。为保持改动最小，直接在类中添加静态方法：

将 `VoiceEngine` 类中的两处重复逻辑抽取。先修改 `pre_create_udp_socket()`（第 154-163 行）：

原代码：
```python
    def pre_create_udp_socket(self):
        if self._udp_sock is not None:
            return
        try:
            self._udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 256 * 1024)
            self._udp_sock.settimeout(1.0)
            self._udp_sock.bind(("0.0.0.0", 0))
        except Exception:
            self._udp_sock = None
```

替换为：
```python
    @staticmethod
    def _create_dualstack_udp(rcvbuf=256 * 1024):
        """创建 IPv6 双栈 UDP socket，降级 IPv4"""
        try:
            sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
            sock.settimeout(1.0)
            sock.bind(("::", 0))
            return sock
        except Exception:
            pass
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
            sock.settimeout(1.0)
            sock.bind(("0.0.0.0", 0))
            return sock
        except Exception:
            return None

    def pre_create_udp_socket(self):
        if self._udp_sock is not None:
            return
        self._udp_sock = self._create_dualstack_udp(256 * 1024)
```

- [ ] **Step 2: 修改 _create_udp_socket()**

将第 441-450 行：

```python
    def _create_udp_socket(self):
        if self._udp_sock is not None:
            return
        try:
            self._udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 256 * 1024)
            self._udp_sock.settimeout(1.0)
            self._udp_sock.bind(("0.0.0.0", 0))
        except Exception:
            self._udp_sock = None
```

替换为（复用上面的 `_create_dualstack_udp`）：
```python
    def _create_udp_socket(self):
        if self._udp_sock is not None:
            return
        self._udp_sock = self._create_dualstack_udp(256 * 1024)
```

- [ ] **Step 3: 确认 set_server_udp 兼容性**

`set_server_udp(self, host, port)` 将 `(host, port)` 元组传给 `socket.sendto(packet, addr)`，`AF_INET6` 双栈 socket 的 `sendto()` 可直接接收 IPv4 地址（自动转换为 `::ffff:x.x.x.x`）：

```python
# 已存在代码，无需修改：
self._udp_sock.sendto(packet, (host, port))  # 对 IPv4/IPv6 均兼容
```

- [ ] **Step 4: 提交**

```bash
git add src/client/gui_python/voice_engine.py
git commit -m "feat(python): voice_engine dual-stack UDP socket with IPv4 fallback"
```

---

### Task 5: Python video_engine.py — Dual-Stack UDP

**Files:**
- Modify: `src/client/gui_python/video_engine.py:140-153`

**说明:** 同 voice_engine.py，`pre_create_udp_socket()` 硬编码 `AF_INET`。

- [ ] **Step 1: 添加双栈 socket 创建辅助函数并修改 pre_create_udp_socket()**

将第 140-153 行：

```python
    def pre_create_udp_socket(self):
        if self._udp_sock is not None:
            return True
        try:
            self._udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512 * 1024)
            self._udp_sock.settimeout(1.0)
            self._udp_sock.bind(("0.0.0.0", 0))
            _vlog(f"[RECV] pre_create_udp_socket SUCCESS: port={self.local_udp_port}")
            return True
        except Exception as e:
            _vlog_exc(f"[RECV] pre_create_udp_socket FAILED: {e}")
            self._udp_sock = None
            return False
```

替换为：

```python
    @staticmethod
    def _create_dualstack_udp(rcvbuf=512 * 1024):
        """创建 IPv6 双栈 UDP socket，降级 IPv4"""
        try:
            sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
            sock.settimeout(1.0)
            sock.bind(("::", 0))
            return sock
        except Exception:
            pass
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
            sock.settimeout(1.0)
            sock.bind(("0.0.0.0", 0))
            return sock
        except Exception:
            return None

    def pre_create_udp_socket(self):
        if self._udp_sock is not None:
            return True
        self._udp_sock = self._create_dualstack_udp(512 * 1024)
        if self._udp_sock:
            _vlog(f"[RECV] pre_create_udp_socket SUCCESS: port={self.local_udp_port}")
            return True
        else:
            _vlog("[RECV] pre_create_udp_socket FAILED")
            return False
```

- [ ] **Step 2: 提交**

```bash
git add src/client/gui_python/video_engine.py
git commit -m "feat(python): video_engine dual-stack UDP socket with IPv4 fallback"
```

---

### Task 6: Python nevo_client.py — Dual-Stack TCP

**Files:**
- Modify: `src/client/gui_python/nevo_client.py:250-252`

**说明:** 客户端 TCP 连接硬编码 `AF_INET`。改为 `getaddrinfo(AF_UNSPEC)` 自动选择地址族 + 遍历连接。

- [ ] **Step 1: 修改 connect() — 使用 getaddrinfo 自动选择地址族**

将第 249-252 行：

```python
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.settimeout(10)
            self._sock.connect((host, port))
            self._sock.settimeout(None)
```

替换为：

```python
        try:
            addr_info = socket.getaddrinfo(host, port, socket.AF_UNSPEC,
                                           socket.SOCK_STREAM, socket.IPPROTO_TCP)
            last_err = None
            self._sock = None
            for family, socktype, proto, canonname, sockaddr in addr_info:
                try:
                    self._sock = socket.socket(family, socktype, proto)
                    self._sock.settimeout(10)
                    self._sock.connect(sockaddr)
                    self._sock.settimeout(None)
                    last_err = None
                    break
                except Exception as e:
                    last_err = e
                    if self._sock:
                        try:
                            self._sock.close()
                        except Exception:
                            pass
                        self._sock = None
            if last_err or self._sock is None:
                raise RuntimeError(f"Failed to connect to {host}:{port}: {last_err}")
```

- [ ] **Step 2: 验证 Python 客户端启动**

```bash
cd src/client/gui_python && python -c "from nevo_client import NevoClient; c = NevoClient(); print('OK')"
```

预期：`OK`，无 import 错误。

- [ ] **Step 3: 提交**

```bash
git add src/client/gui_python/nevo_client.py
git commit -m "feat(python): nevo_client dual-stack TCP connect via getaddrinfo"
```

---

### Task 7: 集成验证

- [ ] **Step 1: 全量编译**

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build --config Release
```

- [ ] **Step 2: 运行全部测试**

```bash
ctest --test-dir build --output-on-failure
```

预期：所有已有测试通过，无回归。

- [ ] **Step 3: 本地 IPv4 兼容性验证**

启动服务端，用 IPv4 地址（`127.0.0.1`）连接 Python 客户端，验证语音通信正常。

```bash
# Terminal 1: 启动服务端
./build/bin/Release/nevo_server --tcp-port 9999 --udp-port 9998

# Terminal 2: 启动 Python 客户端连接 127.0.0.1
cd src/client/gui_python && python main.py
```

预期：连接成功，频道加入正常，语音通信正常。

- [ ] **Step 4: IPv6 连接验证（如有 IPv6 环境）**

```bash
# 用 ::1 地址连接服务端
cd src/client/gui_python
python -c "
from nevo_client import NevoClient
c = NevoClient()
c.connect('::1', 9999, 'testuser', 'testpass')
print('IPv6 connection:', c.is_connected())
c.disconnect()
"
```

预期：`IPv6 connection: True`

- [ ] **Step 5: 提交最终确认**

```bash
git log --oneline -7
```

---

## 自检清单

1. **覆盖范围** — 所有硬编码 `AF_INET` / `v4()` 的网络创建点均已覆盖：UdpSocket、ServerCore TCP acceptor、NatTraversal STUN、Python TCP 客户端、Python UDP 语音、Python UDP 视频。
2. **无占位符** — 所有代码块均为可直接复制粘贴的完整实现。
3. **类型一致性** — `AF_INET6` 双栈 socket 的 `sendto()` 和 `connect()` 均可直接接受 IPv4 地址元组，无需额外转换。
4. **降级策略** — 所有改动包含 IPv6 → IPv4 降级路径，在不支持 IPv6 的系统上行为与改造前一致。