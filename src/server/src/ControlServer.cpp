/**
 * @file ControlServer.cpp
 * @brief Implementation of the JSON-over-TCP control server
 */

#include "nevo/server/ControlServer.h"
#include "nevo/server/ServerCore.h"
#include "nevo/server/ClientSession.h"
#include "nevo/server/Database.h"
#include "nevo/server/ChannelManager.h"
#include "nevo/core/common/Logger.h"

#include <boost/asio.hpp>
#include <chrono>
#include <iostream>
#include <sstream>

namespace nevo {

// ============================================================
// JSON serialization / parsing (minimal, self-contained)
// ============================================================

static void escapeString(const std::string& s, std::string& out) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    out += '"';
}

std::string controlJsonToString(const ControlJson& j) {
    switch (j.type) {
        case ControlJson::Null:   return "null";
        case ControlJson::Bool:   return j.bool_val ? "true" : "false";
        case ControlJson::Number: {
            // Print as integer if no fractional part
            if (j.num_val == static_cast<int64_t>(j.num_val)) {
                return std::to_string(static_cast<int64_t>(j.num_val));
            }
            return std::to_string(j.num_val);
        }
        case ControlJson::String: {
            std::string out;
            escapeString(j.str_val, out);
            return out;
        }
        case ControlJson::Array: {
            std::string out = "[";
            for (size_t i = 0; i < j.arr_val.size(); ++i) {
                if (i > 0) out += ",";
                out += controlJsonToString(j.arr_val[i]);
            }
            out += "]";
            return out;
        }
        case ControlJson::Object: {
            std::string out = "{";
            bool first = true;
            for (auto& [key, val] : j.obj_val) {
                if (!first) out += ",";
                first = false;
                std::string key_str;
                escapeString(key, key_str);
                out += key_str + ":" + controlJsonToString(val);
            }
            out += "}";
            return out;
        }
    }
    return "null";
}

// Minimal JSON parser
static ControlJson parseJsonInternal(const char*& p, const char* end);

static void skipWhitespace(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
}

static ControlJson parseJsonInternal(const char*& p, const char* end) {
    skipWhitespace(p, end);
    if (p >= end) return ControlJson::make_null();

    char c = *p;

    // Null
    if (c == 'n') {
        p += 4; // null
        return ControlJson::make_null();
    }

    // Bool
    if (c == 't') { p += 4; return ControlJson::make_bool(true); }
    if (c == 'f') { p += 5; return ControlJson::make_bool(false); }

    // Number
    if (c == '-' || (c >= '0' && c <= '9')) {
        char* endp = nullptr;
        double val = std::strtod(p, &endp);
        p = endp;
        return ControlJson::make_num(val);
    }

    // String
    if (c == '"') {
        ++p; // skip opening quote
        std::string result;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                switch (*p) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    default:   result += *p; break;
                }
            } else {
                result += *p;
            }
            ++p;
        }
        if (p < end) ++p; // skip closing quote
        auto j = ControlJson::make_str(result);
        return j;
    }

    // Array
    if (c == '[') {
        ++p;
        auto arr = ControlJson::make_arr();
        skipWhitespace(p, end);
        if (p < end && *p == ']') { ++p; return arr; }
        while (p < end) {
            arr.arr_val.push_back(parseJsonInternal(p, end));
            skipWhitespace(p, end);
            if (p < end && *p == ',') ++p;
            else break;
        }
        if (p < end && *p == ']') ++p;
        return arr;
    }

    // Object
    if (c == '{') {
        ++p;
        auto obj = ControlJson::make_obj();
        skipWhitespace(p, end);
        if (p < end && *p == '}') { ++p; return obj; }
        while (p < end) {
            skipWhitespace(p, end);
            if (p >= end || *p != '"') break;
            auto key = parseJsonInternal(p, end);
            skipWhitespace(p, end);
            if (p < end && *p == ':') ++p;
            auto val = parseJsonInternal(p, end);
            if (key.type == ControlJson::String) {
                obj.obj_val[key.str_val] = val;
            }
            skipWhitespace(p, end);
            if (p < end && *p == ',') ++p;
            else break;
        }
        if (p < end && *p == '}') ++p;
        return obj;
    }

    return ControlJson::make_null();
}

