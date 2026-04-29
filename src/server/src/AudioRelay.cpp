/**
 * @file AudioRelay.cpp
 * @brief UDP 语音包转发器实现
 *
 * 实现语音包的中继转发逻辑。在服务端中继模式下，
 * 不解密语音载荷内容，仅读取包头信息并原样转发。
 */

#include "nevo/server/AudioRelay.h"
#include "nevo/server/ChannelManager.h"
#include "nevo/core/common/Logger.h"
#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/network/UdpSocket.h"
#include "nevo/network/VoiceCrypto.h"

// Protobuf 生成头文件
#include "voice.pb.h"

#include <sstream>
#include <cstring>

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

AudioRelay::AudioRelay() = default;

AudioRelay::~AudioRelay() {
    NEVO_LOG_INFO("server", "AudioRelay destroyed (relayed={}, dropped={})",
                  packets_relayed_, packets_dropped_);
}

// ============================================================
// 语音包处理
// ============================================================

void AudioRelay::handleVoicePacket(const uint8_t* data, uint32_t size,
                                    const boost::asio::ip::udp::endpoint& sender_endpoint) {
    if (!data || size == 0) {
        ++packets_dropped_;
        return;
    }

    // 解析语音包头
    uint32_t header_size = 0;
    auto header = decodeVoicePacketHeader(data, size, header_size);
    if (!header) {
        NEVO_LOG_WARN("server", "Failed to decode voice packet header from {}:{}",
                      sender_endpoint.address().to_string(), sender_endpoint.port());
        ++packets_dropped_;
        return;
    }

    // 获取发送者用户 ID 和频道 ID
    UserId sender_id(header->sender_id());
    ChannelId channel_id(header->channel_id());

    // 如果包头中的 sender_id 为 0，尝试通过端点查找
    if (!sender_id) {
        sender_id = findUserByEndpoint(sender_endpoint);
        if (!sender_id) {
            NEVO_LOG_WARN("server", "Unknown UDP sender: {}:{}",
                          sender_endpoint.address().to_string(), sender_endpoint.port());
            ++packets_dropped_;
            return;
        }
    }

    // 更新客户端映射中的端点信息（如果尚未记录）
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = client_map_.find(sender_id);
        if (it != client_map_.end()) {
            // 更新端点（可能发生变化，如 NAT 重绑定）
            if (it->second.udp_endpoint != sender_endpoint) {
                // 移除旧的端点映射
                std::string old_key = it->second.udp_endpoint.address().to_string() + ":"
                                    + std::to_string(it->second.udp_endpoint.port());
                endpoint_to_user_.erase(old_key);

                // 更新端点
                it->second.udp_endpoint = sender_endpoint;
                std::string new_key = sender_endpoint.address().to_string() + ":"
                                    + std::to_string(sender_endpoint.port());
                endpoint_to_user_[new_key] = sender_id;
            }
            // 更新频道信息
            it->second.current_channel = channel_id;
        }
    }

    // 获取同频道其他用户的端点列表
    auto peers = getChannelPeers(sender_id, channel_id);
    if (peers.empty()) {
        // 频道内没有其他用户，无需转发
        return;
    }

    if (!udp_socket_ || !io_ctx_) {
        ++packets_dropped_;
        return;
    }

    // 计算加密帧在数据包中的位置
    const uint8_t* encrypted_frame = data + header_size;
    uint32_t encrypted_frame_size = size - header_size;

    if (encrypted_frame_size < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE) {
        NEVO_LOG_WARN("server", "Voice packet too short: {} bytes (expected at least {})",
                     encrypted_frame_size, XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE);
        ++packets_dropped_;
        return;
    }

    // 提取 nonce 和密文
    const uint8_t* nonce = encrypted_frame;
    const uint8_t* ciphertext = encrypted_frame + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted_frame_size - XCHACHA_NONCE_SIZE;

    // 尝试逐客户端解密-重加密转发
    bool use_per_client_crypto = false;
    std::vector<uint8_t> plaintext;
    const uint8_t* sender_key = nullptr;

    if (session_key_query_) {
        sender_key = session_key_query_(sender_id);
    }

    if (sender_key) {
        auto decrypted = VoiceCrypto::decryptWithKey(
            sender_key, ciphertext, ct_len,
            nonce, XCHACHA_NONCE_SIZE,
            data, header_size);
        if (decrypted) {
            plaintext = std::move(*decrypted);
            use_per_client_crypto = true;
        } else {
            NEVO_LOG_WARN("server", "Failed to decrypt voice packet from user_id={}", sender_id.value);
            ++packets_dropped_;
            return;
        }
    }

    // 转发给同频道其他用户
    for (const auto& peer_endpoint : peers) {
        UserId receiver_id = findUserByEndpoint(peer_endpoint);
        if (!receiver_id) continue;

        std::vector<uint8_t> packet_to_send;

        if (use_per_client_crypto) {
            // 查找接收者的 VoiceCrypto 实例
            VoiceCrypto* receiver_crypto = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto crypto_it = client_cryptos_.find(receiver_id);
                if (crypto_it != client_cryptos_.end() && crypto_it->second) {
                    receiver_crypto = crypto_it->second.get();
                }
            }

            if (!receiver_crypto) {
                NEVO_LOG_TRACE("server", "No crypto context for receiver user_id={}, skipping", receiver_id.value);
                continue;
            }

            // 使用接收者的密钥重新加密
            auto reencrypted = receiver_crypto->encrypt(
                plaintext.data(), plaintext.size(),
                data, header_size);
            if (reencrypted.empty()) {
                NEVO_LOG_WARN("server", "Failed to re-encrypt voice packet for user_id={}", receiver_id.value);
                continue;
            }

            // 组装新数据包：原包头 + 新加密帧
            packet_to_send.reserve(header_size + reencrypted.size());
            packet_to_send.insert(packet_to_send.end(), data, data + header_size);
            packet_to_send.insert(packet_to_send.end(), reencrypted.begin(), reencrypted.end());
        } else {
            // 无逐客户端加密能力，原样转发（兼容旧模式）
            packet_to_send.assign(data, data + size);
        }

        // 构建数据副本用于异步发送
        auto data_copy = std::make_shared<std::vector<uint8_t>>(std::move(packet_to_send));

        // 通过 io_context 启动异步发送协程
        boost::asio::co_spawn(*io_ctx_,
            [this, data_copy, peer_endpoint]() -> boost::asio::awaitable<void> {
                auto ec = co_await udp_socket_->asyncSendTo(
                    data_copy->data(), static_cast<uint32_t>(data_copy->size()),
                    peer_endpoint);
                if (ec) {
                    NEVO_LOG_WARN("server", "Failed to relay voice packet to {}:{}: {}",
                                  peer_endpoint.address().to_string(),
                                  peer_endpoint.port(), ec.message());
                }
            },
            boost::asio::detached);
    }

    ++packets_relayed_;

    NEVO_LOG_TRACE("server", "Relayed voice packet from user {} to {} peers in channel {}",
                   sender_id.value, peers.size(), channel_id.value);
}

