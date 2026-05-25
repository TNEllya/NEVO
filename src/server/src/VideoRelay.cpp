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
    if (header_size == 0 || 2 + header_size > size) {
        ++packets_dropped_;
        return;
    }

    video::VideoPacketHeader header;
    if (!header.ParseFromArray(data + 2, static_cast<int>(header_size))) {
        ++packets_dropped_;
        return;
    }

    UserId sender_id(header.sender_id());
    ChannelId packet_channel(header.channel_id());

    NEVO_LOG_INFO("video_relay", "RX pkt: sender_id={}, channel_id={}, frame_type={}, "
                  "size={}, header_size={}, addr={}:{}",
                  sender_id.value, packet_channel.value, header.frame_type(),
                  size, header_size,
                  sender.address().to_string(), sender.port());

    bool sender_resolved = false;

    if (!sender_id) {
        NEVO_LOG_WARN("video_relay", "RX pkt: sender_id=0, trying endpoint lookup");
        auto sender_id_opt = findUserByEndpoint(sender);
        if (!sender_id_opt) {
            NEVO_LOG_WARN("video_relay", "RX pkt: endpoint lookup FAILED for {}:{}",
                         sender.address().to_string(), sender.port());
            ++packets_dropped_;
            return;
        }
        sender_id = *sender_id_opt;
        sender_resolved = true;
        header.set_sender_id(sender_id.value);
        NEVO_LOG_INFO("video_relay", "RX pkt: resolved sender_id={} via endpoint", sender_id.value);
    }

    // --- 单次加锁：更新映射 + 收集 peers ---
    ChannelId sender_channel;
    std::vector<boost::asio::ip::udp::endpoint> peers;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = client_map_.find(sender_id);
        if (it != client_map_.end()) {
            std::string old_key = it->second.endpoint.address().to_string() + ":" +
                                  std::to_string(it->second.endpoint.port());
            std::string new_key = sender.address().to_string() + ":" + std::to_string(sender.port());
            if (old_key != new_key) {
                NEVO_LOG_INFO("video_relay", "Endpoint changed for user_id={}: {} -> {}",
                             sender_id.value, old_key, new_key);
                endpoint_to_user_.erase(old_key);
                endpoint_to_user_[new_key] = sender_id;
                it->second.endpoint = sender;
            }
            sender_channel = packet_channel ? packet_channel : it->second.channel_id;
            it->second.channel_id = sender_channel;
        } else {
            NEVO_LOG_INFO("video_relay", "NEW client_map entry: user_id={}, channel_id={}, addr={}:{}",
                         sender_id.value, packet_channel.value,
                         sender.address().to_string(), sender.port());
            sender_channel = packet_channel;
            if (!sender_channel) {
                ++packets_dropped_;
                return;
            }
            VideoClientMapping mapping;
            mapping.user_id = sender_id;
            mapping.channel_id = sender_channel;
            mapping.endpoint = sender;
            client_map_[sender_id] = mapping;
            std::string new_key = sender.address().to_string() + ":" + std::to_string(sender.port());
            endpoint_to_user_[new_key] = sender_id;

            if (session_key_query_) {
                const uint8_t* key = session_key_query_(sender_id);
                if (key) {
                    auto crypto = std::make_unique<VoiceCrypto>();
                    crypto->setSessionKey(key);
                    client_cryptos_[sender_id] = std::move(crypto);
                }
            }
        }

        peers = getChannelPeersLocked(sender_id, sender_channel);
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

    if (peers.empty()) {
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

    bool use_per_client_crypto = false;
    std::vector<uint8_t> plaintext;
    const uint8_t* sender_key = nullptr;

    if (session_key_query_) {
        sender_key = session_key_query_(sender_id);
    }

    if (sender_key) {
        std::string key_hex;
        for (int i = 0; i < 8; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x", sender_key[i]);
            key_hex += buf;
        }
        NEVO_LOG_INFO("video_relay", "DECRYPT: user_id={}, decrypt_aad_size={}, ct_len={}, key_prefix={}",
                       sender_id.value, decrypt_aad_size, ct_len, key_hex);

        auto decrypted = VoiceCrypto::decryptWithKey(
            sender_key, ciphertext, ct_len,
            nonce, XCHACHA_NONCE_SIZE,
            decrypt_aad_ptr, decrypt_aad_size);
        if (decrypted) {
            plaintext = std::move(*decrypted);
            use_per_client_crypto = true;
            NEVO_LOG_DEBUG("video_relay", "Decrypt SUCCESS: user_id={}, plaintext_len={}", sender_id.value, plaintext.size());
        } else {
            ++packets_dropped_;
            NEVO_LOG_WARN("video_relay", "Failed to decrypt video from user_id={} (decrypt_aad_size={}, ct_len={}, server_key_prefix={})",
                          sender_id.value, decrypt_aad_size, ct_len, key_hex);
            return;
        }
    } else {
        NEVO_LOG_WARN("video_relay", "NO sender_key for user_id={}, forwarding as-is (passthrough)", sender_id.value);
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

        if (use_per_client_crypto) {
            // Dynamically get/create receiver's crypto (under lock to avoid timing issues)
            VoiceCrypto* receiver_crypto = nullptr;
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
        } else {
            if (sender_resolved) {
                packet_to_send.reserve(2 + header_size + (encrypted_frame_size));
                packet_to_send.insert(packet_to_send.end(), updated_header_bytes.data(),
                                     updated_header_bytes.data() + 2 + header_size);
                packet_to_send.insert(packet_to_send.end(), encrypted_frame, encrypted_frame + encrypted_frame_size);
            } else {
                packet_to_send.assign(data, data + size);
            }
        }

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

    packets_relayed_.fetch_add(peers.size());
}

// ============================================================
// 客户端映射管理
// ============================================================

void VideoRelay::addClientMapping(UserId user_id,
                                   const boost::asio::ip::udp::endpoint& ep,
                                   ChannelId channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    VideoClientMapping mapping;
    mapping.user_id = user_id;
    mapping.channel_id = channel_id;
    mapping.endpoint = ep;
    client_map_[user_id] = mapping;

    std::string ep_key = ep.address().to_string() + ":" + std::to_string(ep.port());
    endpoint_to_user_[ep_key] = user_id;

    // 为该用户创建 VoiceCrypto 实例
    if (session_key_query_) {
        const uint8_t* key = session_key_query_(user_id);
        if (key) {
            auto crypto = std::make_unique<VoiceCrypto>();
            crypto->setSessionKey(key);
            client_cryptos_[user_id] = std::move(crypto);
        }
    }
}

void VideoRelay::removeClientMapping(UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        std::string ep_key = it->second.endpoint.address().to_string() +
                             ":" + std::to_string(it->second.endpoint.port());
        endpoint_to_user_.erase(ep_key);
    }
    client_map_.erase(user_id);
    client_cryptos_.erase(user_id);
}

