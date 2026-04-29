#pragma once
/**
 * @file ClientCore.h
 * @brief 客户端核心生命周期管理器
 *
 * ClientCore 是 NEVO VoIP 客户端的顶层协调器，管理从连接建立到
 * 频道切换、音频控制的所有客户端生命周期。
 *
 * 核心职责：
 *   1. 连接管理：TCP/TLS 连接 + NAT 穿透 + 密钥交换
 *   2. 频道管理：加入/离开频道、用户列表维护
 *   3. 音频控制：静音、耳聋、PTT、VAD
 *   4. 状态机：驱动客户端在 Disconnected → Connecting → Connected → InChannel 间切换
 *
 * 状态机转换图：
 *
 *   Disconnected ──connect()──> Connecting ──TCP+TLS OK──> Connected
 *                                                                   |
 *                                              joinChannel() ───────> InChannel
 *                                              leaveChannel() <──────┘
 *                                                                   |
 *                                              disconnect() <────────┘
 *                                              (任意状态均可回到 Disconnected)
 *
 * 组件所有权：
 *   ClientCore 拥有并管理以下组件的完整生命周期：
 *   - NetworkManager: 网络连接和语音通道
 *   - AudioEngine: 音频采集/编码/解码/播放
 *   - AudioInput: 编码输出 → 网络发送的桥接
 *   - AudioOutput: 网络接收 → 解码播放的桥接
 *
 * 线程安全：
 *   - 所有公共方法应在主线程调用
 *   - 回调在 io_context 线程中触发，ClientCore 内部做线程安全处理
 */

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

#include "nevo/core/audio/AudioEngine.h"
#include "nevo/core/common/Result.h"
#include "nevo/core/common/Types.h"
#include "nevo/core/model/User.h"
#include "nevo/core/model/Channel.h"
#include "nevo/client/NetworkManager.h"

namespace nevo {

class AudioEngine;
class AudioInput;
class AudioOutput;

// ============================================================
// 客户端连接状态
// ============================================================

/// 客户端状态枚举
enum class ClientState {
    Disconnected,   ///< 未连接：初始状态或已断开
    Connecting,     ///< 连接中：正在建立 TCP/TLS 连接
    Connected,      ///< 已连接：TCP 连接成功，已登录，但未加入频道
    InChannel,      ///< 在频道中：已加入某个语音频道
};

/// 客户端状态转字符串（用于日志和调试）
inline const char* clientStateToString(ClientState state) {
    switch (state) {
        case ClientState::Disconnected: return "Disconnected";
        case ClientState::Connecting:   return "Connecting";
        case ClientState::Connected:    return "Connected";
        case ClientState::InChannel:    return "InChannel";
        default:                        return "Unknown";
    }
}

// ============================================================
// 客户端状态快照
// ============================================================

/// 客户端当前状态的只读快照
struct ClientStateSnapshot {
    ClientState state = ClientState::Disconnected;   ///< 连接状态
    SessionId session_id;                              ///< 当前会话 ID
    UserId local_user_id;                              ///< 本地用户 ID
    std::string local_username;                        ///< 本地用户名
    ChannelId current_channel;                         ///< 当前所在频道 ID
    std::string current_channel_name;                  ///< 当前频道名称
    NatType nat_type = NatType::Blocked;               ///< NAT 类型
    UdpChannelMode udp_mode = UdpChannelMode::None;    ///< UDP 通道模式
    bool is_muted = false;                             ///< 是否静音
    bool is_deafened = false;                          ///< 是否耳聋
    bool is_ptt_active = false;                        ///< PTT 是否激活
    bool is_speaking = false;                          ///< 是否正在说话
    std::vector<User> channel_users;                   ///< 当前频道用户列表
};

// ============================================================
// 客户端事件回调
// ============================================================

/// 状态变更回调
using StateChangedCallback = std::function<void(ClientState new_state, ClientState old_state)>;

/// 用户加入频道回调
using UserJoinedCallback = std::function<void(const User& user)>;

/// 用户离开频道回调
using UserLeftCallback = std::function<void(UserId user_id)>;

/// 用户说话状态变更回调
using UserSpeakingCallback = std::function<void(UserId user_id, bool is_speaking)>;

/// 服务器消息回调
using ServerMessageCallback = std::function<void(const std::string& message)>;

/// 频道列表更新回调
using ChannelListCallback = std::function<void(const std::vector<ChannelInfo>& channels)>;

/// 延迟更新回调（延迟毫秒数）
using LatencyUpdateCallback = std::function<void(int latency_ms)>;

/// 错误回调
using ErrorCallback = std::function<void(ResultCode code, const std::string& message)>;

// ============================================================
// ClientCore 类
// ============================================================

/**
 * @class ClientCore
 * @brief 客户端核心生命周期管理器
 *
 * 管理客户端从连接到音频通信的完整生命周期。
 * 拥有所有子组件（NetworkManager、AudioEngine、AudioInput、AudioOutput），
 * 协调它们之间的交互。
 *
 * 典型用法：
 * @code
 *   ClientCore client(io_ctx);
 *   client.onStateChanged = [](auto new_state, auto old_state) { ... };
 *   client.onUserJoined = [](auto& user) { ... };
 *
 *   // 连接并登录
 *   auto result = co_await client.connect("server.example.com", 8080, "alice", "password");
 *   if (!result) { handleError(result.error()); return; }
 *
 *   // 加入频道
 *   result = co_await client.joinChannel(ChannelId(1));
 *
 *   // 音频控制
 *   client.setMuted(true);
 *   client.setPttActive(true);
 *
 *   // 断开连接
 *   client.disconnect();
 * @endcode
 */
class ClientCore {
public:
    /// 构造函数
    /// @param io_ctx Boost.Asio I/O 上下文引用
    explicit ClientCore(boost::asio::io_context& io_ctx);