// ============================================================
// 客户端映射管理
// ============================================================

void AudioRelay::addClientMapping(UserId user_id,
                                   const boost::asio::ip::udp::endpoint& udp_endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 生成端点键
    std::string endpoint_key = udp_endpoint.address().to_string() + ":"
                             + std::to_string(udp_endpoint.port());

    // 移除旧的端点映射（如果该用户已有映射）
    auto existing = client_map_.find(user_id);
    if (existing != client_map_.end()) {
        std::string old_key = existing->second.udp_endpoint.address().to_string() + ":"
                            + std::to_string(existing->second.udp_endpoint.port());
        endpoint_to_user_.erase(old_key);
    }

    // 添加新映射
    ClientUdpMapping mapping;
    mapping.user_id = user_id;
    mapping.udp_endpoint = udp_endpoint;
    mapping.current_channel = ChannelId(0); // 将在 updateClientChannel 中设置

    client_map_[user_id] = mapping;
    endpoint_to_user_[endpoint_key] = user_id;

    // 为该用户创建 VoiceCrypto 实例并设置会话密钥
    if (session_key_query_) {
        const uint8_t* key = session_key_query_(user_id);
        if (key) {
            auto crypto = std::make_unique<VoiceCrypto>();
            crypto->setSessionKey(key);
            client_cryptos_[user_id] = std::move(crypto);
        }
    }

    NEVO_LOG_INFO("server", "UDP mapping added: user={} -> {}:{}",
                  user_id.value,
                  udp_endpoint.address().to_string(),
                  udp_endpoint.port());
}

