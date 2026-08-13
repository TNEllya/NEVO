#include "nevo/server/VideoRelay.h"
#include "nevo/server/ChannelManager.h"
#include "nevo/core/common/Logger.h"
#include "nevo/core/protocol/PacketCodec.h"
#include "nevo/network/UdpSocket.h"
#include "nevo/network/VoiceCrypto.h"

#include "video.pb.h"

#include <optional>
#include <string>
#include <vector>
#include <utility>
#include <cstring>
#include <algorithm>

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

VideoRelay::VideoRelay() = default;

VideoRelay::~VideoRelay() {
    NEVO_LOG_INFO("server", "VideoRelay destroyed (received={}, relayed={}, dropped={})",
                  packets_received_.load(), packets_relayed_.load(), packets_dropped_.load());
}

// ============================================================
// 配置
// ============================================================

void VideoRelay::setChannelManager(std::shared_ptr<ChannelManager> mgr) {
    std::lock_guard<std::mutex> lock(mutex_);
    channel_mgr_ = std::move(mgr);
}

void VideoRelay::setUdpSocket(std::shared_ptr<UdpSocket> socket) {
    std::lock_guard<std::mutex> lock(mutex_);
    udp_socket_ = std::move(socket);
}

void VideoRelay::setIoContext(boost::asio::io_context& io_ctx) {
    std::lock_guard<std::mutex> lock(mutex_);
    io_ctx_ = &io_ctx;
}

void VideoRelay::setSessionKeyQuery(VideoSessionKeyQuery query) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_key_query_ = std::move(query);
}

// ============================================================
// 视频包处理 (核心转发逻辑)
// ============================================================

