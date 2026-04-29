#pragma once
/**
 * @file spdlog.h
 * @brief Lightweight drop-in replacement for spdlog (minimal stub)
 *
 * Provides just enough API for the NEVO project's Logger.h/cpp wrapper.
 * Uses a simple {} placeholder replacement that converts any printable
 * type to string, avoiding the need for std::formatter specializations.
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <sstream>

namespace spdlog {

// Sink base class
namespace sinks {
class sink {
public:
    virtual ~sink() = default;
    virtual void log(const std::string& msg) { (void)msg; }
    virtual void flush() {}
    virtual void set_pattern(const std::string& pattern) { (void)pattern; }
};
} // namespace sinks

// ============================================================
// Log Levels
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
// Source Location
// ============================================================
struct source_loc {
    const char* filename = nullptr;
    int line = 0;
    const char* funcname = nullptr;

    source_loc() = default;
    source_loc(const char* f, int l, const char* fn) : filename(f), line(l), funcname(fn) {}
};

// ============================================================
// Simple format helper: converts any type to string
// ============================================================
namespace detail {

inline void format_replace(std::string& result, const char* fmt) {
    (void)fmt;
    // No more {} to replace - just append remaining
    result += fmt;
}

template<typename T>
std::string to_str(const T& v) {
    if constexpr (std::is_same_v<std::decay_t<T>, const char*> ||
                  std::is_same_v<std::decay_t<T>, char*> ||
                  std::is_same_v<std::decay_t<T>, std::string>) {
        return std::string(v);
    } else if constexpr (std::is_enum_v<std::decay_t<T>>) {
        return std::to_string(static_cast<long long>(v));
    } else if constexpr (std::is_integral_v<std::decay_t<T>>) {
        return std::to_string(v);
    } else if constexpr (std::is_floating_point_v<std::decay_t<T>>) {
        // Use snprintf for floats to get reasonable precision
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
        return buf;
    } else {
        // Fallback: use ostringstream
        std::ostringstream oss;
        oss << v;
        return oss.str();
    }
}

template<typename Arg, typename... Args>
void format_replace(std::string& result, const char* fmt, Arg&& arg, Args&&... args) {
    const char* p = fmt;
    while (*p) {
        if (*p == '{' && *(p + 1) == '}') {
            result += to_str(std::forward<Arg>(arg));
            p += 2;
            format_replace(result, p, std::forward<Args>(args)...);
            return;
        }
        // Handle {{ as literal {
        if (*p == '{' && *(p + 1) == '{') {
            result += '{';
            p += 2;
            continue;
        }
        // Handle }} as literal }
        if (*p == '}' && *(p + 1) == '}') {
            result += '}';
            p += 2;
            continue;
        }
        result += *p++;
    }
}

} // namespace detail

// ============================================================
// Logger Class
// ============================================================
class logger {
public:
    explicit logger(std::string name = "") : name_(std::move(name)) {}
    virtual ~logger() = default;

    const std::string& name() const { return name_; }

    // Sink list (for file sink attachment)
    std::vector<std::shared_ptr<sinks::sink>>& sinks() { return sinks_; }
    const std::vector<std::shared_ptr<sinks::sink>>& sinks() const { return sinks_; }

    void set_level(level::level_enum l) { level_ = l; }
    level::level_enum level() const { return level_; }

    template<typename... Args>
    void log(source_loc loc, level::level_enum lvl, const char* fmt, Args&&... args) {
        if (lvl < level_) return;
        std::string msg;
        detail::format_replace(msg, fmt, std::forward<Args>(args)...);
        fprintf(stderr, "[%s] [%s] %s:%d: %s\n",
                name_.c_str(), level::to_string_view(lvl),
                loc.filename ? loc.filename : "?", loc.line, msg.c_str());
    }

    // Overload for no arguments
    void log(source_loc loc, level::level_enum lvl, const char* fmt) {
        if (lvl < level_) return;
        fprintf(stderr, "[%s] [%s] %s:%d: %s\n",
                name_.c_str(), level::to_string_view(lvl),
                loc.filename ? loc.filename : "?", loc.line, fmt);
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
    std::vector<std::shared_ptr<sinks::sink>> sinks_;
};

// ============================================================
// Global Functions
// ============================================================
// ============================================================
// Global Logger Management
// ============================================================

/// Get the global default logger instance.
/// Returns a static shared_ptr that can be replaced via set_default_logger().
inline std::shared_ptr<logger>& default_logger_ref() {
    static auto logger_ = std::make_shared<logger>("nevo");
    return logger_;
}

inline std::shared_ptr<logger> default_logger() {
    return default_logger_ref();
}

/// Set the global default logger.
/// Subsequent calls to default_logger() and set_level() will use this logger.
inline void set_default_logger(std::shared_ptr<logger> l) {
    if (l) {
        default_logger_ref() = std::move(l);
    }
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
// Convenience Macros
// ============================================================
#define SPDLOG_TRACE(logger, ...)    if(logger) logger->trace(__VA_ARGS__)
#define SPDLOG_DEBUG(logger, ...)    if(logger) logger->debug(__VA_ARGS__)
#define SPDLOG_INFO(logger, ...)     if(logger) logger->info(__VA_ARGS__)
#define SPDLOG_WARN(logger, ...)     if(logger) logger->warn(__VA_ARGS__)
#define SPDLOG_ERROR(logger, ...)    if(logger) logger->error(__VA_ARGS__)
#define SPDLOG_CRITICAL(logger, ...) if(logger) logger->critical(__VA_ARGS__)
