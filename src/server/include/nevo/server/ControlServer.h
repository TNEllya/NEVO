#pragma once
/**
 * @file ControlServer.h
 * @brief JSON-over-TCP control server for IPC with Python GUI
 *
 * Listens on a configurable localhost port and accepts JSON commands
 * from the Python GUI. Supports request-response and event push.
 *
 * Protocol:
 *   Request:  {"id": N, "command": "cmd", "params": {...}}\n
 *   Response: {"id": N, "status": "ok"|"error", "data": {...}}\n
 *   Event:    {"event": "name", "data": {...}}\n
 */

#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

namespace nevo {

class ServerCore;

/// Simple JSON value type (string-keyed map with string/number/bool/list values)
struct ControlJson {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;

    bool bool_val = false;
    double num_val = 0;
    std::string str_val;
    std::vector<ControlJson> arr_val;
    std::unordered_map<std::string, ControlJson> obj_val;

    // Convenience accessors
    bool has(const std::string& key) const { return obj_val.count(key) > 0; }
    const ControlJson& operator[](const std::string& key) const {
        static ControlJson null_val;
        auto it = obj_val.find(key);
        return it != obj_val.end() ? it->second : null_val;
    }
    std::string str(const std::string& key, const std::string& def = "") const {
        auto it = obj_val.find(key);
        return (it != obj_val.end() && it->second.type == String) ? it->second.str_val : def;
    }
    double num(const std::string& key, double def = 0) const {
        auto it = obj_val.find(key);
        return (it != obj_val.end() && it->second.type == Number) ? it->second.num_val : def;
    }
    int integer(const std::string& key, int def = 0) const {
        return static_cast<int>(num(key, def));
    }
    bool boolean(const std::string& key, bool def = false) const {
        auto it = obj_val.find(key);
        return (it != obj_val.end() && it->second.type == Bool) ? it->second.bool_val : def;
    }

    // Factory helpers
    static ControlJson make_null() { return {}; }
    static ControlJson make_bool(bool v) { ControlJson j; j.type = Bool; j.bool_val = v; return j; }
    static ControlJson make_num(double v) { ControlJson j; j.type = Number; j.num_val = v; return j; }
    static ControlJson make_str(const std::string& v) { ControlJson j; j.type = String; j.str_val = v; return j; }
    static ControlJson make_arr() { ControlJson j; j.type = Array; return j; }
    static ControlJson make_obj() { ControlJson j; j.type = Object; return j; }
};

/// Serialize ControlJson to string
std::string controlJsonToString(const ControlJson& j);

/// Parse string to ControlJson
ControlJson parseControlJson(const std::string& s);

/**
 * @class ControlServer
 * @brief TCP control server for GUI IPC
 */
class ControlServer {
public:
    using Tcp = boost::asio::ip::tcp;

    /**
     * @brief Constructor
     * @param io_ctx  Boost.Asio I/O context
     * @param port    Listening port (default 24432)
     * @param core    ServerCore pointer (non-owning)
     */
    ControlServer(boost::asio::io_context& io_ctx, uint16_t port, ServerCore* core);

    /// Start accepting connections
    void start();

    /// Stop server and disconnect all clients
    void stop();

    /// Broadcast an event to all connected GUI clients
    void broadcastEvent(const std::string& event_name, const ControlJson& data);

    /// Check if the server is running
    bool isRunning() const { return running_; }

private:
    /// Accept loop
    void doAccept();

    /// Handle a connected client
    boost::asio::awaitable<void> handleClient(std::shared_ptr<Tcp::socket> socket);

    /// Process a single JSON command
    ControlJson handleCommand(const ControlJson& request);

    /// Command handlers
    ControlJson cmdGetStatus(const ControlJson& params);
    ControlJson cmdGetSessions(const ControlJson& params);
    ControlJson cmdGetChannels(const ControlJson& params);
    ControlJson cmdKickUser(const ControlJson& params);
    ControlJson cmdDisconnectAll(const ControlJson& params);
    ControlJson cmdShutdown(const ControlJson& params);
    ControlJson cmdGetConfig(const ControlJson& params);
    ControlJson cmdBanUser(const ControlJson& params);
    ControlJson cmdSetAdminPassword(const ControlJson& params);
    ControlJson cmdSetConfig(const ControlJson& params);
    ControlJson cmdConfigureSsl(const ControlJson& params);
    ControlJson cmdCreateChannel(const ControlJson& params);
    ControlJson cmdDeleteChannel(const ControlJson& params);
    ControlJson cmdUpdateChannel(const ControlJson& params);
    ControlJson cmdReorderChannels(const ControlJson& params);

    boost::asio::io_context& io_ctx_;
    Tcp::acceptor acceptor_;
    ServerCore* core_;
    uint16_t port_;
    std::atomic<bool> running_{false};

    /// Connected GUI clients
    std::mutex clients_mutex_;
    std::unordered_set<std::shared_ptr<Tcp::socket>> clients_;
};

} // namespace nevo