void VideoRelay::handleVideoPacket(const uint8_t* data, uint32_t size,
                                    const boost::asio::ip::udp::endpoint& sender) {
    packets_received_.fetch_add(1);

    if (!data || size < 2) {
        ++packets_dropped_;
        return;
    }

    // 解析包头
    uint16_t header_size = 0;
    std::memcpy(&header_size, data, 2);
    if (header_size == 0 || static_cast<uint32_t>(2 + header_size) > size) {
        ++packets_dropped_;
        return;
    }

    video::VideoPacketHeader header;
    if (!header.ParseFromArray(data + 2, static_cast<int>(header_size))) {
        ++packets_dropped_;
        return;
    }

    UserId header_sender_id(header.sender_id());
    ChannelId packet_channel(header.channel_id());

    NEVO_LOG_INFO("video_relay", "RX pkt: sender_id={}, channel_id={}, frame_type={}, "
                  "size={}, header_size={}, addr={}:{}",
                  header_sender_id.value, packet_channel.value, header.frame_type(),
                  size, header_size,
                  sender.address().to_string(), sender.port());

    // ---- 发送者身份解析（fail-closed）：身份以映射表为准，不信任包头 ----
    bool sender_resolved = false;
    bool need_crypto_auth = false;
    UserId sender_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto mapped = findUserByEndpointLocked(sender);
        if (!mapped) {
            // 未知端点：若包头携带 sender_id，走"解密认证自动注册"
            // （NAT/frp 穿透场景端点会变化）——能使用该用户会话密钥解密
            // = 持有密钥 = 身份可信，认证通过后才建立端点映射。
            if (!header_sender_id) {
                NEVO_LOG_WARN("video_relay", "RX pkt: endpoint {}:{} not mapped and no header sender_id, dropping",
                              sender.address().to_string(), sender.port());
                ++packets_dropped_;
                return;
            }
            need_crypto_auth = true;
            sender_id = header_sender_id;
        } else {
            sender_id = *mapped;
            if (header_sender_id && header_sender_id != sender_id) {
                NEVO_LOG_WARN("video_relay", "RX pkt: header sender_id={} != mapped user={}, dropping",
                              header_sender_id.value, sender_id.value);
                ++packets_dropped_;
                return;
            }
            if (!header_sender_id) {
                // 包头无 sender_id，用映射身份回填（供接收方识别）
                sender_resolved = true;
                header.set_sender_id(sender_id.value);
            }
        }
    }

    // --- 更新映射 + 收集 peers（认证路径延迟到解密成功后执行） ---
    ChannelId sender_channel;
    std::vector<boost::asio::ip::udp::endpoint> peers;
    if (!need_crypto_auth) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string sender_key = endpointKey(sender);

        auto& user_mappings = client_map_[sender_id];
        auto mit = user_mappings.find(sender_key);
        if (mit != user_mappings.end()) {
            // 该端点已存在，更新频道信息
            sender_channel = packet_channel ? packet_channel : mit->second.channel_id;
            mit->second.channel_id = sender_channel;
        } else {
            // 端点已通过 addClientMapping 注册（前面 fail-closed 检查保证），此处兜底创建
            sender_channel = packet_channel;
            if (!sender_channel) {
                ++packets_dropped_;
                return;
            }
            VideoClientMapping mapping;
            mapping.user_id = sender_id;
            mapping.channel_id = sender_channel;
            mapping.endpoint = sender;
            user_mappings[sender_key] = mapping;
            endpoint_to_user_[sender_key] = sender_id;
        }

        // ---- 频道成员校验（防跨频道视频注入） ----
        if (channel_mgr_) {
            Channel* channel = channel_mgr_->getChannel(sender_channel);
            if (!channel || !channel->hasUser(sender_id)) {
                NEVO_LOG_WARN("video_relay", "RX pkt: user={} is not a member of channel={}, dropping",
                              sender_id.value, sender_channel.value);
                ++packets_dropped_;
                return;
            }
        }

        peers = getChannelPeersLocked(sender_id, sender_channel, sender_key);
    }

    // --- Prepare AAD for decryption and re-encryption ---
    // Save original header size BEFORE any modification
    const uint16_t original_header_size = header_size;

    // Decryption AAD: ALWAYS use the ORIGINAL header bytes (as encrypted by sender)
    const uint8_t* decrypt_aad_ptr = data + 2;
    uint32_t decrypt_aad_size = original_header_size;

    // Re-encryption AAD: use updated header bytes if sender was resolved
    const uint8_t* encrypt_aad_ptr = data + 2;
    uint32_t encrypt_aad_size = original_header_size;
    std::vector<uint8_t> updated_header_bytes;

    if (sender_resolved) {
        updated_header_bytes.resize(2 + header.ByteSizeLong());
        updated_header_bytes[0] = static_cast<uint8_t>(header.ByteSizeLong() & 0xFF);
        updated_header_bytes[1] = static_cast<uint8_t>((header.ByteSizeLong() >> 8) & 0xFF);
        header.SerializeToArray(updated_header_bytes.data() + 2, static_cast<int>(header.ByteSizeLong()));
        header_size = static_cast<uint16_t>(header.ByteSizeLong());
        encrypt_aad_ptr = updated_header_bytes.data() + 2;
        encrypt_aad_size = static_cast<uint32_t>(header.ByteSizeLong());
    }

    NEVO_LOG_INFO("video_relay", "PEERS for user_id={} channel={}: count={}, client_map_size={}",
                  sender_id.value, sender_channel.value, peers.size(), client_map_.size());

    if (!need_crypto_auth && peers.empty()) {
        NEVO_LOG_INFO("video_relay", "No peers to forward to (empty peer list)");
        return;
    }

    if (!udp_socket_ || !io_ctx_) {
        ++packets_dropped_;
        return;
    }

    // --- Decrypt: use SENDER's key with ORIGINAL header AAD ---
    const uint8_t* encrypted_frame = data + 2 + original_header_size;
    uint32_t encrypted_frame_size = size - 2 - original_header_size;

    if (encrypted_frame_size < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE) {
        ++packets_dropped_;
        return;
    }

    const uint8_t* nonce = encrypted_frame;
    const uint8_t* ciphertext = encrypted_frame + XCHACHA_NONCE_SIZE;
    size_t ct_len = encrypted_frame_size - XCHACHA_NONCE_SIZE;

    std::vector<uint8_t> plaintext;
    const uint8_t* sender_key = nullptr;

    if (session_key_query_) {
        sender_key = session_key_query_(sender_id);
    }

    // fail-closed：没有发送者密钥上下文时直接丢弃，绝不原样转发
    if (!sender_key) {
        NEVO_LOG_WARN("video_relay", "NO sender_key for user_id={}, dropping packet (fail-closed)",
                      sender_id.value);
        ++packets_dropped_;
        return;
    }

    {
        auto decrypted = VoiceCrypto::decryptWithKey(
            sender_key, ciphertext, ct_len,
            nonce, XCHACHA_NONCE_SIZE,
            decrypt_aad_ptr, decrypt_aad_size);
        if (decrypted) {
            plaintext = std::move(*decrypted);
            NEVO_LOG_DEBUG("video_relay", "Decrypt SUCCESS: user_id={}, plaintext_len={}",
                           sender_id.value, plaintext.size());
        } else {
            ++packets_dropped_;
            NEVO_LOG_WARN("video_relay", "Failed to decrypt video from user_id={} (decrypt_aad_size={}, ct_len={})",
                          sender_id.value, decrypt_aad_size, ct_len);
            return;
        }
    }

    // --- 未知端点 + 解密认证通过：建立端点映射 + 成员校验 + 收集 peers ---
    if (need_crypto_auth) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 端点已被其他用户占用则拒绝（防端点劫持）
        auto existing = findUserByEndpointLocked(sender);
        if (existing && *existing != sender_id) {
            NEVO_LOG_WARN("video_relay", "RX pkt: endpoint {}:{} claimed by user={}, dropping",
                          sender.address().to_string(), sender.port(), existing->value);
            ++packets_dropped_;
            return;
        }

        sender_channel = packet_channel;
        if (!sender_channel) {
            ++packets_dropped_;
            return;
        }

        // ---- 频道成员校验（防跨频道视频注入） ----
        if (channel_mgr_) {
            Channel* channel = channel_mgr_->getChannel(sender_channel);
            if (!channel || !channel->hasUser(sender_id)) {
                NEVO_LOG_WARN("video_relay", "RX pkt: user={} is not a member of channel={}, dropping",
                              sender_id.value, sender_channel.value);
                ++packets_dropped_;
                return;
            }
        }

        std::string auth_sender_key = endpointKey(sender);
        auto& user_mappings = client_map_[sender_id];
        auto mit = user_mappings.find(auth_sender_key);
        if (mit != user_mappings.end()) {
            mit->second.channel_id = sender_channel;
        } else {
            // 解密认证成功 = 持有该用户会话密钥，允许绑定端点
            VideoClientMapping mapping;
            mapping.user_id = sender_id;
            mapping.channel_id = sender_channel;
            mapping.endpoint = sender;
            user_mappings[auth_sender_key] = mapping;
            endpoint_to_user_[auth_sender_key] = sender_id;
            NEVO_LOG_INFO("video_relay",
                          "UDP endpoint auto-registered via crypto auth: user={} -> {}:{} (channel={})",
                          sender_id.value,
                          sender.address().to_string(), sender.port(),
                          sender_channel.value);
        }

        peers = getChannelPeersLocked(sender_id, sender_channel, auth_sender_key);

        if (peers.empty()) {
            NEVO_LOG_INFO("video_relay", "No peers to forward to (empty peer list)");
            return;
        }
    }

    // --- Forward: re-encrypt for each receiver with UPDATED header AAD ---
    int fwd_count = 0;
    for (const auto& peer_endpoint : peers) {
        auto receiver_id_opt = findUserByEndpoint(peer_endpoint);
        if (!receiver_id_opt) {
            NEVO_LOG_WARN("video_relay", "FWD SKIP: peer_endpoint not found in endpoint_to_user_ map: {}", 
                          peer_endpoint.address().to_string() + ":" + std::to_string(peer_endpoint.port()));
            continue;
        }
        UserId receiver_id = *receiver_id_opt;

        std::vector<uint8_t> packet_to_send;

        // 接收者加密上下文（shared_ptr 持有引用，防止并发销毁）
        std::shared_ptr<VoiceCrypto> receiver_crypto;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            receiver_crypto = getOrCreateCryptoForUserLocked(receiver_id);
        }

        if (!receiver_crypto) {
            NEVO_LOG_WARN("video_relay", "FWD SKIP: no crypto for receiver_id={}", receiver_id.value);
            continue;
        }

        auto reencrypted = receiver_crypto->encrypt(
            plaintext.data(), plaintext.size(),
            encrypt_aad_ptr, encrypt_aad_size);
        if (reencrypted.empty()) {
            NEVO_LOG_WARN("video_relay", "FWD SKIP: reencrypt failed for receiver_id={}", receiver_id.value);
            continue;
        }

        packet_to_send.reserve(2 + header_size + reencrypted.size());
        if (sender_resolved) {
            packet_to_send.insert(packet_to_send.end(), updated_header_bytes.data(),
                                 updated_header_bytes.data() + 2 + header_size);
        } else {
            packet_to_send.insert(packet_to_send.end(), data, data + 2 + header_size);
        }
        packet_to_send.insert(packet_to_send.end(), reencrypted.begin(), reencrypted.end());

        auto data_copy = std::make_shared<std::vector<uint8_t>>(std::move(packet_to_send));
        auto target_str = peer_endpoint.address().to_string() + ":" + std::to_string(peer_endpoint.port());

        NEVO_LOG_INFO("video_relay", "FWD to receiver_id={} at {}: pkt_size={}", 
                       receiver_id.value, target_str, data_copy->size());

        boost::asio::co_spawn(*io_ctx_,
            [this, data_copy, peer_endpoint, target_str, receiver_id]() -> boost::asio::awaitable<void> {
                auto ec = co_await udp_socket_->asyncSendTo(
                    data_copy->data(), static_cast<uint32_t>(data_copy->size()), peer_endpoint);
                if (ec) {
                    NEVO_LOG_WARN("video_relay", "SEND FAILED to {}: ec={}", target_str, ec.message());
                }
            },
            boost::asio::detached);
        fwd_count++;
    }

    packets_relayed_.fetch_add(fwd_count);
}

