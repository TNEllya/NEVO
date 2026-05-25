#include "nevo/server/ServerConfig.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

// ============================================================
// JSON 解析说明
// ============================================================
// 当前实现使用简单的逐行字符串匹配解析 JSON 配置文件。
// 已知限制：
//   - 不支持多行字符串值
//   - 不支持嵌套 JSON 对象
//   - 不支持字符串中的转义字符（如 \" \\n 等）
//   - 不支持 JSON 数组
// TODO: 迁移到 nlohmann/json 或复用 ControlServer 中的 JSON 解析器
// ============================================================

namespace nevo {

// ---------------------------------------------------------------------------
// Helper: trim whitespace from both ends of a string
// ---------------------------------------------------------------------------
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ---------------------------------------------------------------------------
// Helper: extract the value portion after the colon in a "key": value line
// ---------------------------------------------------------------------------
static std::string extractValue(const std::string& line) {
    auto colonPos = line.find(':');
    if (colonPos == std::string::npos) return "";
    std::string val = line.substr(colonPos + 1);
    // Remove trailing comma
    auto commaPos = val.find_last_of(',');
    if (commaPos != std::string::npos) {
        val = val.substr(0, commaPos);
    }
    return trim(val);
}

// ---------------------------------------------------------------------------
// Helper: parse an integer value string, returning defaultValue on failure
// ---------------------------------------------------------------------------
static int parseInt(const std::string& val, int defaultValue) {
    if (val.empty()) return defaultValue;
    try {
        return std::stoi(val);
    } catch (...) {
        return defaultValue;
    }
}

// ---------------------------------------------------------------------------
// Helper: parse a string value (strip surrounding quotes)
// ---------------------------------------------------------------------------
static std::string parseString(const std::string& val) {
    std::string result = trim(val);
    if (result.size() >= 2 && result.front() == '"' && result.back() == '"') {
        result = result.substr(1, result.size() - 2);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Helper: check if a line contains a given JSON key
// ---------------------------------------------------------------------------
static bool hasKey(const std::string& line, const std::string& key) {
    return line.find("\"" + key + "\"") != std::string::npos;
}

// ===========================================================================
// validate
// ===========================================================================
Result<void> ServerConfig::validate() const {
    if (tcp_port == 0) {
        return Error(ResultCode::InvalidRequest, "TCP port must be between 1 and 65535");
    }
    if (udp_port == 0) {
        return Error(ResultCode::InvalidRequest, "UDP port must be between 1 and 65535");
    }
    if (tcp_port == udp_port) {
        return Error(ResultCode::InvalidRequest, "TCP and UDP ports must be different");
    }
    if (server_name.empty() || trim(server_name).empty()) {
        return Error(ResultCode::InvalidRequest, "Server name must not be empty");
    }
    if (max_users <= 0) {
        return Error(ResultCode::InvalidRequest, "Max users must be greater than 0");
    }
    if (threads <= 0) {
        return Error(ResultCode::InvalidRequest, "Thread count must be greater than 0");
    }
    if (log_level != "debug" && log_level != "info" && log_level != "warn" && log_level != "error") {
        return Error(ResultCode::InvalidRequest,
            "Log level must be one of: debug, info, warn, error");
    }
    return {};
}

// ===========================================================================
// loadFromFile
// ===========================================================================
bool ServerConfig::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '{' || trimmed[0] == '}') continue;

        std::string value = extractValue(trimmed);

        if (hasKey(trimmed, "tcp_port")) {
            tcp_port = static_cast<uint16_t>(parseInt(value, tcp_port));
        } else if (hasKey(trimmed, "udp_port")) {
            udp_port = static_cast<uint16_t>(parseInt(value, udp_port));
        } else if (hasKey(trimmed, "db_path")) {
            db_path = parseString(value);
        } else if (hasKey(trimmed, "threads")) {
            threads = parseInt(value, threads);
        } else if (hasKey(trimmed, "log_level")) {
            log_level = parseString(value);
        } else if (hasKey(trimmed, "server_name")) {
            server_name = parseString(value);
        } else if (hasKey(trimmed, "max_users")) {
            max_users = parseInt(value, max_users);
        } else if (hasKey(trimmed, "welcome_message")) {
            welcome_message = parseString(value);
        } else if (hasKey(trimmed, "ft_limit_upload_speed")) {
            file_transfer.limit_upload_speed = (value == "true" || value == "1");
        } else if (hasKey(trimmed, "ft_upload_speed_kbps")) {
            file_transfer.upload_speed_kbps = parseInt(value, file_transfer.upload_speed_kbps);
        } else if (hasKey(trimmed, "ft_limit_download_speed")) {
            file_transfer.limit_download_speed = (value == "true" || value == "1");
        } else if (hasKey(trimmed, "ft_download_speed_kbps")) {
            file_transfer.download_speed_kbps = parseInt(value, file_transfer.download_speed_kbps);
        } else if (hasKey(trimmed, "ft_max_concurrent_uploads")) {
            file_transfer.max_concurrent_uploads = parseInt(value, file_transfer.max_concurrent_uploads);
        } else if (hasKey(trimmed, "ft_max_concurrent_downloads")) {
            file_transfer.max_concurrent_downloads = parseInt(value, file_transfer.max_concurrent_downloads);
        } else if (hasKey(trimmed, "ft_max_file_size_mb")) {
            file_transfer.max_file_size_mb = parseInt(value, file_transfer.max_file_size_mb);
        } else if (hasKey(trimmed, "ft_upload_dir")) {
            file_transfer.upload_dir = parseString(value);
        }
    }

