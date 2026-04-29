#pragma once
/**
 * @file spdlog.h
 * @brief Lightweight drop-in replacement for spdlog (minimal stub)
 *
 * Provides just enough API for the NEVO project's Logger.h/cpp wrapper.
 * Uses fprintf to stderr for output.
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <memory>
#include <vector>
#include <functional>

namespace spdlog {

// ============================================================
// 日志级别
// ============================================================
namespace level {
enum level_enum {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    err = 4,
    critical = 5,
    off = 6,
    n_levels = 7
};

inline const char* to_string_view(level_enum l) {
    switch (l) {
        case trace: return "trace";
        case debug: return "debug";
        case info: return "info";
        case warn: return "warning";
        case err: return "error";
        case critical: return "critical";
        default: return "off";
    }
}
} // namespace level

// ============================================================
// 源码位置
// ============================================================
struct source_loc {
    const char* filename = nullptr;
    int line = 0;
    const char* funcname = nullptr;

    source_loc() = default;
    source_loc(const char* f, int l, const char* fn) : filename(f), line(l), funcname(fn) {}
};

// ============================================================
// Logger 类
// ============================================================
class logger {
public:
    explicit logger(std::string name = "") : name_(std::move(name)) {}
    virtual ~logger() = default;

    const std::string& name() const { return name_; }

    void set_level(level::level_enum l) { level_ = l; }
    level::level_enum level() const { return level_; }

    template<typename... Args>
    void log(source_loc loc, level::level_enum lvl, const char* fmt, Args&&... args) {
        if (lvl < level_) return;
        fprintf(stderr, "[%s] [%s] %s:%d: ", name_.c_str(), level::to_string_view(lvl),
                loc.filename ? loc.filename : "?", loc.line);
        fprintf(stderr, fmt, std::forward<Args>(args)...);
        fprintf(stderr, "\n");
    }

    template<typename... Args>
    void trace(const char* fmt, Args&&... args) {
        log(source_loc{__FILE__, __LINE__, __func__}, level::trace, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void debug(const char* fmt, Args&&... args) {
        log(source_loc{__FILE__, __LINE__, __func__}, level::debug, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void info(const char* fmt, Args&&... args) {
        log(source_loc{__FILE__, __LINE__, __func__}, level::info, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void warn(const char* fmt, Args&&... args) {
        log(source_loc{__FILE__, __LINE__, __func__}, level::warn, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void error(const char* fmt, Args&&... args) {
        log(source_loc{__FILE__, __LINE__, __func__}, level::err, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void critical(const char* fmt, Args&&... args) {
        log(source_loc{__FILE__, __LINE__, __func__}, level::critical, fmt, std::forward<Args>(args)...);
    }

private:
    std::string name_;
    level::level_enum level_ = level::info;
};

// ============================================================
// 全局函数
// ============================================================
inline std::shared_ptr<logger> default_logger() {
    static auto logger_ = std::make_shared<logger>("nevo");
    return logger_;
}

inline void set_default_logger(std::shared_ptr<logger> l) {
    // 简化实现：不替换全局静态 logger
    (void)l;
}

inline void set_level(level::level_enum l) {
    default_logger()->set_level(l);
}

inline void init_thread_pool(size_t q_size, size_t thread_count) {
    (void)q_size; (void)thread_count;
}

inline std::shared_ptr<logger> thread_pool() { return nullptr; }

} // namespace spdlog

// ============================================================
// 便捷宏
// ============================================================
#define SPDLOG_TRACE(logger, ...)    if(logger) logger->trace(__VA_ARGS__)
#define SPDLOG_DEBUG(logger, ...)    if(logger) logger->debug(__VA_ARGS__)
#define SPDLOG_INFO(logger, ...)     if(logger) logger->info(__VA_ARGS__)
#define SPDLOG_WARN(logger, ...)     if(logger) logger->warn(__VA_ARGS__)
#define SPDLOG_ERROR(logger, ...)    if(logger) logger->error(__VA_ARGS__)
#define SPDLOG_CRITICAL(logger, ...) if(logger) logger->critical(__VA_ARGS__)