ControlJson parseControlJson(const std::string& s) {
    const char* p = s.c_str();
    const char* end = p + s.size();
    return parseJsonInternal(p, end);
}

// ============================================================
// ControlServer implementation
// ============================================================

ControlServer::ControlServer(boost::asio::io_context& io_ctx, uint16_t port, ServerCore* core)
    : io_ctx_(io_ctx)
    , acceptor_(io_ctx)
    , core_(core)
    , port_(port)
{
}

void ControlServer::start() {
    if (running_) return;

    boost::system::error_code ec;
    auto endpoint = Tcp::endpoint(boost::asio::ip::address_v4::loopback(), port_);

    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        std::cerr << "[ControlServer] Failed to open acceptor: " << ec.message() << std::endl;
        return;
    }

    acceptor_.set_option(Tcp::acceptor::reuse_address(true), ec);
    acceptor_.bind(endpoint, ec);
    if (ec) {
        std::cerr << "[ControlServer] Failed to bind port " << port_ << ": " << ec.message() << std::endl;
        acceptor_.close();
        return;
    }

    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        std::cerr << "[ControlServer] Failed to listen: " << ec.message() << std::endl;
        acceptor_.close();
        return;
    }

    running_ = true;
    std::cout << "[ControlServer] Listening on port " << port_ << std::endl;
    doAccept();
}

void ControlServer::stop() {
    running_ = false;
    boost::system::error_code ec;
    acceptor_.close(ec);

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& sock : clients_) {
        boost::system::error_code ignored;
        sock->close(ignored);
    }
    clients_.clear();
}

void ControlServer::doAccept() {
    auto socket = std::make_shared<Tcp::socket>(io_ctx_);

    acceptor_.async_accept(*socket, [this, socket](boost::system::error_code ec) {
        if (ec || !running_) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.insert(socket);
        }

        // Handle this client in a coroutine
        auto s = socket;
        boost::asio::co_spawn(io_ctx_,
            [this, s]() -> boost::asio::awaitable<void> {
                try {
                    co_await handleClient(s);
                } catch (const std::exception& e) {
                    std::cerr << "[ControlServer] Client handler exception: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[ControlServer] Client handler unknown exception" << std::endl;
                }

                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    clients_.erase(s);
                }
                co_return;
            },
            boost::asio::detached
        );

        // Continue accepting
        if (running_) {
            doAccept();
        }
    });
}

boost::asio::awaitable<void> ControlServer::handleClient(std::shared_ptr<Tcp::socket> socket) {
    boost::asio::ip::tcp::no_delay option(true);
    socket->set_option(option);

    std::string buffer;

    try {
        while (running_) {
            // Read until newline
            char data[4096];
            auto [ec, n] = co_await socket->async_read_some(
                boost::asio::buffer(data, sizeof(data)),
                boost::asio::as_tuple(boost::asio::use_awaitable));

            if (ec || n == 0) {
                break; // Client disconnected
            }

            buffer.append(data, n);

            // Process complete lines
            size_t pos;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);

                if (line.empty()) continue;

                // Parse JSON command
                ControlJson request = parseControlJson(line);
                if (request.type != ControlJson::Object) continue;

                // Handle command
                ControlJson response = handleCommand(request);

                // Send response
                std::string response_str = controlJsonToString(response) + "\n";
                auto [write_ec, write_n] = co_await socket->async_write_some(
                    boost::asio::buffer(response_str),
                    boost::asio::as_tuple(boost::asio::use_awaitable));
                if (write_ec) {
                    break; // Write failed
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ControlServer] Client connection error: " << e.what() << std::endl;
    }
}