    /// 析构函数：确保优雅断开连接并释放所有资源
    ~ClientCore();

    // 禁止拷贝和移动
    ClientCore(const ClientCore&) = delete;
    ClientCore& operator=(const ClientCore&) = delete;
    ClientCore(ClientCore&&) = delete;
    ClientCore& operator=(ClientCore&&) = delete;

    // ============================================================
    // 连接管理
    // ============================================================

    /**
     * @brief 连接到服务器并登录
     *
     * 完整连接序列：
     *   1. 状态切换：Disconnected → Connecting
     *   2. NetworkManager.connect(): 建立 TCP/TLS 连接
     *   3. 发送 LoginRequest 控制消息
     *   4. 等待 LoginResponse，处理认证结果
     *   5. 从 LoginResponse 中提取会话密钥，存入 VoiceCrypto
     *   6. NetworkManager.establishUdpChannel(): 建立 UDP 语音通道
     *   7. 初始化 AudioEngine
     *   8. 启动 AudioInput 和 AudioOutput
     *   9. 状态切换：Connecting → Connected
     *
     * @param host     服务器主机名或 IP
     * @param tcp_port 服务器 TCP 端口
     * @param username 用户名
     * @param password 密码（空字符串表示无密码/访客模式）
     * @return awaitable<Result<void>> 连接结果
     */
    boost::asio::awaitable<Result<void>> connect(
        const std::string& host,
        uint16_t tcp_port,
        const std::string& username,
        const std::string& password = "");

    /**
     * @brief 优雅断开连接
     *
     * 断开流程：
     *   1. 如果在频道中，先离开频道
     *   2. 停止 AudioInput 和 AudioOutput
     *   3. 关闭 AudioEngine
     *   4. NetworkManager.disconnect()
     *   5. 状态切换：任意状态 → Disconnected
     */
    void disconnect();

    // ============================================================
    // 频道管理
    // ============================================================

    /**
     * @brief 加入语音频道
     *
     * 加入流程：
     *   1. 检查当前状态是否为 Connected
     *   2. 发送 JoinChannel 控制消息
     *   3. 等待服务器确认（频道用户列表等）
     *   4. 状态切换：Connected → InChannel
     *
     * @param channel_id 目标频道 ID
     * @return awaitable<Result<void>> 加入结果
     */
    boost::asio::awaitable<Result<void>> joinChannel(ChannelId channel_id);

    /**
     * @brief 离开当前频道
     *
     * 离开流程：
     *   1. 发送 LeaveChannel 控制消息
     *   2. 清理远端用户解码器
     *   3. 状态切换：InChannel → Connected
     *
     * @return awaitable<Result<void>> 离开结果
     */
    boost::asio::awaitable<Result<void>> leaveChannel();

    // ============================================================
    // 音频控制
    // ============================================================

    /**
     * @brief 设置麦克风静音状态
     *
     * 静音时 AudioInput 不再发送语音数据。
     * 同时通知服务器更新用户的静音状态。
     *
     * @param muted true 表示静音
     */
    void setMuted(bool muted);

    /**
     * @brief 设置耳聋状态
     *
     * 耳聋时 AudioOutput 丢弃所有收到的语音数据。
     * 耳聋通常同时伴随静音。
     *
     * @param deafened true 表示耳聋
     */
    void setDeafened(bool deafened);

    /**
     * @brief 设置 PTT 按键状态
     *
     * PTT（Push-To-Talk）模式下，只有按键按下时才发送语音。
     * 通过 AudioEngine.setPttActive() 传递到底层。
     *
     * @param active true 表示 PTT 按键按下
     */
    void setPttActive(bool active);

