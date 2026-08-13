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

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

namespace nevo {
namespace {

std::string endpointKey(const boost::asio::ip::udp::endpoint& ep) {
    auto addr = ep.address();
    if (addr.is_v6()) {
        auto v6 = addr.to_v6();
        if (v6.is_v4_mapped()) {
            auto v4 = boost::asio::ip::make_address_v4(
                boost::asio::ip::v4_mapped, v6);
            return v4.to_string() + ":" + std::to_string(ep.port());
        }
        return "[" + addr.to_string() + "]:" + std::to_string(ep.port());
    }
    return addr.to_string() + ":" + std::to_string(ep.port());
}

} // namespace

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
    // UDP 路径：发送者身份必须从映射表解析（不信任包头中的 sender_id）
    relayVoicePacket(data, size, sender_endpoint, INVALID_USER_ID);
}

void AudioRelay::handleVoicePacket(const uint8_t* data, uint32_t size,
                                    const boost::asio::ip::udp::endpoint& sender_endpoint,
                                    UserId known_sender_id) {
    // TCP 隧道路径：身份来自已认证的 TCP 会话
    relayVoicePacket(data, size, sender_endpoint, known_sender_id);
}

void AudioRelay::relayVoicePacket(const uint8_t* data, uint32_t size,
                                  const boost::asio::ip::udp::endpoint& sender_endpoint,
                                  UserId known_sender_id) {
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

    const UserId header_sender_id(header->sender_id());
    const ChannelId channel_id(header->channel_id());

    // 单次加锁：解析身份 + 成员校验 + 收集 peers + 预收集 crypto 上下文
    UserId sender_id;
    std::vector<boost::asio::ip::udp::endpoint> peers;
    std::shared_ptr<VoiceCrypto> sender_crypto;
    std::unordered_map<UserId, std::shared_ptr<VoiceCrypto>> receiver_crypto_map;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // ---- 1. 解析发送者身份（fail-closed） ----
        if (known_sender_id) {
            sender_id = known_sender_id;
        } else {
            // UDP 路径：发送者必须已在映射表中（认证后由 addClientMapping 建立）。
            // 不自动为"自称任意 user_id"的未知端点创建映射——这是语音注入漏洞的根源。
            sender_id = findUserByEndpointLocked(sender_endpoint);
        }
        if (!sender_id) {
            NEVO_LOG_WARN("server", "Voice packet rejected: unknown sender {}:{}",
                          sender_endpoint.address().to_string(), sender_endpoint.port());
            ++packets_dropped_;
            return;
        }

        // 包头声明的 sender_id 必须与实际身份一致（防伪造）
        if (header_sender_id && header_sender_id != sender_id) {
            NEVO_LOG_WARN("server", "Voice packet rejected: header sender_id={} != authenticated user={}",
                          header_sender_id.value, sender_id.value);
            ++packets_dropped_;
            return;
        }

        // ---- 2. 频道成员校验（防跨频道语音注入） ----
        if (channel_mgr_) {
            Channel* channel = channel_mgr_->getChannel(channel_id);
            if (!channel || !channel->hasUser(sender_id)) {
                NEVO_LOG_WARN("server", "Voice packet rejected: user={} is not a member of channel={}",
                              sender_id.value, channel_id.value);
                ++packets_dropped_;
                return;
            }
        }

        // ---- 3. 更新/创建映射 ----
        std::string sender_key = endpointKey(sender_endpoint);
        auto& user_mappings = client_map_[sender_id];
        auto mit = user_mappings.find(sender_key);
        if (mit != user_mappings.end()) {
            mit->second.current_channel = channel_id;
        } else if (known_sender_id) {
            // 仅 TCP 隧道（已认证身份）允许自动创建映射
            ClientUdpMapping mapping;
            mapping.user_id = sender_id;
            mapping.udp_endpoint = sender_endpoint;
            mapping.current_channel = channel_id;
            user_mappings[sender_key] = mapping;
            endpoint_to_user_[sender_key] = sender_id;
            NEVO_LOG_INFO("server", "TCP tunnel mapping auto-created for user={} (channel={})",
                          sender_id.value, channel_id.value);
        } else if (!channel_mgr_) {
            // 无频道管理器（兼容/测试模式）：无法做权威成员校验，允许映射自建
            ClientUdpMapping mapping;
            mapping.user_id = sender_id;
            mapping.udp_endpoint = sender_endpoint;
            mapping.current_channel = channel_id;
            user_mappings[sender_key] = mapping;
            endpoint_to_user_[sender_key] = sender_id;
        } else {
            // UDP 路径 + 有频道管理器：未知端点一律拒绝
            NEVO_LOG_WARN("server", "Voice packet rejected: endpoint {}:{} not mapped for user={}",
                          sender_endpoint.address().to_string(), sender_endpoint.port(),
                          sender_id.value);
            ++packets_dropped_;
            return;
        }

        // ---- 4. 收集同频道 peers 与 crypto 上下文 ----
        peers = getChannelPeersLocked(sender_id, channel_id, sender_key);

        sender_crypto = getOrCreateCryptoLocked(sender_id);

        for (const auto& peer_endpoint : peers) {
            UserId receiver_id = findUserByEndpointLocked(peer_endpoint);
            if (!receiver_id) continue;
            if (receiver_id == sender_id) continue;
            auto crypto = getOrCreateCryptoLocked(receiver_id);
            if (crypto) {
                receiver_crypto_map[receiver_id] = crypto;
            }
        }
    }

    if (peers.empty()) {
        // 频道内没有其他用户，无需转发
        static thread_local uint32_t empty_peers_log_counter = 0;
        if ((empty_peers_log_counter++ % 50) == 0) {
            NEVO_LOG_INFO("server", "Voice: no peers for user={} channel={} (client_map_size={})",
                          sender_id.value, channel_id.value, client_map_.size());
        }
        return;
    }

    if (!udp_socket_ || !io_ctx_) {
        ++packets_dropped_;
        return;
    }

    // ---- 5. 解密（fail-closed：无加密上下文一律丢弃，绝不原样转发） ----
    if (!sender_crypto) {
        static thread_local uint32_t no_crypto_log_counter = 0;
        if ((no_crypto_log_counter++ % 50) == 0) {
            NEVO_LOG_WARN("server", "Voice packet dropped: no crypto context for user={} (fail-closed)",
                          sender_id.value);
        }
        ++packets_dropped_;
        return;
    }

    const uint8_t* encrypted_frame = data + header_size;
    uint32_t encrypted_frame_size = size - header_size;

    if (encrypted_frame_size < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE) {
        NEVO_LOG_WARN("server", "Voice packet too short: {} bytes (expected at least {})",
                     encrypted_frame_size, XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE);
        ++packets_dropped_;
        return;
    }

    const uint8_t* nonce = encrypted_frame;
    const uint8_t* ciphertext = encrypted_frame + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted_frame_size - XCHACHA_NONCE_SIZE;

    // AAD 为 protobuf 包头（不含 2 字节长度前缀）
    const uint8_t* aad = data + 2;
    uint32_t aad_size = header_size - 2;

    auto decrypted = sender_crypto->decrypt(
        ciphertext, ct_len, nonce, XCHACHA_NONCE_SIZE, aad, aad_size);
    if (!decrypted) {
        NEVO_LOG_WARN("server", "Failed to decrypt voice packet from user_id={}", sender_id.value);
        ++packets_dropped_;
        return;
    }
    const std::vector<uint8_t>& plaintext = *decrypted;

    // ---- 6. 转发给同频道其他用户（用接收者密钥重新加密） ----
    for (const auto& peer_endpoint : peers) {
        UserId receiver_id = findUserByEndpoint(peer_endpoint);
        if (!receiver_id || receiver_id == sender_id) continue;

        auto rc_it = receiver_crypto_map.find(receiver_id);
        if (rc_it == receiver_crypto_map.end() || !rc_it->second) {
            NEVO_LOG_TRACE("server", "No crypto context for receiver user_id={}, skipping",
                           receiver_id.value);
            continue;
        }

        // 使用接收者的密钥重新加密
        auto reencrypted = rc_it->second->encrypt(
            plaintext.data(), plaintext.size(), aad, aad_size);
        if (reencrypted.empty()) {
            NEVO_LOG_WARN("server", "Failed to re-encrypt voice packet for user_id={}",
                          receiver_id.value);
            continue;
        }

        std::vector<uint8_t> packet_to_send;
        packet_to_send.reserve(header_size + reencrypted.size());
        packet_to_send.insert(packet_to_send.end(), data, data + header_size);
        packet_to_send.insert(packet_to_send.end(), reencrypted.begin(), reencrypted.end());

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

    static thread_local uint32_t relay_log_counter = 0;
    if ((relay_log_counter++ % 100) == 0) {
        NEVO_LOG_INFO("server", "Voice relayed: user={} -> {} peers in channel={}",
                      sender_id.value, peers.size(), channel_id.value);
    }
}

