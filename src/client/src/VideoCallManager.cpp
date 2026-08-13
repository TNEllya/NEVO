/**
 * @file VideoCallManager.cpp
 * @brief 客户端一对一视频通话管理器实现
 */

#include "nevo/client/VideoCallManager.h"
#include "nevo/client/NetworkManager.h"

#include "nevo/core/common/Logger.h"
#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/core/video/VideoTypes.h"
#include "nevo/network/VoiceCrypto.h"

// Protobuf 头文件
#include "control.pb.h"
#include "video.pb.h"

#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <chrono>
#include <random>

namespace nevo {

// ============================================================
// 辅助函数：默认本地能力
// ============================================================

static std::vector<video::CodecCapability> defaultLocalCapabilities() {
    return {
        video::CodecCapability{
            .codec = video::VideoCodec::H264,
            .max_width = 1280,
            .max_height = 720,
            .max_fps = 30,
            .hardware_accelerated = false,
        },
    };
}

// ============================================================
// 构造 / 析构
// ============================================================

VideoCallManager::VideoCallManager(boost::asio::io_context& io_ctx,
                                   NetworkManager& net_mgr,
                                   UserId local_user_id)
    : io_ctx_(io_ctx)
    , net_mgr_(net_mgr)
    , local_user_id_(local_user_id)
    , local_capabilities_(defaultLocalCapabilities())
    , preferred_profile_(video::kDefaultVideoProfile())
    , calling_timer_(io_ctx)
    , connecting_timer_(io_ctx)
    , reset_timer_(io_ctx)
{
    NEVO_LOG_INFO("video", "VideoCallManager created");
}

VideoCallManager::~VideoCallManager()
{
    hangUp();
    NEVO_LOG_INFO("video", "VideoCallManager destroyed");
}

// ============================================================
// 本地用户与媒体源/渲染器设置
// ============================================================

void VideoCallManager::setLocalUserId(UserId user_id)
{
    local_user_id_ = user_id;
}

void VideoCallManager::setLocalVideoSource(video::IVideoSourcePtr source)
{
    // 如果正在通话中，先停止旧源
    if (video_source_ && video_source_->isCapturing()) {
        video_source_->stopCapture();
    }
    video_source_ = std::move(source);
}

void VideoCallManager::setRemoteVideoSink(video::IVideoSinkPtr sink)
{
    remote_sink_ = std::move(sink);
}

void VideoCallManager::setLocalPreviewSink(video::IVideoSinkPtr preview_sink)
{
    local_preview_sink_ = std::move(preview_sink);
}

// ============================================================
// 能力与配置
// ============================================================

std::vector<video::CodecCapability> VideoCallManager::localCapabilities() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return local_capabilities_;
}

void VideoCallManager::setLocalCapabilities(std::vector<video::CodecCapability> capabilities)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    local_capabilities_ = std::move(capabilities);
}

void VideoCallManager::setPreferredProfile(const video::VideoProfile& profile)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    preferred_profile_ = profile;
}

video::VideoProfile VideoCallManager::negotiatedProfile() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return negotiated_profile_;
}

// ============================================================
// 通话控制
// ============================================================

boost::asio::awaitable<Result<void>> VideoCallManager::initiateCall(UserId peer_id)
{
    if (state_.load(std::memory_order_acquire) != video::VideoCallState::Idle) {
        co_return Err<void>(ResultCode::InvalidRequest,
                           "Cannot initiate call: not idle");
    }

    if (!local_user_id_) {
        co_return Err<void>(ResultCode::InvalidRequest,
                           "Cannot initiate call: local user id not set");
    }

    if (!net_mgr_.isTcpConnected()) {
        co_return Err<void>(ResultCode::ConnectionFailed,
                           "Cannot initiate call: not connected");
    }

    auto call_id = generateCallId();

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_call_id_ = call_id;
        peer_id_ = peer_id;
    }

    setState(video::VideoCallState::Calling);
    startCallingTimer();

    NEVO_LOG_INFO("video", "Initiating video call to user {} call_id={}",
                  peer_id.value, call_id);

    auto result = co_await sendVideoCallRequest(peer_id);
    if (!result) {
        endCall(video::VideoCallEndReason::NetworkError,
                "Failed to send call request: " + result.error().message());
    }

    co_return result;
}

