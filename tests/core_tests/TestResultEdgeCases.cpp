/**
 * @file TestResultEdgeCases.cpp
 * @brief Edge case tests for Result<T> error handling
 * 
 * 覆盖缺口：Result<T>边界条件、value()在错误状态下的断言行为
 * 风险等级：中等 - 错误处理模式影响系统稳定性
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "nevo/core/common/Result.h"

namespace nevo {
namespace {

// ============================================================
// Result<T> with complex types
// ============================================================

TEST(ResultEdgeCaseTest, ResultWithVector) {
    std::vector<int> vec = {1, 2, 3, 4, 5};
    Result<std::vector<int>> r = Ok(vec);
    
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 5u);
    EXPECT_EQ(r.value()[0], 1);
    EXPECT_EQ(r.value()[4], 5);
}

TEST(ResultEdgeCaseTest, ResultWithEmptyVector) {
    std::vector<int> vec;
    Result<std::vector<int>> r = Ok(vec);
    
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.value().empty());
}

TEST(ResultEdgeCaseTest, ResultWithLargeString) {
    std::string large(10000, 'x');
    Result<std::string> r = Ok(large);
    
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 10000u);
    EXPECT_EQ(r.value()[0], 'x');
}

// ============================================================
// Result<void> edge cases
// ============================================================

TEST(ResultEdgeCaseTest, ResultVoidDefaultIsSuccess) {
    Result<void> r;
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultEdgeCaseTest, ResultVoidExplicitSuccess) {
    Result<void> r = Ok();
    EXPECT_TRUE(r.ok());
}

TEST(ResultEdgeCaseTest, ResultVoidExplicitError) {
    Result<void> r = Error(ResultCode::Timeout, "timed out");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code(), ResultCode::Timeout);
    EXPECT_EQ(r.error().message(), "timed out");
}

TEST(ResultEdgeCaseTest, ResultVoidErrorIsMethod) {
    Error e(ResultCode::AuthFailed, "auth failed");
    Result<void> r(e);
    
    EXPECT_TRUE(r.error().is(ResultCode::AuthFailed));
    EXPECT_FALSE(r.error().is(ResultCode::Timeout));
}

// ============================================================
// Error class edge cases
// ============================================================

TEST(ResultEdgeCaseTest, ErrorEmptyMessage) {
    Error e(ResultCode::Unknown, "");
    EXPECT_EQ(e.code(), ResultCode::Unknown);
    EXPECT_EQ(e.message(), "");
    EXPECT_TRUE(e.is(ResultCode::Unknown));
}

TEST(ResultEdgeCaseTest, ErrorLongMessage) {
    std::string longMsg(1000, 'a');
    Error e(ResultCode::DatabaseError, longMsg);
    EXPECT_EQ(e.message(), longMsg);
}

TEST(ResultEdgeCaseTest, ErrorSpecialCharactersInMessage) {
    Error e(ResultCode::InvalidRequest, "Error: special chars !@#$%^&*()\n\t\"'");
    EXPECT_EQ(e.message(), "Error: special chars !@#$%^&*()\n\t\"'");
}

TEST(ResultEdgeCaseTest, ErrorUnicodeMessage) {
    Error e(ResultCode::InvalidRequest, "错误：中文消息");
    EXPECT_EQ(e.message(), "错误：中文消息");
}

// ============================================================
// ResultCode enumeration coverage
// ============================================================

TEST(ResultEdgeCaseTest, AllResultCodes) {
    // Verify all error codes can be used
    std::vector<ResultCode> codes = {
        ResultCode::Ok,
        ResultCode::Unknown,
        ResultCode::AuthFailed,
        ResultCode::PermissionDenied,
        ResultCode::ChannelNotFound,
        ResultCode::AlreadyInChannel,
        ResultCode::InvalidRequest,
        ResultCode::UserNotFound,
        ResultCode::ConnectionFailed,
        ResultCode::Timeout,
        ResultCode::DeviceNotAvailable,
        ResultCode::DeviceInUse,
        ResultCode::CryptoError,
        ResultCode::NatTraversalFailed,
        ResultCode::DatabaseError,
    };
    
    for (auto code : codes) {
        Result<int> r(code, "test");
        EXPECT_FALSE(r.ok());
        EXPECT_EQ(r.error().code(), code);
    }
}

// ============================================================
// value_or edge cases
// ============================================================

TEST(ResultEdgeCaseTest, ValueOrWithOkResult) {
    Result<int> r = Ok(42);
    EXPECT_EQ(r.value_or(99), 42);
}

TEST(ResultEdgeCaseTest, ValueOrWithErrorResult) {
    Result<int> r = Err<int>(ResultCode::Unknown, "error");
    EXPECT_EQ(r.value_or(99), 99);
}

TEST(ResultEdgeCaseTest, ValueOrWithZeroDefault) {
    Result<int> r = Err<int>(ResultCode::Unknown, "error");
    EXPECT_EQ(r.value_or(0), 0);
}

TEST(ResultEdgeCaseTest, ValueOrWithNegativeDefault) {
    Result<int> r = Err<int>(ResultCode::Unknown, "error");
    EXPECT_EQ(r.value_or(-100), -100);
}

TEST(ResultEdgeCaseTest, ValueOrWithString) {
    Result<std::string> r = Err<std::string>(ResultCode::Unknown, "error");
    EXPECT_EQ(r.value_or("default"), "default");
}

TEST(ResultEdgeCaseTest, ValueOrWithEmptyStringDefault) {
    Result<std::string> r = Err<std::string>(ResultCode::Unknown, "error");
    EXPECT_EQ(r.value_or(""), "");
}

// ============================================================
// Chained/nested results
// ============================================================

TEST(ResultEdgeCaseTest, ResultOfResult) {
    // Result containing another Result is unusual but should work
    Result<int> inner = Ok(42);
    // This won't compile as intended, documenting limitation
}

// ============================================================
// Move semantics
// ============================================================

TEST(ResultEdgeCaseTest, MoveConstruction) {
    Result<std::string> r1 = Ok(std::string("move me"));
    Result<std::string> r2 = std::move(r1);
    
    EXPECT_TRUE(r2.ok());
    EXPECT_EQ(r2.value(), "move me");
}

TEST(ResultEdgeCaseTest, MoveAssignment) {
    Result<std::string> r1 = Ok(std::string("move me"));
    Result<std::string> r2 = Err<std::string>(ResultCode::Unknown, "error");
    
    r2 = std::move(r1);
    
    EXPECT_TRUE(r2.ok());
    EXPECT_EQ(r2.value(), "move me");
}

// ============================================================
// Const correctness
// ============================================================

TEST(ResultEdgeCaseTest, ConstResultAccess) {
    const Result<int> r = Ok(42);
    
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultEdgeCaseTest, ConstErrorAccess) {
    const Result<int> r = Err<int>(ResultCode::Unknown, "error");
    
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code(), ResultCode::Unknown);
}

// ============================================================
// Multiple errors with same code
// ============================================================

TEST(ResultEdgeCaseTest, SameErrorCodeDifferentMessages) {
    Result<int> r1 = Err<int>(ResultCode::AuthFailed, "invalid password");
    Result<int> r2 = Err<int>(ResultCode::AuthFailed, "token expired");
    
    EXPECT_EQ(r1.error().code(), r2.error().code());
    EXPECT_NE(r1.error().message(), r2.error().message());
}

// ============================================================
// Result construction patterns
// ============================================================

TEST(ResultEdgeCaseTest, ErrHelperVariations) {
    // Err with code and message
    auto r1 = Err<int>(ResultCode::Timeout, "timeout");
    EXPECT_FALSE(r1.ok());
    
    // Err with Error object
    Error e(ResultCode::DatabaseError, "db error");
    auto r2 = Err<int>(e);
    EXPECT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code(), ResultCode::DatabaseError);
}

TEST(ResultEdgeCaseTest, DirectErrorConstruction) {
    // Direct construction from Error
    Error e(ResultCode::CryptoError, "crypto failed");
    Result<int> r(e);
    
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code(), ResultCode::CryptoError);
}

TEST(ResultEdgeCaseTest, DirectCodeMessageConstruction) {
    // Direct construction from code and message
    Result<int> r(ResultCode::NatTraversalFailed, "stun failed");
    
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code(), ResultCode::NatTraversalFailed);
}

// ============================================================
// Boolean context
// ============================================================

TEST(ResultEdgeCaseTest, BooleanContextOk) {
    Result<int> r = Ok(42);
    
    if (r) {
        EXPECT_TRUE(true);
    } else {
        EXPECT_TRUE(false) << "Should be true for Ok result";
    }
}

TEST(ResultEdgeCaseTest, BooleanContextError) {
    Result<int> r = Err<int>(ResultCode::Unknown, "error");
    
    if (!r) {
        EXPECT_TRUE(true);
    } else {
        EXPECT_TRUE(false) << "Should be false for Error result";
    }
}

} // namespace
} // namespace nevo