// ============================================================
// 客户端映射管理
// ============================================================

void AudioRelay::addClientMapping(UserId user_id,
                                   const boost::asio::ip::udp::endpoint& udp_endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 生成端点键
    std::string endpoint_key = endpointKey(udp_endpoint);

    // 添加新映射（按端点键插入，不影响同一账号其他设备的映射）
    ClientUdpMapping mapping;
    mapping.user_id = user_id;
    mapping.udp_endpoint = udp_endpoint;
    mapping.current_channel = ChannelId(0); // 将在 updateClientChannel 中设置

    client_map_[user_id][endpoint_key] = mapping;
    endpoint_to_user_[endpoint_key] = user_id;

    // 为该用户创建 VoiceCrypto 实例并设置会话密钥（仅当不存在，避免覆盖其他设备的加密上下文）
    if (session_key_query_ &&
        client_cryptos_.find(user_id) == client_cryptos_.end()) {
        const uint8_t* key = session_key_query_(user_id);
        if (key) {
            auto crypto = std::make_shared<VoiceCrypto>();
            crypto->setSessionKey(key);
            client_cryptos_[user_id] = crypto;
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
        for (auto& [ep_key, mapping] : it->second) {
            mapping.current_channel = channel_id;
        }
        NEVO_LOG_DEBUG("server", "UDP mapping channel updated: user={} -> channel={}",
                       user_id.value, channel_id.value);
    }
}

void AudioRelay::removeClientMapping(UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        for (const auto& [ep_key, mapping] : it->second) {
            endpoint_to_user_.erase(ep_key);
        }
        client_map_.erase(it);

        // 销毁该用户的 VoiceCrypto
        client_cryptos_.erase(user_id);

        NEVO_LOG_INFO("server", "UDP mapping removed: user={}", user_id.value);
    }
}