// ============================================================
// 客户端映射管理
// ============================================================

void VideoRelay::addClientMapping(UserId user_id,
                                   const boost::asio::ip::udp::endpoint& ep,
                                   ChannelId channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string ep_key = endpointKey(ep);

    // 按端点键插入（不影响同一账号其他设备的映射）
    VideoClientMapping mapping;
    mapping.user_id = user_id;
    mapping.channel_id = channel_id;
    mapping.endpoint = ep;
    client_map_[user_id][ep_key] = mapping;
    endpoint_to_user_[ep_key] = user_id;

    // 仅为该用户创建 VoiceCrypto（仅当不存在，避免覆盖其他设备的加密上下文）
    if (session_key_query_ &&
        client_cryptos_.find(user_id) == client_cryptos_.end()) {
        const uint8_t* key = session_key_query_(user_id);
        if (key) {
            auto crypto = std::make_shared<VoiceCrypto>();
            crypto->setSessionKey(key);
            client_cryptos_[user_id] = crypto;
        }
    }
}

void VideoRelay::removeClientMapping(UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        for (const auto& [ep_key, mapping] : it->second) {
            endpoint_to_user_.erase(ep_key);
        }
        client_map_.erase(it);
        client_cryptos_.erase(user_id);
        NEVO_LOG_INFO("video_relay", "Removed all mappings for user_id={}", user_id.value);
    }
}

