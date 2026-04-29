#pragma once
/**
 * @file spdlog.h
 * @brief Lightweight drop-in replacement for spdlog (minimal stub)
 *
 * Provides just enough API for the NEVO project's Logger.h/cpp wrapper.
 * Supports fmt-style {} placeholders and outputs to stderr with timestamps.
 * Optionally writes to a log file.
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <mutex>
#include <array>
#include <chrono>

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
// 内部格式化工具
// ============================================================
namespace detail {

/// 将任意类型转换为字符串
inline void format_arg(std::stringstream& ss, const char* val) { ss << val; }
inline void format_arg(std::stringstream& ss, const std::string& val) { ss << val; }
inline void format_arg(std::stringstream& ss, int val) { ss << val; }
inline void format_arg(std::stringstream& ss, unsigned int val) { ss << val; }
inline void format_arg(std::stringstream& ss, long val) { ss << val; }
inline void format_arg(std::stringstream& ss, unsigned long val) { ss << val; }
inline void format_arg(std::stringstream& ss, long long val) { ss << val; }
inline void format_arg(std::stringstream& ss, unsigned long long val) { ss << val; }
inline void format_arg(std::stringstream& ss, float val) { ss << val; }
inline void format_arg(std::stringstream& ss, double val) { ss << std::setprecision(6) << val; }
inline void format_arg(std::stringstream& ss, bool val) { ss << (val ? "true" : "false"); }

/// 递归格式化：处理 fmt 风格的 {} 占位符
inline std::string format_impl(const char* fmt) {
    return std::string(fmt);
}

template<typename Arg, typename... Args>
std::string format_impl(const char* fmt, Arg&& arg, Args&&... args) {
    std::string result;
    const char* p = fmt;
    while (*p) {
        if (*p == '{' && *(p + 1) == '}') {
            std::stringstream ss;
            format_arg(ss, std::forward<Arg>(arg));
            result += ss.str();
            p += 2;
            result += format_impl(p, std::forward<Args>(args)...);
            return result;
        }
        result += *p++;
    }
    return result;
}

/// 获取当前时间戳字符串 [YYYY-MM-DD HH:MM:SS.mmm]
inline std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    std::ostringstream oss;
    oss << buf << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace detail

// ============================================================
// 全局日志文件输出（可选）
// ============================================================
namespace sinks {

/// 全局日志文件输出管理
struct FileSink {
    std::ofstream file;
    std::mutex mutex;
    bool enabled = false;

    static FileSink& instance() {
        static FileSink sink;
        return sink;
    }

    bool open(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        file.open(path, std::ios::app);
        enabled = file.is_open();
        return enabled;
    }

    void write(const std::string& line) {
        if (!enabled) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (file.is_open()) {
            file << line << std::endl;
        }
    }
};

} // namespace sinks

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
    void log(source_loc, level::level_enum lvl, const char* fmt, Args&&... args) {
        if (lvl < level_) return;
        output(lvl, detail::format_impl(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void trace(const char* fmt, Args&&... args) {
        log(source_loc{}, level::trace, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void debug(const char* fmt, Args&&... args) {
        log(source_loc{}, level::debug, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void info(const char* fmt, Args&&... args) {
        log(source_loc{}, level::info, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void warn(const char* fmt, Args&&... args) {
        log(source_loc{}, level::warn, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void error(const char* fmt, Args&&... args) {
        log(source_loc{}, level::err, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void critical(const char* fmt, Args&&... args) {
        log(source_loc{}, level::critical, fmt, std::forward<Args>(args)...);
    }

private:
    void output(level::level_enum lvl, const std::string& msg) {
        std::string line = "[" + detail::timestamp() + "] [" +
                          std::string(level::to_string_view(lvl)) + "] [" +
                          name_ + "] " + msg;

        fprintf(stderr, "%s\n", line.c_str());
        fflush(stderr);

        // 同时写入文件（如果已配置）
        sinks::FileSink::instance().write(line);
    }

    std::string name_;
    level::level_enum level_ = level::info;
};

// ============================================================
// 全局函数
// ============================================================
inline std::shared_ptr<logger>& default_logger_ref() {
    static auto logger_ = std::make_shared<logger>("nevo");
    return logger_;
}

inline std::shared_ptr<logger> default_logger() {
    return default_logger_ref();
}

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
// 便捷宏
// ============================================================
#define SPDLOG_TRACE(logger, ...)    if(logger) logger->trace(__VA_ARGS__)
#define SPDLOG_DEBUG(logger, ...)    if(logger) logger->debug(__VA_ARGS__)
#define SPDLOG_INFO(logger, ...)     if(logger) logger->info(__VA_ARGS__)
#define SPDLOG_WARN(logger, ...)     if(logger) logger->warn(__VA_ARGS__)
#define SPDLOG_ERROR(logger, ...)    if(logger) logger->error(__VA_ARGS__)
#define SPDLOG_CRITICAL(logger, ...) if(logger) logger->critical(__VA_ARGS__)