void VideoCallManager::acceptCall()
{
    if (state_.load(std::memory_order_acquire) != video::VideoCallState::Ringing) {
        NEVO_LOG_WARN("video", "acceptCall() ignored: not ringing");
        return;
    }

    stopTimers();

    // 选择协商配置（作为被叫方，以本地首选配置为基础）
    video::VideoProfile profile;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        profile = preferred_profile_;
        negotiated_profile_ = profile;
    }

    setState(video::VideoCallState::Connecting);
    startConnectingTimer();
    startMedia();

    boost::asio::co_spawn(io_ctx_,
        [this, profile]() mutable -> boost::asio::awaitable<void> {
            video::VideoCallId call_id;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                call_id = current_call_id_;
            }
            auto result = co_await sendVideoCallResponse(call_id, true, "", profile);
            if (!result) {
                NEVO_LOG_ERROR("video", "Failed to send accept response: {}",
                              result.error().message());
                endCall(video::VideoCallEndReason::NetworkError,
                        "Failed to send accept response");
            }
        },
        boost::asio::detached);

    if (onProfileNegotiated) {
        onProfileNegotiated(profile);
    }
}

void VideoCallManager::rejectCall()
{
    if (state_.load(std::memory_order_acquire) != video::VideoCallState::Ringing) {
        NEVO_LOG_WARN("video", "rejectCall() ignored: not ringing");
        return;
    }

    video::VideoCallId call_id;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        call_id = current_call_id_;
    }

    boost::asio::co_spawn(io_ctx_,
        [this, call_id]() -> boost::asio::awaitable<void> {
            auto result = co_await sendVideoCallResponse(
                call_id, false, "rejected");
            if (!result) {
                NEVO_LOG_ERROR("video", "Failed to send reject response: {}",
                              result.error().message());
            }
        },
        boost::asio::detached);

    endCall(video::VideoCallEndReason::RemoteRejected,
            "Call rejected by local user");
}

void VideoCallManager::hangUp()
{
    auto current = state_.load(std::memory_order_acquire);
    if (current == video::VideoCallState::Idle ||
        current == video::VideoCallState::Ended) {
        return;
    }

    video::VideoCallId call_id;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        call_id = current_call_id_;
    }

    if (call_id != video::INVALID_CALL_ID) {
        boost::asio::co_spawn(io_ctx_,
            [this, call_id]() -> boost::asio::awaitable<void> {
                auto result = co_await sendVideoCallHangup(
                    call_id, video::VideoCallEndReason::LocalHangup);
                if (!result) {
                    NEVO_LOG_ERROR("video", "Failed to send hangup: {}",
                                  result.error().message());
                }
            },
            boost::asio::detached);
    }

    endCall(video::VideoCallEndReason::LocalHangup, "Local hangup");
}

// ============================================================
// 通话中媒体控制
// ============================================================

void VideoCallManager::setLocalAudioMuted(bool muted)
{
    local_audio_muted_.store(muted, std::memory_order_release);
    NEVO_LOG_INFO("video", "Local audio muted={}", muted);
    // 注：实际静音操作由 ClientCore 调用 AudioEngine 完成，此处仅记录状态
}

void VideoCallManager::setLocalVideoEnabled(bool enabled)
{
    local_video_enabled_.store(enabled, std::memory_order_release);
    if (video_source_) {
        video_source_->setEnabled(enabled);
    }
    NEVO_LOG_INFO("video", "Local video enabled={}", enabled);
}

bool VideoCallManager::isLocalAudioMuted() const
{
    return local_audio_muted_.load(std::memory_order_acquire);
}

bool VideoCallManager::isLocalVideoEnabled() const
{
    return local_video_enabled_.load(std::memory_order_acquire);
}

// ============================================================
// 状态查询
// ============================================================

video::VideoCallState VideoCallManager::state() const
{
    return state_.load(std::memory_order_acquire);
}

std::optional<UserId> VideoCallManager::peerId() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return peer_id_;
}

video::VideoCallId VideoCallManager::currentCallId() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_call_id_;
}

bool VideoCallManager::isInCall() const
{
    auto s = state_.load(std::memory_order_acquire);
    return s == video::VideoCallState::Calling ||
           s == video::VideoCallState::Ringing ||
           s == video::VideoCallState::Connecting ||
           s == video::VideoCallState::Connected ||
           s == video::VideoCallState::Reconnecting;
}

// ============================================================
// 控制消息处理
// ============================================================