    // ============================================================
    // 设备管理（转发到 AudioEngine）
    // ============================================================

    /// 枚举输入设备
    std::vector<AudioEngine::DeviceInfo> enumerateInputDevices();

    /// 枚举输出设备
    std::vector<AudioEngine::DeviceInfo> enumerateOutputDevices();

    /// 按名称选择输入设备
    Result<void> selectInputDeviceByName(const std::string& name);

    /// 按名称选择输出设备
    Result<void> selectOutputDeviceByName(const std::string& name);

    /// 初始化音频上下文（允许在连接前枚举设备）
    Result<void> initAudioContext();

    /// 获取当前输入设备名称
    std::string currentInputDeviceName() const;

    /// 获取当前输出设备名称
    std::string currentOutputDeviceName() const;

    /// 播放测试音
    Result<void> playTestTone(float frequency = 440.0f, float duration_sec = 1.0f);

    /// 获取当前输入电平（0.0~1.0）
    float getCurrentInputLevel() const;

    /// 设置输入电平回调
    void setInputLevelCallback(AudioEngine::InputLevelCallback cb);

    /// 启用/禁用麦克风本地监听
    void setMonitorEnabled(bool enabled);

    /// 设置监听音量（0.0~1.0）
    void setMonitorVolume(float volume);

    // ============================================================
    // 状态查询
    // ============================================================

    /**
     * @brief 获取客户端状态快照
     *
     * 返回当前客户端的完整状态信息，包括连接状态、频道、用户列表等。
     * 线程安全，可在任意线程调用。
     *
     * @return ClientStateSnapshot 状态快照
     */
    ClientStateSnapshot getState() const;

    /// 获取当前连接状态
    /// @return ClientState 枚举值
    ClientState state() const;

    /// 查询是否已连接
    /// @return true 表示 TCP 连接已建立
    bool isConnected() const;

    /// 查询是否在频道中
    /// @return true 表示已加入某个语音频道
    bool isInChannel() const;

    // ============================================================
    // 子组件访问
    // ============================================================

    /// 获取 NetworkManager 引用
    NetworkManager& networkManager();

    /// 获取 AudioEngine 引用
    AudioEngine& audioEngine();

    /// 获取 AudioInput 引用
    AudioInput& audioInput();

    /// 获取 AudioOutput 引用
    AudioOutput& audioOutput();

    // ============================================================
    // 事件回调
    // ============================================================

    /// 状态变更回调
    StateChangedCallback onStateChanged;

    /// 用户加入频道回调
    UserJoinedCallback onUserJoined;

    /// 用户离开频道回调
    UserLeftCallback onUserLeft;

    /// 用户说话状态变更回调
    UserSpeakingCallback onUserSpeaking;

    /// 服务器消息回调
    ServerMessageCallback onServerMessage;

    /// 频道列表更新回调
    ChannelListCallback onChannelList;

    /// 延迟更新回调
    LatencyUpdateCallback onLatencyUpdate;

    /// 错误回调
    ErrorCallback onError;

    /// 服主绑定结果回调 (success, message)
    std::function<void(bool, const std::string&)> onOwnerBound;

    /// 服务器无管理员，需要客户端进行服主绑定回调
    std::function<void()> onOwnerBindRequired;

    /**
     * @brief 发送服主绑定请求
     *
     * 向服务器发送 BindOwnerRequest，使用提供的绑定密钥申请服主身份。
     * 服务器响应通过 onOwnerBound 回调通知。
     *
     * @param bind_key 绑定密钥（64 字符 hex 字符串）
     */
    void sendBindOwnerRequest(const std::string& bind_key);

private:
    // ============================================================
    // 内部方法
    // ============================================================

    /**
     * @brief 切换客户端状态
     *
     * 线程安全地切换状态，触发 onStateChanged 回调。
     *
     * @param new_state 新状态
     */
    void setState(ClientState new_state);

    /**
     * @brief 处理控制消息
     *
     * NetworkManager 收到控制消息后调用此方法。
     * 根据消息类型分派到具体处理逻辑。
     *
     * @param message   控制消息
     * @param type      消息类型
     * @param request_id 请求 ID
     */
    void handleControlMessage(const control::ControlMessage& message,
                              ControlMessageType type,
                              uint32_t request_id);

    /**
     * @brief 处理断开连接事件
     *
     * NetworkManager 检测到连接断开时调用。
     * 清理所有状态，切换到 Disconnected。
     */
    void handleDisconnected();

    /**
     * @brief 处理登录响应
     *
     * @param message LoginResponse 消息
     * @return Result<void> 登录结果
     */
    Result<void> handleLoginResponse(const control::ControlMessage& message);