void VideoRelay::updateClientChannel(UserId user_id, ChannelId channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_map_.find(user_id);
    if (it != client_map_.end()) {
        it->second.channel_id = channel_id;
    }
}

// ============================================================
// 内部方法
// ============================================================

std::optional<UserId> VideoRelay::findUserByEndpoint(
    const boost::asio::ip::udp::endpoint& ep) const {
    std::string ep_key = ep.address().to_string() + ":" + std::to_string(ep.port());
    auto it = endpoint_to_user_.find(ep_key);
    if (it != endpoint_to_user_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<boost::asio::ip::udp::endpoint> VideoRelay::getChannelPeersLocked(
    UserId sender_id, ChannelId channel_id) const {
    std::vector<boost::asio::ip::udp::endpoint> peers;

    if (channel_mgr_) {
        Channel* channel = channel_mgr_->getChannel(channel_id);
        if (!channel) {
            return peers;
        }
        const auto& users = channel->users();
        for (UserId uid : users) {
            if (uid == sender_id) continue;
            auto it = client_map_.find(uid);
            if (it != client_map_.end()) {
                peers.push_back(it->second.endpoint);
            }
        }
    } else {
        for (const auto& [uid, mapping] : client_map_) {
            if (uid == sender_id) continue;
            if (mapping.channel_id == channel_id) {
                peers.push_back(mapping.endpoint);
            }
        }
    }

    return peers;
}

// 在调用方已持有 mutex_ 锁的前提下，获取或创建指定用户的 VoiceCrypto 实例
VoiceCrypto* VideoRelay::getOrCreateCryptoForUserLocked(UserId user_id) {
    auto it = client_cryptos_.find(user_id);
    if (it != client_cryptos_.end() && it->second) {
        return it->second.get();
    }

    // 动态创建：通过 session_key_query 获取密钥
    if (session_key_query_) {
        const uint8_t* key = session_key_query_(user_id);
        if (key) {
            auto crypto = std::make_unique<VoiceCrypto>();
            crypto->setSessionKey(key);
            VoiceCrypto* raw_ptr = crypto.get();
            client_cryptos_[user_id] = std::move(crypto);
            return raw_ptr;
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
    for (const auto& [uid, mapping] : client_map_) {
        auto ep_str = mapping.endpoint.address().to_string() + ":" + std::to_string(mapping.endpoint.port());
        NEVO_LOG_WARN("video_relay", "  user={} endpoint={} channel={}",
                      uid.value, ep_str, mapping.channel_id.value);
    }
}

} // namespace nevo