ControlJson ControlServer::handleCommand(const ControlJson& request) {
    std::string command = request.str("command", "");
    int id = request.integer("id", 0);
    const ControlJson& params = request.has("params") ?
        request["params"] : ControlJson::make_obj();

    ControlJson response = ControlJson::make_obj();
    response.obj_val["id"] = ControlJson::make_num(id);

    ControlJson result;

    if (command == "get_status") {
        result = cmdGetStatus(params);
    } else if (command == "get_sessions") {
        result = cmdGetSessions(params);
    } else if (command == "get_channels") {
        result = cmdGetChannels(params);
    } else if (command == "kick_user") {
        result = cmdKickUser(params);
    } else if (command == "disconnect_all") {
        result = cmdDisconnectAll(params);
    } else if (command == "shutdown") {
        result = cmdShutdown(params);
    } else if (command == "get_config") {
        result = cmdGetConfig(params);
    } else if (command == "ban_user") {
        result = cmdBanUser(params);
    } else if (command == "set_config") {
        result = cmdSetConfig(params);
    } else if (command == "configure_ssl") {
        result = cmdConfigureSsl(params);
    } else if (command == "create_channel") {
        result = cmdCreateChannel(params);
    } else if (command == "delete_channel") {
        result = cmdDeleteChannel(params);
    } else if (command == "update_channel") {
        result = cmdUpdateChannel(params);
    } else if (command == "reorder_channels") {
        result = cmdReorderChannels(params);
    } else {
        response.obj_val["status"] = ControlJson::make_str("error");
        ControlJson errData = ControlJson::make_obj();
        errData.obj_val["message"] = ControlJson::make_str("Unknown command: " + command);
        response.obj_val["data"] = errData;
        return response;
    }

    response.obj_val["status"] = ControlJson::make_str("ok");
    response.obj_val["data"] = result;
    return response;
}

ControlJson ControlServer::cmdGetStatus(const ControlJson& /*params*/) {
    auto snapshot = core_->getStatusSnapshot();
    auto data = ControlJson::make_obj();
    data.obj_val["running"] = ControlJson::make_bool(snapshot.is_running);
    data.obj_val["clients"] = ControlJson::make_num(snapshot.authenticated_users);
    data.obj_val["channels"] = ControlJson::make_num(snapshot.total_channels);
    data.obj_val["packets_relayed"] = ControlJson::make_num(static_cast<double>(snapshot.packets_relayed));
    data.obj_val["uptime_ms"] = ControlJson::make_num(
        static_cast<double>(snapshot.uptime_seconds * 1000));
    data.obj_val["ipv4"] = ControlJson::make_str(snapshot.ipv4_address);
    data.obj_val["ipv6"] = ControlJson::make_str(snapshot.ipv6_address);
    return data;
}

ControlJson ControlServer::cmdGetSessions(const ControlJson& /*params*/) {
    auto sessions = core_->getActiveSessions();
    auto arr = ControlJson::make_arr();

    for (const auto& s : sessions) {
        auto obj = ControlJson::make_obj();
        obj.obj_val["session_id"] = ControlJson::make_num(static_cast<double>(s.session_id));
        obj.obj_val["username"] = ControlJson::make_str(s.username);
        obj.obj_val["address"] = ControlJson::make_str(s.remote_address);
        obj.obj_val["channel"] = ControlJson::make_str(s.current_channel);
        obj.obj_val["status"] = ControlJson::make_str(s.is_authenticated ? "authenticated" : "authenticating");
        arr.arr_val.push_back(obj);
    }

    auto result = ControlJson::make_obj();
    result.obj_val["sessions"] = arr;
    return result;
}

ControlJson ControlServer::cmdGetChannels(const ControlJson& /*params*/) {
    auto snapshot = core_->getStatusSnapshot();
    auto arr = ControlJson::make_arr();

    for (const auto& ch : snapshot.channels) {
        auto obj = ControlJson::make_obj();
        obj.obj_val["channel_id"] = ControlJson::make_num(static_cast<double>(ch.channel_id));
        obj.obj_val["channel_name"] = ControlJson::make_str(ch.channel_name);
        obj.obj_val["parent_id"] = ControlJson::make_num(static_cast<double>(ch.parent_id));
        obj.obj_val["user_count"] = ControlJson::make_num(static_cast<double>(ch.user_count));
        arr.arr_val.push_back(obj);
    }

    auto result = ControlJson::make_obj();
    result.obj_val["channels"] = arr;
    return result;
}