void VideoCallManager::handleVideoCallRequest(const control::ControlMessage& message)
{
    if (!message.has_video_call_request()) {
        NEVO_LOG_WARN("video", "handleVideoCallRequest: missing payload");
        return;
    }

    const auto& req = message.video_call_request();
    auto call_id = req.call_id();
    UserId caller_id(req.target_user_id());

    NEVO_LOG_INFO("video", "Incoming video call from {} call_id={}",
                  caller_id.value, call_id);

    // 如果正忙，发送忙响应
    if (state_.load(std::memory_order_acquire) != video::VideoCallState::Idle) {
        NEVO_LOG_WARN("video", "Incoming call rejected: busy");
        boost::asio::co_spawn(io_ctx_,
            [this, call_id]() -> boost::asio::awaitable<void> {
                auto result = co_await sendVideoCallResponse(
                    call_id, false, "busy");
                if (!result) {
                    NEVO_LOG_ERROR("video", "Failed to send busy response: {}",
                                  result.error().message());
                }
            },
            boost::asio::detached);
        return;
    }

    // 解析远端能力
    std::vector<video::CodecCapability> remote_caps;
    for (const auto& cap : req.capabilities()) {
        remote_caps.push_back(video::codecCapabilityFromProto(cap));
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_call_id_ = call_id;
        peer_id_ = caller_id;
    }

    setState(video::VideoCallState::Ringing);

    if (onIncomingCall) {
        onIncomingCall(caller_id);
    }
}

void VideoCallManager::handleVideoCallResponse(const control::ControlMessage& message)
{
    if (!message.has_video_call_response()) {
        NEVO_LOG_WARN("video", "handleVideoCallResponse: missing payload");
        return;
    }

    const auto& resp = message.video_call_response();
    auto call_id = resp.call_id();

    if (state_.load(std::memory_order_acquire) != video::VideoCallState::Calling) {
        NEVO_LOG_WARN("video", "VideoCallResponse ignored: not calling");
        return;
    }

    // 校验 call_id
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (current_call_id_ != call_id) {
            NEVO_LOG_WARN("video", "VideoCallResponse call_id mismatch");
            return;
        }
    }

    stopTimers();

    if (resp.accepted()) {
        video::VideoProfile profile = video::videoProfileFromProto(resp.profile());
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            negotiated_profile_ = profile;
        }

        setState(video::VideoCallState::Connecting);
        startConnectingTimer();
        startMedia();

        if (onProfileNegotiated) {
            onProfileNegotiated(profile);
        }

        NEVO_LOG_INFO("video", "Call accepted, profile={}x{}@{}fps {}kbps",
                      profile.width, profile.height, profile.fps,
                      profile.target_bitrate_kbps);
    } else {
        video::VideoCallEndReason reason = video::VideoCallEndReason::RemoteRejected;
        if (resp.reason() == "busy") {
            reason = video::VideoCallEndReason::RemoteBusy;
        }
        endCall(reason, resp.reason().empty() ? "Call rejected" : resp.reason());
    }
}

void VideoCallManager::handleVideoCallHangup(const control::ControlMessage& message)
{
    if (!message.has_video_call_hangup()) {
        NEVO_LOG_WARN("video", "handleVideoCallHangup: missing payload");
        return;
    }

    const auto& hangup = message.video_call_hangup();
    auto call_id = hangup.call_id();

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (current_call_id_ != call_id) {
            NEVO_LOG_WARN("video", "VideoCallHangup call_id mismatch");
            return;
        }
    }

    auto reason = video::videoCallEndReasonFromProto(hangup.reason());
    endCall(reason, "Remote hangup");
}

void VideoCallManager::handleVideoProfileUpdate(const control::ControlMessage& message)
{
    if (!message.has_video_profile_update()) {
        NEVO_LOG_WARN("video", "handleVideoProfileUpdate: missing payload");
        return;
    }

    const auto& update = message.video_profile_update();
    auto call_id = update.call_id();

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (current_call_id_ != call_id) {
            return;
        }
    }

    video::VideoProfile profile = video::videoProfileFromProto(update.profile());
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        negotiated_profile_ = profile;
    }

    // 通知视频源调整配置
    if (video_source_ && video_source_->isCapturing()) {
        video_source_->stopCapture();
        auto result = video_source_->startCapture(profile);
        if (!result) {
            NEVO_LOG_ERROR("video", "Failed to restart source with new profile: {}",
                          result.error().message());
        }
    }

    if (onProfileNegotiated) {
        onProfileNegotiated(profile);
    }

    NEVO_LOG_INFO("video", "Profile updated to {}x{}@{}fps {}kbps",
                  profile.width, profile.height, profile.fps,
                  profile.target_bitrate_kbps);
}

