#pragma once

#include "nevo/core/common/Types.h"
#include "nevo/network/UdpSocket.h"
#include "nevo/network/VoiceCrypto.h"

#include <boost/asio.hpp>

#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>
#include <optional>

namespace nevo {

class ChannelManager;
class VoiceCrypto;

struct VideoClientMapping {
    UserId user_id;
    ChannelId channel_id;
    boost::asio::ip::udp::endpoint endpoint;
};

using VideoSessionKeyQuery = std::function<const uint8_t*(UserId)>;

class VideoRelay {
public:
    VideoRelay();
    ~VideoRelay();

    void setChannelManager(std::shared_ptr<ChannelManager> mgr);
    void setUdpSocket(std::shared_ptr<UdpSocket> socket);
    void setIoContext(boost::asio::io_context& io_ctx);
    void setSessionKeyQuery(VideoSessionKeyQuery query);

    void handleVideoPacket(const uint8_t* data, uint32_t size,
                           const boost::asio::ip::udp::endpoint& sender);

    void addClientMapping(UserId user_id,
                          const boost::asio::ip::udp::endpoint& ep,
                          ChannelId channel_id);
    void removeClientMapping(UserId user_id);
    void updateClientChannel(UserId user_id, ChannelId channel_id);

    uint64_t packetsRelayed() const { return packets_relayed_.load(); }
    uint64_t packetsDropped() const { return packets_dropped_.load(); }
    uint64_t packetsReceived() const { return packets_received_.load(); }

private:
    std::optional<UserId> findUserByEndpoint(
        const boost::asio::ip::udp::endpoint& ep) const;
    std::vector<boost::asio::ip::udp::endpoint> getChannelPeersLocked(
        UserId sender_id, ChannelId channel_id) const;
    VoiceCrypto* getOrCreateCryptoForUserLocked(UserId user_id);
    void _dumpClientMap();

    std::mutex mutex_;
    std::shared_ptr<ChannelManager> channel_mgr_;
    std::shared_ptr<UdpSocket> udp_socket_;
    boost::asio::io_context* io_ctx_ = nullptr;
    VideoSessionKeyQuery session_key_query_;

    std::unordered_map<UserId, VideoClientMapping> client_map_;
    std::unordered_map<std::string, UserId> endpoint_to_user_;
    std::unordered_map<UserId, std::unique_ptr<VoiceCrypto>> client_cryptos_;

    std::atomic<uint64_t> packets_received_{0};
    std::atomic<uint64_t> packets_relayed_{0};
    std::atomic<uint64_t> packets_dropped_{0};
};

} // namespace nevo