    /**
     * @brief 处理加入频道响应
     *
     * @param message 频道相关消息
     */
    void handleChannelEvent(const control::ControlMessage& message,
                            ControlMessageType type);

    /**
     * @brief 处理用户事件
     *
     * @param message 用户相关消息
     * @param type    消息类型
     */
    void handleUserEvent(const control::ControlMessage& message,
                         ControlMessageType type);

    /**
     * @brief 处理密钥轮换
     *
     * @param message 密钥轮换消息
     */
    void handleKeyRotation(const control::ControlMessage& message);

    /**
     * @brief 初始化音频子系统
     *
     * 连接成功后调用，初始化 AudioEngine、AudioInput、AudioOutput。
     *
     * @return Result<void> 初始化结果
     */
    Result<void> initAudioSubsystem();

    /**
     * @brief 关闭音频子系统
     *
     * 断开连接前调用，停止并释放音频资源。
     */
    void shutdownAudioSubsystem();

    /**
     * @brief 发送 UDP Ping 并启动定时器
     *
     * 连接成功后开始周期性 ping，测量到服务器的 RTT 延迟。
     */
    void startPingTimer();

    /**
     * @brief 停止 Ping 定时器
     */
    void stopPingTimer();

    /**
     * @brief 定时发送 UdpPingRequest
     */
    void sendPing();

    /**
     * @brief 处理 UdpPingResponse，计算 RTT
     */
    void handleUdpPingResponse(const control::ControlMessage& message);

    /**
     * @brief 处理服主绑定响应
     *
     * @param message BindOwnerResponse 消息
     */
    void handleBindOwnerResponse(const control::ControlMessage& message);

    // ============================================================
    // 数据成员
    // ============================================================

    // --- I/O 上下文 ---
    boost::asio::io_context& io_ctx_;

    // --- 客户端状态 ---
    std::atomic<ClientState> state_{ClientState::Disconnected};
    mutable std::mutex state_mutex_;    ///< 保护复合状态查询

    // --- 本地用户信息 ---
    SessionId session_id_;
    UserId local_user_id_;
    std::string local_username_;
    std::string auth_credential_;  ///< 认证凭证（密码或令牌）

    // --- 当前频道 ---
    ChannelId current_channel_;
    std::string current_channel_name_;

    // --- 频道用户列表 ---
    std::unordered_map<UserId, User> channel_users_;
    mutable std::mutex users_mutex_;    ///< 保护 channel_users_

    // --- 音频控制状态 ---
    std::atomic<bool> muted_{false};
    std::atomic<bool> deafened_{false};
    std::atomic<bool> ptt_active_{false};

    // --- 客户端身份密钥对 (Curve25519) ---
    static constexpr size_t IDENTITY_PUBLIC_KEY_SIZE = 32;
    static constexpr size_t IDENTITY_SECRET_KEY_SIZE = 32;
    std::array<uint8_t, IDENTITY_PUBLIC_KEY_SIZE> client_public_key_{};
    std::array<uint8_t, IDENTITY_SECRET_KEY_SIZE> client_secret_key_{};
    bool has_identity_keypair_ = false;

    /// 确保客户端身份密钥对已加载（不存在则生成）
    void ensureIdentityKeys();
    /// 从 QSettings 加载身份密钥对
    bool loadIdentityKeys();
    /// 生成新的身份密钥对并保存到 QSettings
    void generateIdentityKeys();

    // --- 登录等待机制 ---
    /// 用于 connect() 协程等待 LoginResponse 的定时器
    boost::asio::steady_timer login_waiter_;
    /// 是否已收到 LoginResponse（在 login_mutex_ 保护下访问）
    bool login_completed_ = false;
    /// 登录结果（在 login_mutex_ 保护下访问）
    Result<void> login_result_;
    /// 保护登录等待状态
    std::mutex login_mutex_;

    // --- 子组件（拥有所有权） ---
    std::unique_ptr<NetworkManager> network_mgr_;
    std::unique_ptr<AudioEngine> audio_engine_;
    std::unique_ptr<AudioInput> audio_input_;
    std::unique_ptr<AudioOutput> audio_output_;

    // --- 音频引擎状态追踪 ---
    bool audio_engine_started_for_monitor_{false};  // 标记引擎是否仅因监听测试而启动

    // --- Ping / 延迟测量 ---
    boost::asio::steady_timer ping_timer_;           ///< 定时发送 UdpPing 的定时器
    uint32_t ping_sequence_ = 0;                     ///< Ping 序列号
    std::chrono::steady_clock::time_point ping_send_time_;  ///< 最近一次 ping 发送时间
    std::atomic<int> last_latency_ms_{-1};           ///< 最近测量的延迟（毫秒）
};

} // namespace nevo