// ============================================================
// 媒体包处理
// ============================================================

void VideoCallManager::onVideoPacketReceived(const uint8_t* data,
                                             uint32_t size,
                                             const boost::asio::ip::udp::endpoint& /*sender*/)
{
    if (size < 2) {
        return;
    }

    // ------------------------------------------------------------------
    // 1. 解析 2-byte header length 和视频包头
    // ------------------------------------------------------------------
    uint16_t header_len = 0;
    std::memcpy(&header_len, data, sizeof(header_len));

    if (size < 2u + header_len) {
        NEVO_LOG_WARN("video", "Video packet too short for header: {} < {}",
                      size, 2u + header_len);
        return;
    }

    video::VideoPacketHeader header;
    if (!header.ParseFromArray(data + 2, header_len)) {
        NEVO_LOG_WARN("video", "Failed to parse video packet header");
        return;
    }

    // ------------------------------------------------------------------
    // 2. call_id 过滤：只接收当前通话的视频包
    // ------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (current_call_id_ != header.call_id()) {
            return;
        }
    }

    // ------------------------------------------------------------------
    // 3. 解密视频载荷
    //    加密帧格式：[nonce (24 bytes)][ciphertext+tag]
    // ------------------------------------------------------------------
    const uint8_t* encrypted = data + 2 + header_len;
    uint32_t encrypted_size = size - 2 - header_len;

    if (encrypted_size <= XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE) {
        NEVO_LOG_WARN("video", "Video packet encrypted payload too short: {}",
                      encrypted_size);
        return;
    }

    const uint8_t* nonce = encrypted;
    const uint8_t* ciphertext = encrypted + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted_size - XCHACHA_NONCE_SIZE;

    auto decrypted = net_mgr_.voiceCrypto().decrypt(
        ciphertext, ct_len,
        nonce, XCHACHA_NONCE_SIZE,
        data + 2, header_len);

    if (!decrypted) {
        NEVO_LOG_WARN("video", "Video packet decryption failed");
        return;
    }

    // ------------------------------------------------------------------
    // 4. 首帧状态转换
    // ------------------------------------------------------------------
    auto s = state_.load(std::memory_order_acquire);
    if (s == video::VideoCallState::Connecting) {
        if (!has_received_frame_.exchange(true, std::memory_order_acq_rel)) {
            setState(video::VideoCallState::Connected);
            NEVO_LOG_INFO("video", "First video frame received, state -> Connected");
        }
    }

    // ------------------------------------------------------------------
    // 5. 分片重组或直接渲染
    // ------------------------------------------------------------------
    uint64_t frame_key = (static_cast<uint64_t>(header.timestamp()) << 32) |
                         (header.sender_id() & 0xFFFFFFFFULL);

    if (header.fragment_total() <= 1) {
        video::VideoFrame frame;
        frame.type = static_cast<video::VideoFrameType>(header.frame_type());
        frame.sequence_number = header.sequence_number();
        frame.timestamp_us = static_cast<uint64_t>(header.timestamp()) * 1000;
        frame.width = header.width();
        frame.height = header.height();
        frame.payload = std::move(*decrypted);
        if (remote_sink_) {
            remote_sink_->renderFrame(frame);
        }
        return;
    }

    tryAssembleAndRenderFrame(frame_key, header, std::move(*decrypted));
}

// ============================================================
// 断开连接清理
// ============================================================

void VideoCallManager::onDisconnected()
{
    if (isInCall()) {
        endCall(video::VideoCallEndReason::PeerDisconnected,
                "Peer disconnected");
    }
}

// ============================================================
// 内部方法
// ============================================================

video::VideoCallId VideoCallManager::generateCallId()
{
    static std::atomic<video::VideoCallId> counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    // 高 48 位为时间戳（约 78 小时不重复），低 16 位为递增计数
    return (static_cast<video::VideoCallId>(ns) << 16) |
           (counter.fetch_add(1, std::memory_order_relaxed) & 0xFFFF);
}

