#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <cassert>

namespace nevo {

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
    DeviceInUse = 13,
    CryptoError = 10,
    NatTraversalFailed = 11,
    DatabaseError = 12,
};

class Error {
public:
    Error(ResultCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    ResultCode code() const { return code_; }
    const std::string& message() const { return message_; }

    bool is(ResultCode c) const { return code_ == c; }

private:
    ResultCode code_;
    std::string message_;
};

/// void 特化的前向声明
template<typename T>
class Result;

/// Result<void> 特化
template<>
class Result<void> {
public:
    Result() : error_(std::nullopt) {}

    Result(Error error) : error_(std::move(error)) {}

    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }

    const Error& error() const { return error_.value(); }

private:
    std::optional<Error> error_;
};

/// Result<T> 主模板
template<typename T>
class Result {
public:
    Result(T value) : data_(std::in_place_index<0>, std::move(value)) {}

    Result(Error error) : data_(std::in_place_index<1>, std::move(error)) {}

    Result(ResultCode code, std::string message)
        : data_(std::in_place_index<1>, Error(code, std::move(message))) {}

    bool ok() const { return data_.index() == 0; }
    explicit operator bool() const { return ok(); }

    const T& value() const& { assert(ok() && "Result<T>::value() called on error result"); return std::get<0>(data_); }
    T& value() & { assert(ok() && "Result<T>::value() called on error result"); return std::get<0>(data_); }
    T&& value() && { assert(ok() && "Result<T>::value() called on error result"); return std::get<0>(std::move(data_)); }

    T value_or(T&& default_val) const {
        return ok() ? value() : std::move(default_val);
    }

    T value_or(const T& default_val) const {
        return ok() ? value() : default_val;
    }

    const Error& error() const { return std::get<1>(data_); }

private:
    std::variant<T, Error> data_;
};

template<typename T>
Result<T> Ok(T value) {
    return Result<T>(std::move(value));
}

inline Result<void> Ok() {
    return Result<void>();
}

template<typename T>
Result<T> Err(ResultCode code, std::string message) {
    return Result<T>(Error(code, std::move(message)));
}

template<typename T>
Result<T> Err(Error error) {
    return Result<T>(std::move(error));
}

} // namespace nevo