    return true;
}

// ===========================================================================
// saveToFile
// ===========================================================================
bool ServerConfig::saveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "    \"tcp_port\": " << tcp_port << ",\n";
    file << "    \"udp_port\": " << udp_port << ",\n";
    file << "    \"db_path\": \"" << db_path << "\",\n";
    file << "    \"threads\": " << threads << ",\n";
    file << "    \"log_level\": \"" << log_level << "\",\n";
    file << "    \"server_name\": \"" << server_name << "\",\n";
    file << "    \"max_users\": " << max_users << ",\n";
    file << "    \"welcome_message\": \"" << welcome_message << "\",\n";
    file << "    \"ft_limit_upload_speed\": " << (file_transfer.limit_upload_speed ? "true" : "false") << ",\n";
    file << "    \"ft_upload_speed_kbps\": " << file_transfer.upload_speed_kbps << ",\n";
    file << "    \"ft_limit_download_speed\": " << (file_transfer.limit_download_speed ? "true" : "false") << ",\n";
    file << "    \"ft_download_speed_kbps\": " << file_transfer.download_speed_kbps << ",\n";
    file << "    \"ft_max_concurrent_uploads\": " << file_transfer.max_concurrent_uploads << ",\n";
    file << "    \"ft_max_concurrent_downloads\": " << file_transfer.max_concurrent_downloads << ",\n";
    file << "    \"ft_max_file_size_mb\": " << file_transfer.max_file_size_mb << ",\n";
    file << "    \"ft_upload_dir\": \"" << file_transfer.upload_dir << "\"\n";
    file << "}\n";

    return true;
}

// ===========================================================================
// fromArgs
// ===========================================================================
ServerConfig ServerConfig::fromArgs(int argc, char* argv[]) {
    ServerConfig config;
    std::string configPath;

    // First pass: look for --config to load the JSON file early
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            configPath = argv[++i];
            config.loadFromFile(configPath);
        }
    }

    // Second pass: apply CLI overrides
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--config" || arg == "-c") {
            // Already handled; skip the path argument
            ++i;
            continue;
        } else if (arg == "--tcp-port" && i + 1 < argc) {
            config.tcp_port = static_cast<uint16_t>(parseInt(argv[++i], config.tcp_port));
        } else if (arg == "--udp-port" && i + 1 < argc) {
            config.udp_port = static_cast<uint16_t>(parseInt(argv[++i], config.udp_port));
        } else if (arg == "--db" && i + 1 < argc) {
            config.db_path = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            config.threads = parseInt(argv[++i], config.threads);
        } else if (arg == "--log-level" && i + 1 < argc) {
            config.log_level = argv[++i];
        }
    }

    return config;
}

} // namespace nevo