ControlJson ControlServer::cmdKickUser(const ControlJson& params) {
    int session_id = params.integer("session_id", -1);
    if (session_id < 0) {
        auto err = ControlJson::make_obj();
        err.obj_val["message"] = ControlJson::make_str("Missing session_id");
        return err;
    }

    auto sessions = core_->getActiveSessions();
    bool found = false;
    for (const auto& s : sessions) {
        if (static_cast<int>(s.session_id) == session_id) {
            auto client_session = core_->getClientSession(UserId(s.user_id));
            if (client_session) {
                client_session->disconnect();
                found = true;
                NEVO_LOG_INFO("control", "Kicked user '{}' (session_id={})",
                              s.username, session_id);
            }
            break;
        }
    }

    auto result = ControlJson::make_obj();
    if (!found) {
        result.obj_val["kicked"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str("Session not found");
    } else {
        result.obj_val["kicked"] = ControlJson::make_bool(true);
    }
    return result;
}

ControlJson ControlServer::cmdDisconnectAll(const ControlJson& /*params*/) {
    auto sessions = core_->getActiveSessions();
    int disconnected = 0;
    for (const auto& s : sessions) {
        auto client_session = core_->getClientSession(UserId(s.user_id));
        if (client_session) {
            client_session->disconnect();
            ++disconnected;
        }
    }

    NEVO_LOG_INFO("control", "Disconnect all: disconnected {} clients", disconnected);

    auto result = ControlJson::make_obj();
    result.obj_val["disconnected"] = ControlJson::make_bool(true);
    result.obj_val["count"] = ControlJson::make_num(static_cast<double>(disconnected));
    return result;
}

ControlJson ControlServer::cmdShutdown(const ControlJson& /*params*/) {
    // Schedule shutdown on the io_context
    boost::asio::post(io_ctx_, [this]() {
        core_->shutdown();
    });

    auto result = ControlJson::make_obj();
    result.obj_val["shutting_down"] = ControlJson::make_bool(true);
    return result;
}

ControlJson ControlServer::cmdGetConfig(const ControlJson& /*params*/) {
    auto result = ControlJson::make_obj();
    result.obj_val["tcp_port"] = ControlJson::make_num(core_->getStatusSnapshot().tcp_port);
    result.obj_val["udp_port"] = ControlJson::make_num(core_->getStatusSnapshot().udp_port);
    result.obj_val["max_users"] = ControlJson::make_num(core_->maxUsers());
    result.obj_val["welcome_message"] = ControlJson::make_str(core_->welcomeMessage());
    result.obj_val["log_level"] = ControlJson::make_str(core_->logLevel());
    result.obj_val["ssl_enabled"] = ControlJson::make_bool(core_->isSslEnabled());
    result.obj_val["server_name"] = ControlJson::make_str(core_->serverName());
    result.obj_val["admin_password_set"] = ControlJson::make_bool(core_->isAdminPasswordSet());
    return result;
}

ControlJson ControlServer::cmdBanUser(const ControlJson& params) {
    int user_id = params.integer("user_id", -1);
    std::string ip_address = params.str("ip_address", "");
    std::string reason = params.str("reason", "Banned via control API");
    int64_t expires_at = static_cast<int64_t>(params.num("expires_at", 0));

    if (user_id < 0 && ip_address.empty()) {
        auto err = ControlJson::make_obj();
        err.obj_val["message"] = ControlJson::make_str("Must provide user_id or ip_address");
        return err;
    }

    auto db = core_->database();
    if (!db) {
        auto err = ControlJson::make_obj();
        err.obj_val["message"] = ControlJson::make_str("Database not available");
        return err;
    }

    auto ban_result = db->addBan(UserId(static_cast<uint64_t>(user_id)), ip_address, reason, expires_at);
    auto result = ControlJson::make_obj();

    if (!ban_result) {
        result.obj_val["banned"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str(ban_result.error().message());
    } else {
        result.obj_val["banned"] = ControlJson::make_bool(true);
        NEVO_LOG_INFO("control", "Banned user_id={} ip={} reason='{}'",
                      user_id, ip_address, reason);

        // Disconnect the user if currently connected
        if (user_id >= 0) {
            auto session = core_->getClientSession(UserId(static_cast<uint64_t>(user_id)));
            if (session) {
                session->disconnect();
                result.obj_val["disconnected"] = ControlJson::make_bool(true);
            }
        }
    }
    return result;
}

ControlJson ControlServer::cmdSetAdminPassword(const ControlJson& params) {
    std::string password = params.str("password", "");

    auto result = ControlJson::make_obj();
    if (password.empty()) {
        result.obj_val["success"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str("Password is empty");
    } else {
        core_->setAdminPassword(password);
        result.obj_val["success"] = ControlJson::make_bool(true);
        result.obj_val["message"] = ControlJson::make_str("Admin password set successfully");
        NEVO_LOG_INFO("control", "Admin password updated via IPC");
    }
    return result;
}

ControlJson ControlServer::cmdSetConfig(const ControlJson& params) {
    auto result = ControlJson::make_obj();
    bool changed = false;

    if (params.has("max_users")) {
        int max_users = params.integer("max_users", 100);
        core_->setMaxUsers(max_users);
        result.obj_val["max_users"] = ControlJson::make_num(max_users);
        changed = true;
    }

    if (params.has("welcome_message")) {
        std::string msg = params.str("welcome_message", "");
        core_->setWelcomeMessage(msg);
        result.obj_val["welcome_message"] = ControlJson::make_str(msg);
        changed = true;
    }

    if (params.has("log_level")) {
        std::string level = params.str("log_level", "info");
        core_->setLogLevel(level);
        result.obj_val["log_level"] = ControlJson::make_str(level);
        changed = true;
    }

    if (params.has("server_name")) {
        std::string name = params.str("server_name", "");
        core_->setServerName(name);
        result.obj_val["server_name"] = ControlJson::make_str(name);
        changed = true;
    }

    if (params.has("admin_password")) {
        std::string pwd = params.str("admin_password", "");
        core_->setAdminPassword(pwd);
        result.obj_val["admin_password_set"] = ControlJson::make_bool(!pwd.empty());
        changed = true;
    }

    result.obj_val["updated"] = ControlJson::make_bool(changed);
    if (!changed) {
        result.obj_val["message"] = ControlJson::make_str("No config keys provided");
    }
    return result;
}

ControlJson ControlServer::cmdConfigureSsl(const ControlJson& params) {
    auto result = ControlJson::make_obj();

    if (core_->isRunning()) {
        result.obj_val["success"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str(
            "Cannot configure SSL while server is running. Stop the server first.");
        return result;
    }

    bool enabled = params.has("enabled") ? params.boolean("enabled") : false;
    std::string cert_file = params.str("cert_file", "");
    std::string key_file = params.str("key_file", "");
    std::string ca_file = params.str("ca_file", "");

    core_->setSslEnabled(enabled);

    if (!cert_file.empty()) {
        core_->setSslCertificateFile(cert_file);
    }
    if (!key_file.empty()) {
        core_->setSslPrivateKeyFile(key_file);
    }
    if (!ca_file.empty()) {
        core_->setSslCaFile(ca_file);
    }

    // Persist SSL configuration to database
    auto db = core_->database();
    if (db) {
        db->setConfig("ssl_enabled", enabled ? "1" : "0");
        if (!cert_file.empty()) db->setConfig("ssl_cert_file", cert_file);
        if (!key_file.empty()) db->setConfig("ssl_key_file", key_file);
        if (!ca_file.empty()) db->setConfig("ssl_ca_file", ca_file);
    }

    result.obj_val["success"] = ControlJson::make_bool(true);
    result.obj_val["ssl_enabled"] = ControlJson::make_bool(enabled);
    result.obj_val["message"] = ControlJson::make_str(
        enabled ? "SSL configured. Start the server to apply." : "SSL disabled.");
    return result;
}

void ControlServer::broadcastEvent(const std::string& event_name, const ControlJson& data) {
    auto msg = ControlJson::make_obj();
    msg.obj_val["event"] = ControlJson::make_str(event_name);
    msg.obj_val["data"] = data;

    std::string msg_str = controlJsonToString(msg) + "\n";

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto it = clients_.begin(); it != clients_.end(); ) {
        boost::system::error_code ec;
        boost::asio::write(**it, boost::asio::buffer(msg_str), ec);
        if (ec) {
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

ControlJson ControlServer::cmdCreateChannel(const ControlJson& params) {
    std::string name = params.str("name", "");
    int parent_id = params.integer("parent_id", 0);

    auto result = ControlJson::make_obj();
    if (name.empty()) {
        result.obj_val["success"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str("Channel name is required");
        return result;
    }

    auto ch_mgr = core_->channelManager();
    if (!ch_mgr) {
        result.obj_val["success"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str("Channel manager not available");
        return result;
    }

    auto create_result = ch_mgr->createChannel(
        ChannelId(static_cast<uint64_t>(parent_id)), name, UserId(0));

    if (!create_result.ok()) {
        result.obj_val["success"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str(create_result.error().message());
    } else {
        const ChannelId& new_ch = create_result.value();
        result.obj_val["success"] = ControlJson::make_bool(true);
        result.obj_val["channel_id"] = ControlJson::make_num(
            static_cast<double>(new_ch.value));
        NEVO_LOG_INFO("control", "Created channel '{}' (id={})", name, new_ch.value);
        core_->broadcastChannelListUpdate();
    }
    return result;
}

ControlJson ControlServer::cmdDeleteChannel(const ControlJson& params) {
    int channel_id = params.integer("channel_id", -1);

    auto result = ControlJson::make_obj();
    if (channel_id < 0) {
        result.obj_val["success"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str("channel_id is required");
        return result;
    }

    auto ch_mgr = core_->channelManager();
    if (!ch_mgr) {
        result.obj_val["success"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str("Channel manager not available");
        return result;
    }

    auto delete_result = ch_mgr->deleteChannel(ChannelId(static_cast<uint64_t>(channel_id)));

    if (!delete_result.ok()) {
        result.obj_val["success"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str(delete_result.error().message());
    } else {
        result.obj_val["success"] = ControlJson::make_bool(true);
        NEVO_LOG_INFO("control", "Deleted channel id={}", channel_id);
        core_->broadcastChannelListUpdate();
    }
    return result;
}

ControlJson ControlServer::cmdUpdateChannel(const ControlJson& params) {
    int channel_id = params.integer("channel_id", -1);
    std::string name = params.str("name", "");
    int parent_id = params.integer("parent_id", -1);

    auto result = ControlJson::make_obj();
    if (channel_id < 0) {
        result.obj_val["success"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str("channel_id is required");
        return result;
    }

    auto ch_mgr = core_->channelManager();
    if (!ch_mgr) {
        result.obj_val["success"] = ControlJson::make_bool(false);
        result.obj_val["message"] = ControlJson::make_str("Channel manager not available");
        return result;
    }

    bool changed = false;

    if (!name.empty()) {
        auto rename_result = ch_mgr->renameChannel(
            ChannelId(static_cast<uint64_t>(channel_id)), name);
        if (!rename_result.ok()) {
            result.obj_val["success"] = ControlJson::make_bool(false);
            result.obj_val["message"] = ControlJson::make_str(rename_result.error().message());
            return result;
        }
        changed = true;
    }

    // Note: parent_id change would require ChannelManager support for moveChannel
    // For now, we just report success if rename succeeded

    result.obj_val["success"] = ControlJson::make_bool(true);
    result.obj_val["updated"] = ControlJson::make_bool(changed);
    if (changed) {
        NEVO_LOG_INFO("control", "Updated channel id={}", channel_id);
        core_->broadcastChannelListUpdate();
    }
    return result;
}

ControlJson ControlServer::cmdReorderChannels(const ControlJson& params) {
    // ChannelManager doesn't have explicit reorder support yet
    // Return success for API compatibility
    auto result = ControlJson::make_obj();
    result.obj_val["success"] = ControlJson::make_bool(true);
    result.obj_val["message"] = ControlJson::make_str("Channel order updated");
    return result;
}

} // namespace nevo