void AudioRelay::removeClientMapping(UserId user_id,
                                     const boost::asio::ip::udp::endpoint& udp_endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string endpoint_key = endpointKey(udp_endpoint);

    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        auto mit = it->second.find(endpoint_key);
        if (mit != it->second.end()) {
            it->second.erase(mit);
            endpoint_to_user_.erase(endpoint_key);

            NEVO_LOG_INFO("server", "UDP mapping removed: user={} endpoint={}:{}",
                          user_id.value,
                          udp_endpoint.address().to_string(),
                          udp_endpoint.port());

            // 该用户已无其他设备映射，销毁其 VoiceCrypto
            if (it->second.empty()) {
                client_map_.erase(it);
                client_cryptos_.erase(user_id);
            }
        }
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

void AudioRelay::rotateClientKey(UserId user_id, const uint8_t* key) {
    if (!key) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_cryptos_.find(user_id);
    if (it != client_cryptos_.end() && it->second) {
        it->second->rotateKey(key);
    } else {
        auto crypto = std::make_shared<VoiceCrypto>();
        crypto->setSessionKey(key);
        client_cryptos_[user_id] = crypto;
    }
}

std::shared_ptr<VoiceCrypto> AudioRelay::getOrCreateCryptoLocked(UserId user_id) {
    // 调用者必须已持有 mutex_
    auto it = client_cryptos_.find(user_id);
    if (it != client_cryptos_.end() && it->second) {
        return it->second;
    }

    if (session_key_query_) {
        const uint8_t* key = session_key_query_(user_id);
        if (key) {
            auto crypto = std::make_shared<VoiceCrypto>();
            crypto->setSessionKey(key);
            client_cryptos_[user_id] = crypto;
            return crypto;
        }
    }
    return nullptr;
}

// ============================================================
// 内部方法
// ============================================================

UserId AudioRelay::findUserByEndpoint(const boost::asio::ip::udp::endpoint& endpoint) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return findUserByEndpointLocked(endpoint);
}