void VideoCallManager::setState(video::VideoCallState new_state)
{
    video::VideoCallState old_state =
        state_.exchange(new_state, std::memory_order_acq_rel);
    if (old_state != new_state) {
        NEVO_LOG_INFO("video", "Call state changed: {} -> {}",
                      video::videoCallStateToString(old_state),
                      video::videoCallStateToString(new_state));
        if (onStateChanged) {
            onStateChanged(new_state);
        }
    }
}

void VideoCallManager::endCall(video::VideoCallEndReason reason,
                               const std::string& message)
{
    stopTimers();
    stopMedia();

    video::VideoCallState old_state =
        state_.exchange(video::VideoCallState::Ended, std::memory_order_acq_rel);
    if (old_state != video::VideoCallState::Ended) {
        NEVO_LOG_INFO("video", "Call ended: reason={} message='{}'",
                      video::videoCallEndReasonToString(reason), message);
        if (onStateChanged) {
            onStateChanged(video::VideoCallState::Ended);
        }
        if (onCallEnded) {
            onCallEnded(reason, message);
        }
    }

    // 短暂停留在 Ended 后自动回到 Idle
    reset_timer_.cancel();
    reset_timer_.expires_after(kResetDelay);
    reset_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (!ec) {
            resetToIdle();
        }
    });
}

void VideoCallManager::resetToIdle()
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_call_id_ = video::INVALID_CALL_ID;
        peer_id_.reset();
        negotiated_profile_ = video::VideoProfile{};
    }
    has_received_frame_.store(false, std::memory_order_release);
    has_sent_frame_.store(false, std::memory_order_release);
    video_sequence_.store(0, std::memory_order_release);
    local_video_enabled_.store(true, std::memory_order_release);

    setState(video::VideoCallState::Idle);
}

video::VideoProfile VideoCallManager::negotiateProfile(
    const std::vector<video::CodecCapability>& remote_caps) const
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    // 优先 H.264
    auto find_codec = [](const auto& caps, video::VideoCodec codec) {
        return std::find_if(caps.begin(), caps.end(),
                            [codec](const video::CodecCapability& cap) {
                                return cap.codec == codec;
                            });
    };

    auto local_h264 = find_codec(local_capabilities_, video::VideoCodec::H264);
    auto remote_h264 = find_codec(remote_caps, video::VideoCodec::H264);

    video::CodecCapability selected;
    if (local_h264 != local_capabilities_.end() &&
        remote_h264 != remote_caps.end()) {
        selected = *local_h264;
        // 取交集分辨率/帧率
        selected.max_width = std::min(local_h264->max_width, remote_h264->max_width);
        selected.max_height = std::min(local_h264->max_height, remote_h264->max_height);
        selected.max_fps = std::min(local_h264->max_fps, remote_h264->max_fps);
    } else {
        // 找第一个共同支持的编解码器
        for (const auto& local_cap : local_capabilities_) {
            auto it = find_codec(remote_caps, local_cap.codec);
            if (it != remote_caps.end()) {
                selected = local_cap;
                selected.max_width = std::min(local_cap.max_width, it->max_width);
                selected.max_height = std::min(local_cap.max_height, it->max_height);
                selected.max_fps = std::min(local_cap.max_fps, it->max_fps);
                break;
            }
        }
    }

    // 以首选配置为基础，但不超过能力上限
    video::VideoProfile result = preferred_profile_;
    result.codec = selected.codec != video::VideoCodec::Unknown
                       ? selected.codec
                       : video::kDefaultVideoCodec;
    result.width = std::min(preferred_profile_.width, selected.max_width);
    result.height = std::min(preferred_profile_.height, selected.max_height);
    result.fps = std::min(preferred_profile_.fps, selected.max_fps);
    result.target_bitrate_kbps = preferred_profile_.target_bitrate_kbps;

    return result;
}

// ============================================================
// 控制消息发送
// ============================================================

boost::asio::awaitable<Result<void>> VideoCallManager::sendVideoCallRequest(UserId peer_id)
{
    control::ControlMessage msg;
    auto* req = msg.mutable_video_call_request();
    req->set_target_user_id(peer_id.value);

    video::VideoCallId call_id;
    std::vector<video::CodecCapability> caps;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        call_id = current_call_id_;
        caps = local_capabilities_;
    }

    req->set_call_id(call_id);
    for (const auto& cap : caps) {
        auto* proto_cap = req->add_capabilities();
        video::codecCapabilityToProto(cap, proto_cap);
    }

    co_return co_await net_mgr_.sendControl(
        msg, ControlMessageType::VideoCallRequest, 0);
}

