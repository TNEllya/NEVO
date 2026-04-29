/**
 * @file main.cpp
 * @brief NEVO VoIP 服务器入口
 *
 * 解析命令行参数，初始化日志、数据库和服务器核心，
 * 运行 io_context 线程池，处理信号以优雅关闭。
 *
 * 命令行参数：
 *   --tcp-port <port>     TCP 监听端口（默认 24800）
 *   --udp-port <port>     UDP 监听端口（默认 24801）
 *   --db <path>           数据库文件路径（默认 "nevo_server.db"）
 *   --threads <count>     线程池大小（默认 = CPU 核心数）
 *   --log-level <level>   日志级别：trace/debug/info/warn/error（默认 info）
 *   --help                显示帮助信息
 *
 * 信号处理：
 *   - SIGINT (Ctrl+C)：触发优雅关闭
 *   - SIGTERM：触发优雅关闭
 */

#include "nevo/server/ServerCore.h"
#include "nevo/server/Database.h"
#include "nevo/server/ServerConfig.h"
#include "nevo/core/common/Logger.h"

#include <boost/asio.hpp>

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <csignal>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

/// 全局服务器核心指针（用于信号处理）
nevo::ServerCore* g_server_core = nullptr;

/// 全局 io_context 指针（用于信号处理时停止）
boost::asio::io_context* g_io_context = nullptr;

// ============================================================
// 帮助信息
// ============================================================

void printHelp(const char* program_name) {
    std::cout << "NEVO VoIP Server\n"
              << "\n"
              << "Usage: " << program_name << " [OPTIONS]\n"
              << "\n"
              << "Options:\n"
              << "  --config <path>       Load configuration from JSON file\n"
              << "  --tcp-port <port>     TCP listening port (default: 24430)\n"
              << "  --udp-port <port>     UDP listening port (default: 24431)\n"
              << "  --db <path>           Database file path (default: nevo_server.db)\n"
              << "  --threads <count>     Thread pool size (default: CPU cores)\n"
              << "  --log-level <level>   Log level: trace/debug/info/warn/error (default: info)\n"
              << "  --help                Show this help message\n"
              << "\n"
              << "Priority: CLI arguments > config file > defaults\n"
              << "\n"
              << "Examples:\n"
              << "  " << program_name << " --config server_config.json\n"
              << "  " << program_name << " --tcp-port 8080 --udp-port 8081\n"
              << "  " << program_name << " --db /var/lib/nevo/server.db --threads 4\n"
              << std::endl;
}

// ============================================================
// 命令行解析
// ============================================================

template <typename T>
bool parseIntSafe(const std::string& str, T& out, const std::string& name) {
    try {
        int value = std::stoi(str);
        out = static_cast<T>(value);
        return true;
    } catch (const std::invalid_argument&) {
        std::cerr << "Invalid " << name << ": '" << str << "' (not a number)" << std::endl;
        return false;
    } catch (const std::out_of_range&) {
        std::cerr << "Invalid " << name << ": '" << str << "' (out of range)" << std::endl;
        return false;
    }
}

bool parseArgs(int argc, char* argv[], nevo::ServerConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printHelp(argv[0]);
            return false;
        } else if (arg == "--tcp-port" && i + 1 < argc) {
            if (!parseIntSafe(argv[++i], config.tcp_port, "TCP port")) {
                printHelp(argv[0]);
                return false;
            }
        } else if (arg == "--udp-port" && i + 1 < argc) {
            if (!parseIntSafe(argv[++i], config.udp_port, "UDP port")) {
                printHelp(argv[0]);
                return false;
            }
        } else if (arg == "--db" && i + 1 < argc) {
            config.db_path = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            if (!parseIntSafe(argv[++i], config.threads, "thread count")) {
                printHelp(argv[0]);
                return false;
            }
        } else if (arg == "--log-level" && i + 1 < argc) {
            config.log_level = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printHelp(argv[0]);
            return false;
        }
    }
    return true;
}

// ============================================================
// 日志级别转换
// ============================================================

