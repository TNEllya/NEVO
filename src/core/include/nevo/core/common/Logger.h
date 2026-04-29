#pragma once
/**
 * @file Logger.h
 * @brief NEVO 日志接口
 *
 * 基于 spdlog 的封装，支持分类标签。
 * 当 spdlog 不可用时使用内置简单实现。
 */

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nevo {

class LoggerManager {
public:
    static LoggerManager& instance() {
        static LoggerManager mgr;
        return mgr;
    }

    void initialize(const std::string& log_file = "",
                    spdlog::level::level_enum level = spdlog::level::info) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
        if (!log_file.empty()) {
            setLogFileImpl(log_file);
        }
        initialize_unlocked(level);
    }

    /// 设置日志文件输出路径（可在 initialize 之后调用）
    void setLogFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        setLogFileImpl(path);
    }

    std::shared_ptr<spdlog::logger> get(const std::string& category) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            initialize_unlocked(level_);
        }
        auto it = loggers_.find(category);
        if (it != loggers_.end()) return it->second;

        auto logger = std::make_shared<spdlog::logger>(category);
        loggers_[category] = logger;
        return logger;
    }

private:
    LoggerManager() = default;

    void initialize_unlocked(spdlog::level::level_enum level) {
        if (initialized_) return;
        spdlog::set_level(level);
        auto logger = std::make_shared<spdlog::logger>("nevo");
        logger->set_level(level);
        loggers_["nevo"] = logger;
        loggers_["core"] = logger;
        loggers_["audio"] = logger;
        loggers_["network"] = logger;
        loggers_["server"] = logger;
        loggers_["client"] = logger;
        loggers_["permission"] = logger;
        loggers_["protocol"] = logger;
        initialized_ = true;
    }

    void setLogFileImpl(const std::string& path) {
        // Add a basic file sink to the default logger
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, true);
        for (auto& [name, logger] : loggers_) {
            logger->sinks().push_back(file_sink);
        }
    }

    bool initialized_ = false;
    spdlog::level::level_enum level_ = spdlog::level::info;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers_;
};

} // namespace nevo

#define NEVO_LOG_IMPL(level, category, ...) \
    do { \
        auto logger = ::nevo::LoggerManager::instance().get(category); \
        if (logger) logger->level(__VA_ARGS__); \
    } while (0)

#define NEVO_LOG_TRACE(category, ...) NEVO_LOG_IMPL(trace, category, __VA_ARGS__)
#define NEVO_LOG_DEBUG(category, ...) NEVO_LOG_IMPL(debug, category, __VA_ARGS__)
#define NEVO_LOG_INFO(category, ...)  NEVO_LOG_IMPL(info,  category, __VA_ARGS__)
#define NEVO_LOG_WARN(category, ...)  NEVO_LOG_IMPL(warn,  category, __VA_ARGS__)
#define NEVO_LOG_ERROR(category, ...) NEVO_LOG_IMPL(error, category, __VA_ARGS__)
#define NEVO_LOG_CRITICAL(category, ...) NEVO_LOG_IMPL(critical, category, __VA_ARGS__)