boost::asio::awaitable<Result<void>> VideoCallManager::sendVideoCallResponse(
    video::VideoCallId call_id, bool accepted,
    const std::string& reason,
    const video::VideoProfile& profile)
{
    control::ControlMessage msg;
    auto* resp = msg.mutable_video_call_response();
    resp->set_call_id(call_id);
    resp->set_accepted(accepted);
    resp->set_reason(reason);

    if (accepted) {
        video::VideoProfile response_profile = profile;
        if (response_profile.width == 0) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            response_profile = negotiated_profile_;
        }
        video::videoProfileToProto(response_profile, resp->mutable_profile());
    }

    co_return co_await net_mgr_.sendControl(
        msg, ControlMessageType::VideoCallResponse, 0);
}

boost::asio::awaitable<Result<void>> VideoCallManager::sendVideoCallHangup(
    video::VideoCallId call_id, video::VideoCallEndReason reason)
{
    control::ControlMessage msg;
    auto* hangup = msg.mutable_video_call_hangup();
    hangup->set_call_id(call_id);
    hangup->set_reason(static_cast<uint32_t>(reason));

    co_return co_await net_mgr_.sendControl(
        msg, ControlMessageType::VideoCallHangup, 0);
}

// ============================================================
// 媒体生命周期
// ============================================================

void VideoCallManager::startMedia()
{
    if (!video_source_) {
        NEVO_LOG_WARN("video", "No video source set, media not started");
        return;
    }

    video::VideoProfile profile;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        profile = negotiated_profile_;
    }

    // 注册编码帧回调
    video_source_->onEncodedFrame =
        [this](const video::VideoFrame& frame) { onLocalEncodedFrame(frame); };

    auto result = video_source_->startCapture(profile);
    if (!result) {
        NEVO_LOG_ERROR("video", "Failed to start video capture: {}",
                      result.error().message());
        endCall(video::VideoCallEndReason::NetworkError,
                "Failed to start video capture");
        return;
    }

    video_source_->setEnabled(local_video_enabled_.load(std::memory_order_acquire));
    NEVO_LOG_INFO("video", "Video media started");
}

void VideoCallManager::stopMedia()
{
    if (video_source_) {
        video_source_->onEncodedFrame = nullptr;
        video_source_->stopCapture();
    }
    if (remote_sink_) {
        remote_sink_->clear();
    }
    if (local_preview_sink_) {
        local_preview_sink_->clear();
    }
    NEVO_LOG_INFO("video", "Video media stopped");
}

// ============================================================
// 超时定时器
// ============================================================

void VideoCallManager::startCallingTimer()
{
    calling_timer_.cancel();
    calling_timer_.expires_after(kCallingTimeout);
    calling_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (!ec) {
            onCallingTimeout(ec);
        }
    });
}

void VideoCallManager::startConnectingTimer()
{
    connecting_timer_.cancel();
    connecting_timer_.expires_after(kConnectingTimeout);
    connecting_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (!ec) {
            onConnectingTimeout(ec);
        }
    });
}

void VideoCallManager::stopTimers()
{
    calling_timer_.cancel();
    connecting_timer_.cancel();
    reset_timer_.cancel();
}

void VideoCallManager::onCallingTimeout(const boost::system::error_code& /*ec*/)
{
    if (state_.load(std::memory_order_acquire) == video::VideoCallState::Calling) {
        endCall(video::VideoCallEndReason::Timeout, "Calling timed out");
    }
}

void VideoCallManager::onConnectingTimeout(const boost::system::error_code& /*ec*/)
{
    if (state_.load(std::memory_order_acquire) == video::VideoCallState::Connecting) {
        endCall(video::VideoCallEndReason::NetworkError, "Connecting timed out");
    }
}

// ============================================================
// 分片重组
// ============================================================

