#pragma once
#include <string>
#include <cstdint>
#include "nevo/core/common/Result.h"

namespace nevo {

struct FileTransferConfig {
    bool limit_upload_speed = false;
    int upload_speed_kbps = 0;
    bool limit_download_speed = false;
    int download_speed_kbps = 0;
    int max_concurrent_uploads = 3;
    int max_concurrent_downloads = 3;
    int max_file_size_mb = 100;
    std::string upload_dir = "uploads";
};

struct ServerConfig {
    uint16_t tcp_port = 24430;
    uint16_t udp_port = 24431;
    std::string db_path = "nevo_server.db";
    int threads = 4;
    std::string log_level = "info";
    std::string server_name = "NEVO Server";
    int max_users = 100;
    std::string welcome_message = "Welcome to the NEVO server!";
    FileTransferConfig file_transfer;

    /// Validate config values. Returns error if invalid.
    Result<void> validate() const;

    /// Load from JSON file. Returns true on success.
    bool loadFromFile(const std::string& path);

    /// Save to JSON file. Returns true on success.
    bool saveToFile(const std::string& path) const;

    /// Load from CLI arguments (argc, argv). Returns the config path if provided.
    static ServerConfig fromArgs(int argc, char* argv[]);
};

} // namespace nevo
