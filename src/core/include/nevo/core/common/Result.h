#pragma once
/**
 * @file Result.h
 * @brief Result<T> 错误处理模式
 *
 * 替代异常用于可预期的错误。所有可能失败的函数返回 Result<T>。
 * 使用方式：
 *   auto result = doSomething();
 *   if (!result) { handle error: result.error() }
 *   else { use value: result.value() }
 */

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <cassert>

namespace nevo {

// ============================================================
// 错误码枚举
// ============================================================
enum class ResultCode {
    Ok = 0,
    Unknown = 1,
    AuthFailed = 2,
    PermissionDenied = 3,
    ChannelNotFound = 4,
    AlreadyInChannel = 5,
    InvalidRequest = 6,
    UserNotFound = 14,
    ConnectionFailed = 7,
    Timeout = 8,
    DeviceNotAvailable = 9,
    DeviceInUse = 13,      // 设备被其他应用独占（WASAPI MA_BUSY）
    CryptoError = 10,
    NatTraversalFailed = 11,
    DatabaseError = 12,
};

// ============================================================
// Error 类
// ============================================================
class Error {
public:
    Error(ResultCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    ResultCode code() const { return code_; }
    const std::string& message() const { return message_; }

    /// 便捷检查：是否为特定错误码
    bool is(ResultCode c) const { return code_ == c; }

private:
    ResultCode code_;
    std::string message_;
};

// ============================================================
// Result<T> 模板
// ============================================================

/// void 特化的前向声明
template<typename T>
class Result;

/// Result<void> 特化：不需要返回值的操作
template<>
class Result<void> {
public:
    /// 成功构造
    Result() : error_(std::nullopt) {}

    /// 错误构造
    Result(Error error) : error_(std::move(error)) {}

    /// 是否成功
    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }

    /// 获取错误（仅当 !ok() 时调用）
    const Error& error() const { return error_.value(); }

private:
    std::optional<Error> error_;
};

/// 通用 Result<T> 模板
template<typename T>
class Result {
public:
    /// 成功构造（从值）
    Result(T value) : data_(std::in_place_index<0>, std::move(value)) {}

    /// 错误构造（从 Error）
    Result(Error error) : data_(std::in_place_index<1>, std::move(error)) {}

    /// 错误构造（从 ResultCode + 消息）
    Result(ResultCode code, std::string message)
        : data_(std::in_place_index<1>, Error(code, std::move(message))) {}

    /// 是否成功
    bool ok() const { return data_.index() == 0; }
    explicit operator bool() const { return ok(); }

    /// 获取值（仅当 ok() 时调用，否则触发断言）
    const T& value() const& { assert(ok() && "Result<T>::value() called on error result"); return std::get<0>(data_); }
    T& value() & { assert(ok() && "Result<T>::value() called on error result"); return std::get<0>(data_); }
    T&& value() && { assert(ok() && "Result<T>::value() called on error result"); return std::get<0>(std::move(data_)); }

    /// 获取值，如果失败则返回默认值
    T value_or(T&& default_val) const {
        return ok() ? value() : std::move(default_val);
    }

    /// 获取值，如果失败则返回默认值（const 引用重载，不移动默认值）
    T value_or(const T& default_val) const {
        return ok() ? value() : default_val;
    }

    /// 获取错误（仅当 !ok() 时调用）
    const Error& error() const { return std::get<1>(data_); }

private:
    std::variant<T, Error> data_;
};

// ============================================================
// 便捷构造函数
// ============================================================

/// 创建成功结果
template<typename T>
Result<T> Ok(T value) {
    return Result<T>(std::move(value));
}

/// 创建成功结果（void）
inline Result<void> Ok() {
    return Result<void>();
}

/// 创建错误结果
template<typename T>
Result<T> Err(ResultCode code, std::string message) {
    return Result<T>(Error(code, std::move(message)));
}

/// 创建错误结果（从 Error）
template<typename T>
Result<T> Err(Error error) {
    return Result<T>(std::move(error));
}

} // namespace nevo