void VideoCallManager::tryAssembleAndRenderFrame(
    uint64_t frame_key,
    const video::VideoPacketHeader& header,
    std::vector<uint8_t> payload)
{
    std::lock_guard<std::mutex> lock(fragment_mutex_);

    pruneFragmentBuffers();

    auto& buffer = fragment_buffers_[frame_key];
    buffer.last_update = std::chrono::steady_clock::now();

    if (buffer.fragment_total == 0) {
        buffer.fragment_total = header.fragment_total();
        buffer.fragments.resize(buffer.fragment_total);
    }

    if (header.fragment_index() >= buffer.fragment_total) {
        NEVO_LOG_WARN("video", "Invalid fragment index {}/{} for frame key {}",
                      header.fragment_index(), buffer.fragment_total, frame_key);
        return;
    }

    buffer.fragments[header.fragment_index()] = std::move(payload);

    // 检查是否已收齐
    size_t total_size = 0;
    for (const auto& frag : buffer.fragments) {
        if (!frag.has_value()) {
            return;
        }
        total_size += frag->size();
    }

    // 重组完整帧
    std::vector<uint8_t> assembled;
    assembled.reserve(total_size);
    for (auto& frag : buffer.fragments) {
        assembled.insert(assembled.end(),
                         frag->begin(), frag->end());
    }

    video::VideoFrame frame;
    frame.type = static_cast<video::VideoFrameType>(header.frame_type());
    frame.sequence_number = header.sequence_number();
    frame.timestamp_us = static_cast<uint64_t>(header.timestamp()) * 1000;
    frame.width = header.width();
    frame.height = header.height();
    frame.payload = std::move(assembled);

    fragment_buffers_.erase(frame_key);

    if (remote_sink_) {
        remote_sink_->renderFrame(frame);
    }
}

void VideoCallManager::pruneFragmentBuffers()
{
    auto now = std::chrono::steady_clock::now();
    for (auto it = fragment_buffers_.begin(); it != fragment_buffers_.end();) {
        if (now - it->second.last_update > kFragmentBufferTimeout) {
            it = fragment_buffers_.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================
// 本地编码帧
// ============================================================

void VideoCallManager::onLocalEncodedFrame(const video::VideoFrame& frame)
{
    // 本地预览
    if (local_preview_sink_) {
        local_preview_sink_->renderFrame(frame);
    }

    auto s = state_.load(std::memory_order_acquire);
    if (s != video::VideoCallState::Connecting &&
        s != video::VideoCallState::Connected) {
        return;
    }

    if (!local_video_enabled_.load(std::memory_order_acquire)) {
        return;
    }

    video::VideoCallId call_id;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        call_id = current_call_id_;
    }
    if (call_id == video::INVALID_CALL_ID) {
        return;
    }

    // 记录首帧到达以驱动状态机
    if (!has_sent_frame_.exchange(true, std::memory_order_acq_rel)) {
        if (s == video::VideoCallState::Connecting) {
            setState(video::VideoCallState::Connected);
            NEVO_LOG_INFO("video", "First local frame sent, state -> Connected");
        }
    }

    // 分片发送视频帧
    constexpr uint32_t kMaxFragmentSize = 1200;
    const uint32_t payload_size = static_cast<uint32_t>(frame.payload.size());
    const uint32_t fragment_total =
        std::max(1u, (payload_size + kMaxFragmentSize - 1) / kMaxFragmentSize);

    for (uint32_t i = 0; i < fragment_total; ++i) {
        uint32_t offset = i * kMaxFragmentSize;
        uint32_t fragment_size = std::min(kMaxFragmentSize, payload_size - offset);
        const uint8_t* fragment_begin = frame.payload.data() + offset;
        std::vector<uint8_t> fragment_data(fragment_begin,
                                           fragment_begin + fragment_size);

        boost::asio::co_spawn(io_ctx_,
            [this, call_id, frame_type = frame.type,
             timestamp_us = frame.timestamp_us,
             fragment_data = std::move(fragment_data),
             i, fragment_total, width = frame.width,
             height = frame.height]() mutable -> boost::asio::awaitable<void> {
                auto result = co_await net_mgr_.sendVideoPacket(
                    fragment_data.data(),
                    static_cast<uint32_t>(fragment_data.size()),
                    call_id,
                    frame_type,
                    timestamp_us,
                    i,
                    fragment_total,
                    width,
                    height,
                    0);
                if (!result) {
                    NEVO_LOG_WARN("video", "Failed to send video fragment {}/{}: {}",
                                  i + 1, fragment_total, result.error().message());
                }
            },
            boost::asio::detached);
    }
}

} // namespace nevo