void VideoRelay::removeClientMapping(UserId user_id,
                                     const boost::asio::ip::udp::endpoint& ep) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string ep_key = endpointKey(ep);

    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        auto mit = it->second.find(ep_key);
        if (mit != it->second.end()) {
            it->second.erase(mit);
            endpoint_to_user_.erase(ep_key);

            NEVO_LOG_INFO("video_relay", "Removed mapping for user_id={} endpoint={}:{}",
                          user_id.value,
                          ep.address().to_string(), ep.port());

            // 该用户已无其他设备映射，销毁其 VoiceCrypto
            if (it->second.empty()) {
                client_map_.erase(it);
                client_cryptos_.erase(user_id);
            }
        }
    }
}

void VideoRelay::updateClientChannel(UserId user_id, ChannelId channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        // 遍历该用户的所有端点（支持同一账号多设备）
        for (auto& [ep_key, mapping] : it->second) {
            mapping.channel_id = channel_id;
        }
    }
}

// ============================================================
// 内部方法
// ============================================================

std::optional<UserId> VideoRelay::findUserByEndpoint(
    const boost::asio::ip::udp::endpoint& ep) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return findUserByEndpointLocked(ep);
}

std::optional<UserId> VideoRelay::findUserByEndpointLocked(
    const boost::asio::ip::udp::endpoint& ep) const {
    std::string ep_key = endpointKey(ep);
    auto it = endpoint_to_user_.find(ep_key);
    if (it != endpoint_to_user_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<boost::asio::ip::udp::endpoint> VideoRelay::getChannelPeersLocked(
    UserId sender_id, ChannelId channel_id,
    const std::string& sender_endpoint_key) const {
    std::vector<boost::asio::ip::udp::endpoint> peers;

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
                peers.push_back(mapping.endpoint);
            }
        }
    } else {
        // 没有频道管理器，回退到映射表中的频道匹配
        for (const auto& [uid, mappings] : client_map_) {
            for (const auto& [ep_key, mapping] : mappings) {
                if (ep_key == sender_endpoint_key) continue;
                if (mapping.channel_id == channel_id) {
                    peers.push_back(mapping.endpoint);
                }
            }
        }
    }

    return peers;
}

// 在调用方已持有 mutex_ 锁的前提下，获取或创建指定用户的 VoiceCrypto 实例
std::shared_ptr<VoiceCrypto> VideoRelay::getOrCreateCryptoForUserLocked(UserId user_id) {
    auto it = client_cryptos_.find(user_id);
    if (it != client_cryptos_.end() && it->second) {
        return it->second;
    }

    // 动态创建：通过 session_key_query 获取密钥
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

void VideoRelay::_dumpClientMap() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (client_map_.empty()) {
        NEVO_LOG_WARN("video_relay", "  client_map is EMPTY");
        return;
    }
    for (const auto& [uid, mappings] : client_map_) {
        for (const auto& [ep_key, mapping] : mappings) {
            auto ep_str = mapping.endpoint.address().to_string() + ":" + std::to_string(mapping.endpoint.port());
            NEVO_LOG_WARN("video_relay", "  user={} endpoint={} channel={}",
                          uid.value, ep_str, mapping.channel_id.value);
        }
    }
}

} // namespace nevo