void AudioRelay::updateClientChannel(UserId user_id, ChannelId channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        it->second.current_channel = channel_id;
        NEVO_LOG_DEBUG("server", "UDP mapping channel updated: user={} -> channel={}",
                       user_id.value, channel_id.value);
    }
}

void AudioRelay::removeClientMapping(UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        std::string endpoint_key = it->second.udp_endpoint.address().to_string() + ":"
                                 + std::to_string(it->second.udp_endpoint.port());
        endpoint_to_user_.erase(endpoint_key);
        client_map_.erase(it);

        // 销毁该用户的 VoiceCrypto
        client_cryptos_.erase(user_id);

        NEVO_LOG_INFO("server", "UDP mapping removed: user={}", user_id.value);
    }
}

// ============================================================
// 配置
// ============================================================

void AudioRelay::setChannelManager(std::shared_ptr<ChannelManager> channel_mgr) {
    std::lock_guard<std::mutex> lock(mutex_);
    channel_mgr_ = std::move(channel_mgr);
}

void AudioRelay::setUdpSocket(std::shared_ptr<UdpSocket> socket) {
    std::lock_guard<std::mutex> lock(mutex_);
    udp_socket_ = std::move(socket);
}

void AudioRelay::setIoContext(boost::asio::io_context& io_ctx) {
    std::lock_guard<std::mutex> lock(mutex_);
    io_ctx_ = &io_ctx;
}

void AudioRelay::setSessionKeyQuery(SessionKeyQuery query) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_key_query_ = std::move(query);
}

// ============================================================
// 内部方法
// ============================================================

UserId AudioRelay::findUserByEndpoint(const boost::asio::ip::udp::endpoint& endpoint) const {
    std::string key = endpoint.address().to_string() + ":"
                    + std::to_string(endpoint.port());

    auto it = endpoint_to_user_.find(key);
    return it != endpoint_to_user_.end() ? it->second : INVALID_USER_ID;
}

std::vector<boost::asio::ip::udp::endpoint> AudioRelay::getChannelPeers(
    UserId sender_id, ChannelId channel_id) const
{
    std::vector<boost::asio::ip::udp::endpoint> peers;

    std::lock_guard<std::mutex> lock(mutex_);

    // 如果有频道管理器，获取频道内的所有用户
    if (channel_mgr_) {
        Channel* channel = channel_mgr_->getChannel(channel_id);
        if (!channel) {
            return peers;
        }

        const auto& users = channel->users();
        for (UserId uid : users) {
            if (uid == sender_id) continue; // 不转发给发送者自己

            auto it = client_map_.find(uid);
            if (it != client_map_.end()) {
                peers.push_back(it->second.udp_endpoint);
            }
        }
    } else {
        // 没有频道管理器，回退到映射表中的频道匹配
        for (const auto& [uid, mapping] : client_map_) {
            if (uid == sender_id) continue;
            if (mapping.current_channel == channel_id) {
                peers.push_back(mapping.udp_endpoint);
            }
        }
    }

    return peers;
}

uint64_t AudioRelay::packetsRelayed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return packets_relayed_;
}

uint64_t AudioRelay::packetsDropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return packets_dropped_;
}

} // namespace nevo