spdlog::level::level_enum parseLogLevel(const std::string& level) {
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info")  return spdlog::level::info;
    if (level == "warn")  return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    std::cerr << "Unknown log level: " << level << ", using info" << std::endl;
    return spdlog::level::info;
}

// ============================================================
// 信号处理
// ============================================================

void signalHandler(int signal) {
    const char* sig_name = (signal == SIGINT) ? "SIGINT" :
                           (signal == SIGTERM) ? "SIGTERM" : "UNKNOWN";
    std::cout << "\nReceived " << sig_name << ", shutting down gracefully..." << std::endl;

    if (g_server_core) {
        g_server_core->shutdown();
    }

    if (g_io_context) {
        g_io_context->stop();
    }
}

#ifdef _WIN32
/**
 * @brief Windows 控制台事件处理器
 *
 * 在 Windows 上处理 Ctrl+C 和关闭事件。
 */
BOOL WINAPI consoleHandler(DWORD event_type) {
    switch (event_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            signalHandler(SIGINT);
            return TRUE;
        default:
            return FALSE;
    }
}
#endif

} // anonymous namespace

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[]) {
    // 解析命令行参数和配置文件
    nevo::ServerConfig config = nevo::ServerConfig::fromArgs(argc, argv);
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                printHelp(argv[0]);
                return 0;
            }
        }
    }

    // 自动检测 CPU 核心数
    uint32_t thread_count = static_cast<uint32_t>(config.threads);
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) {
            thread_count = 4; // 回退默认值
        }
    }

    // 初始化日志系统
    nevo::LoggerManager::instance().initialize(
        "nevo_server.log",
        parseLogLevel(config.log_level)
    );

    NEVO_LOG_INFO("server", "========================================");
    NEVO_LOG_INFO("server", "NEVO VoIP Server Starting");
    NEVO_LOG_INFO("server", "========================================");
    NEVO_LOG_INFO("server", "Server name: {}", config.server_name);
    NEVO_LOG_INFO("server", "TCP port:     {}", config.tcp_port);
    NEVO_LOG_INFO("server", "UDP port:     {}", config.udp_port);
    NEVO_LOG_INFO("server", "Database:     {}", config.db_path);
    NEVO_LOG_INFO("server", "Threads:      {}", thread_count);
    NEVO_LOG_INFO("server", "Max users:    {}", config.max_users);
    NEVO_LOG_INFO("server", "Log level:    {}", config.log_level);

    // 创建 io_context
    boost::asio::io_context io_ctx;
    g_io_context = &io_ctx;

    // 创建并初始化服务器核心
    nevo::ServerCore server(io_ctx, config.tcp_port, config.udp_port);
    g_server_core = &server;

    auto init_result = server.initialize(config.db_path);
    if (!init_result) {
        NEVO_LOG_CRITICAL("server", "Failed to initialize server: {}", init_result.error().message());
        return 1;
    }

    // 注册信号处理器
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#endif

    // 启动服务器
    server.start();

    NEVO_LOG_INFO("server", "Server is now running");

    // 运行 io_context 线程池
    std::vector<std::thread> thread_pool;
    thread_pool.reserve(thread_count - 1);

    // 主线程也参与 io_context 运行
    for (uint32_t i = 0; i < thread_count - 1; ++i) {
        thread_pool.emplace_back([&io_ctx]() {
            try {
                io_ctx.run();
            } catch (const std::exception& e) {
                NEVO_LOG_ERROR("server", "IO thread exception: {}", e.what());
            }
        });
    }

    // 主线程运行 io_context
    try {
        io_ctx.run();
    } catch (const std::exception& e) {
        NEVO_LOG_ERROR("server", "Main IO thread exception: {}", e.what());
    }

    // 等待所有工作线程完成
    for (auto& thread : thread_pool) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // 清理全局指针
    g_server_core = nullptr;
    g_io_context = nullptr;

    NEVO_LOG_INFO("server", "========================================");
    NEVO_LOG_INFO("server", "NEVO VoIP Server Stopped");
    NEVO_LOG_INFO("server", "========================================");

    return 0;
}
