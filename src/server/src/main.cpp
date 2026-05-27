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
#include <chrono>
#include <thread>
#include <csignal>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

namespace {

/// 全局服务器核心指针（用于信号处理）
nevo::ServerCore* g_server_core = nullptr;

/// 全局 io_context 指针（用于信号处理时停止）
boost::asio::io_context* g_io_context = nullptr;

// ============================================================
// Web 管理界面自动启动
// ============================================================

#ifdef _WIN32

/**
 * @brief 获取当前 EXE 所在目录路径
 */
std::string getExeDirectory() {
    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0) return ".";
    std::string path(buffer, len);
    size_t pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos) : ".";
}

/**
 * @brief 启动 Python Web 代理（后台进程）
 *
 * 在 nevo_server.exe 同目录下的 web/ 子目录中查找 server.py 并启动。
 * 优先使用 python3，其次 python。
 */
void launchWebProxy() {
    std::string exe_dir = getExeDirectory();
    std::string web_dir = exe_dir + "\\web";
    std::string server_py = web_dir + "\\server.py";

    // 检查 server.py 是否存在
    DWORD attrs = GetFileAttributesA(server_py.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        std::cout << "[WebUI] server.py not found at " << server_py
                  << " — web UI will not be available" << std::endl;
        return;
    }

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    // 尝试 python3 → python 依次查找
    std::string cmd = "python -c \"import http.server; print('ok')\"";
    std::string python = "python";

    // 构建命令行：python server.py（在 web 目录下运行）
    std::string cmd_line = "cmd /c \"cd /d \"" + web_dir + "\" && " + python + " server.py\"";

    // cmd_line 需要可修改的缓冲区
    std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back('\0');

    if (CreateProcessA(
            nullptr, cmd_buf.data(),
            nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr,
            &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::cout << "[WebUI] Web proxy started (PID: " << pi.dwProcessId << ")" << std::endl;
        NEVO_LOG_INFO("server", "Web proxy started on http://127.0.0.1:8090 (PID: {})", pi.dwProcessId);
    } else {
        DWORD err = GetLastError();
        std::cout << "[WebUI] Failed to start web proxy: error " << err << std::endl;
        NEVO_LOG_WARN("server", "Failed to start web proxy: error {}", err);
    }
}

/**
 * @brief 在默认浏览器中打开 Web 管理界面
 *
 * 等待 Web 代理启动后就就绪后打开 http://127.0.0.1:8090。
 * 使用异步方式，不阻塞主线程。
 */
void openBrowserAsync() {
    // 在独立线程中等待服务就绪后打开浏览器
    std::thread([url = std::string("http://127.0.0.1:8090")]() {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::string curl_cmd = std::string("curl -s --max-time 1 ") + url + "/api/health >nul 2>&1";
            if (system(curl_cmd.c_str()) == 0) {
                break;
            }
        }
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }).detach();
}

#else
// Linux/macOS：使用 xdg-open / open
void launchWebProxy() {
    std::string web_dir;
    // 尝试从可执行文件路径推断 web 目录
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        std::string exe_path(buf);
        size_t pos = exe_path.find_last_of("/");
        if (pos != std::string::npos) {
            web_dir = exe_path.substr(0, pos) + "/web";
        }
    }
    if (web_dir.empty()) web_dir = "./web";

    std::string cmd = "cd \"" + web_dir + "\" && python3 server.py &";
    if (system(cmd.c_str()) != 0) {
        cmd = "cd \"" + web_dir + "\" && python server.py &";
        system(cmd.c_str());
    }
    NEVO_LOG_INFO("server", "Web proxy started on http://127.0.0.1:8090");
}

void openBrowserAsync() {
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        system("xdg-open http://127.0.0.1:8090 2>/dev/null || open http://127.0.0.1:8090 2>/dev/null &");
    }).detach();
}
#endif

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
    nevo::ServerConfig config = nevo::ServerConfig::fromArgs(argc, argv);

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

    // 自动启动 Web 管理界面
    launchWebProxy();
    openBrowserAsync();

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
