#include "nevo/client/ClientCore.h"
#include "nevo/core/common/Logger.h"
#include "nevo/core/model/User.h"
#include "nevo/core/model/Channel.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_connected{false};

static void signalHandler(int) { g_running.store(false); }

int main(int argc, char* argv[])
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string host = "127.0.0.1";
    uint16_t port = 24680;
    std::string username = "user";
    std::string password;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-h" || arg == "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if ((arg == "-u" || arg == "--username") && i + 1 < argc) {
            username = argv[++i];
        } else if ((arg == "-P" || arg == "--password") && i + 1 < argc) {
            password = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: nevo_console_client [options]\n"
                      << "  -h, --host <addr>     Server address (default: 127.0.0.1)\n"
                      << "  -p, --port <port>     Server port (default: 24680)\n"
                      << "  -u, --username <name> Username (default: user)\n"
                      << "  -P, --password <pwd>  Password\n";
            return 0;
        }
    }

    std::cout << "========================================\n"
              << "  NEVO Console Client\n"
              << "========================================\n"
              << "  Server: " << host << ":" << port << "\n"
              << "  User:   " << username << "\n"
              << "========================================\n\n";

    boost::asio::io_context io_ctx;

    nevo::ClientCore client(io_ctx);

    client.onStateChanged = [](nevo::ClientState new_state, nevo::ClientState) {
        std::cout << "[STATE] " << static_cast<int>(new_state) << std::endl;
    };

    client.onChannelList = [](const std::vector<nevo::ChannelInfo>& channels) {
        std::cout << "[CHANNELS] " << channels.size() << " channels:" << std::endl;
        for (const auto& ch : channels) {
            std::cout << "  - " << ch.name << " (id=" << ch.channel_id.value << ")" << std::endl;
        }
    };

    client.onUserJoined = [](const nevo::User& user) {
        std::cout << "[USER JOINED] " << user.username() << " (id=" << user.id().value << ")" << std::endl;
    };

    client.onUserLeft = [](nevo::UserId uid) {
        std::cout << "[USER LEFT] id=" << uid.value << std::endl;
    };

    client.onUserSpeaking = [](nevo::UserId uid, bool speaking) {
        std::cout << "[SPEAKING] user=" << uid.value << " " << (speaking ? "START" : "STOP") << std::endl;
    };

    client.onServerMessage = [](const std::string& msg) {
        std::cout << "[SERVER] " << msg << std::endl;
    };

    boost::asio::co_spawn(io_ctx,
        [&client, &host, port, &username, &password]() -> boost::asio::awaitable<void> {
            auto result = co_await client.connect(host, port, username, password);
            if (!result) {
                std::cerr << "Failed to connect: " << result.error().message() << std::endl;
                g_running.store(false);
                co_return;
            }
            g_connected.store(true);
            std::cout << "Connected! Type commands:\n"
                      << "  /join <channel_id>  - Join channel\n"
                      << "  /leave              - Leave channel\n"
                      << "  /mute               - Mute\n"
                      << "  /unmute             - Unmute\n"
                      << "  /deaf               - Deafen\n"
                      << "  /undeaf             - Undeafen\n"
                      << "  /quit               - Quit\n\n";
        }, boost::asio::detached);

    std::thread io_thread([&io_ctx]() { io_ctx.run(); });

    std::string line;
    while (g_running.load()) {
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        if (line == "/quit") {
            break;
        } else if (line.rfind("/join ", 0) == 0) {
            try {
                uint64_t cid = std::stoull(line.substr(6));
                boost::asio::co_spawn(io_ctx,
                    [&client, cid]() -> boost::asio::awaitable<void> {
                        auto result = co_await client.joinChannel(nevo::ChannelId{cid});
                        if (result) {
                            std::cout << "Joined channel " << cid << std::endl;
                        } else {
                            std::cout << "Failed to join: " << result.error().message() << std::endl;
                        }
                    }, boost::asio::detached);
            } catch (...) {
                std::cout << "Invalid channel id" << std::endl;
            }
        } else if (line == "/leave") {
            boost::asio::co_spawn(io_ctx,
                [&client]() -> boost::asio::awaitable<void> {
                    auto result = co_await client.leaveChannel();
                    if (result) {
                        std::cout << "Left channel" << std::endl;
                    } else {
                        std::cout << "Failed to leave: " << result.error().message() << std::endl;
                    }
                }, boost::asio::detached);
        } else if (line == "/mute") {
            client.setMuted(true);
            std::cout << "Muted" << std::endl;
        } else if (line == "/unmute") {
            client.setMuted(false);
            std::cout << "Unmuted" << std::endl;
        } else if (line == "/deaf") {
            client.setDeafened(true);
            std::cout << "Deafened" << std::endl;
        } else if (line == "/undeaf") {
            client.setDeafened(false);
            std::cout << "Undeafened" << std::endl;
        } else {
            std::cout << "Unknown command: " << line << std::endl;
        }
    }

    client.disconnect();
    io_ctx.stop();
    if (io_thread.joinable()) io_thread.join();

    std::cout << "Bye!" << std::endl;
    return 0;
}