UserId AudioRelay::findUserByEndpointLocked(const boost::asio::ip::udp::endpoint& endpoint) const {
    std::string key = endpointKey(endpoint);

    auto it = endpoint_to_user_.find(key);
    return it != endpoint_to_user_.end() ? it->second : INVALID_USER_ID;
}

std::vector<boost::asio::ip::udp::endpoint> AudioRelay::getChannelPeersLocked(
    UserId sender_id, ChannelId channel_id,
    const std::string& sender_endpoint_key) const
{
    std::vector<boost::asio::ip::udp::endpoint> peers;

    // 注意：调用者必须已持有 mutex_（无锁版本）

    // 如果有频道管理器，获取频道内的所有用户
    if (channel_mgr_) {
        Channel* channel = channel_mgr_->getChannel(channel_id);
        if (!channel) {
            return peers;
        }

        const auto& users = channel->users();
        for (UserId uid : users) {
            auto it = client_map_.find(uid);
            if (it == client_map_.end()) {
                continue;
            }
            // 遍历该用户的所有端点（支持同一账号多设备），排除发送者自身端点
            for (const auto& [ep_key, mapping] : it->second) {
                if (ep_key == sender_endpoint_key) continue; // 不转发给发送者自身端点
                peers.push_back(mapping.udp_endpoint);
            }
        }
    } else {
        // 没有频道管理器，回退到映射表中的频道匹配
        for (const auto& [uid, mappings] : client_map_) {
            for (const auto& [ep_key, mapping] : mappings) {
                if (ep_key == sender_endpoint_key) continue;
                if (mapping.current_channel == channel_id) {
                    peers.push_back(mapping.udp_endpoint);
                }
            }
        }
    }

    return peers;
}

std::vector<boost::asio::ip::udp::endpoint> AudioRelay::getChannelPeers(
    UserId sender_id, ChannelId channel_id,
    const std::string& sender_endpoint_key) const
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
            auto it = client_map_.find(uid);
            if (it == client_map_.end()) {
                continue;
            }
            // 遍历该用户的所有端点（支持同一账号多设备），排除发送者自身端点
            for (const auto& [ep_key, mapping] : it->second) {
                if (ep_key == sender_endpoint_key) continue;
                peers.push_back(mapping.udp_endpoint);
            }
        }
    } else {
        // 没有频道管理器，回退到映射表中的频道匹配
        for (const auto& [uid, mappings] : client_map_) {
            for (const auto& [ep_key, mapping] : mappings) {
                if (ep_key == sender_endpoint_key) continue;
                if (mapping.current_channel == channel_id) {
                    peers.push_back(mapping.udp_endpoint);
                }
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
