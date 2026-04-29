/**
 * @file TestResult.cpp
 * @brief Unit tests for Result<T> error handling pattern
 */

#include <gtest/gtest.h>
#include "nevo/core/common/Result.h"

namespace nevo {
namespace {

// ============================================================
// Result<T> with Ok and Err construction
// ============================================================

TEST(ResultTest, OkConstructionHoldsValue) {
    Result<int> r = Ok(42);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, ErrConstructionWithCodeAndMessage) {
    Result<int> r = Err<int>(ResultCode::AuthFailed, "bad credentials");
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error().code(), ResultCode::AuthFailed);
    EXPECT_EQ(r.error().message(), "bad credentials");
}

TEST(ResultTest, ErrConstructionFromErrorObject) {
    Error err(ResultCode::ChannelNotFound, "no such channel");
    Result<int> r = Err<int>(err);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code(), ResultCode::ChannelNotFound);
    EXPECT_EQ(r.error().message(), "no such channel");
}

TEST(ResultTest, ErrConstructionViaResultConstructor) {
    Result<int> r(ResultCode::Timeout, "timed out");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code(), ResultCode::Timeout);
    EXPECT_EQ(r.error().message(), "timed out");
}

TEST(ResultTest, StringValueRoundtrip) {
    Result<std::string> r = Ok(std::string("hello"));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value(), "hello");
}

// ============================================================
// Result<void> specialization
// ============================================================

TEST(ResultVoidTest, DefaultConstructionIsSuccess) {
    Result<void> r;
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultVoidTest, OkFunctionReturnsSuccess) {
    Result<void> r = Ok();
    EXPECT_TRUE(r.ok());
}

TEST(ResultVoidTest, ErrorConstructionIsFailure) {
    Result<void> r(Error(ResultCode::PermissionDenied, "access denied"));
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error().code(), ResultCode::PermissionDenied);
    EXPECT_EQ(r.error().message(), "access denied");
}

// ============================================================
// value_or()
// ============================================================

TEST(ResultTest, ValueOrReturnsValueWhenOk) {
    Result<int> r = Ok(10);
    EXPECT_EQ(r.value_or(99), 10);
}

TEST(ResultTest, ValueOrReturnsDefaultWhenErr) {
    Result<int> r = Err<int>(ResultCode::Unknown, "fail");
    EXPECT_EQ(r.value_or(99), 99);
}

TEST(ResultTest, ValueOrWithZeroDefault) {
    Result<int> r = Err<int>(ResultCode::Unknown, "fail");
    EXPECT_EQ(r.value_or(0), 0);
}

// ============================================================
// ok() and operator bool
// ============================================================

TEST(ResultTest, OkReturnsTrueForSuccess) {
    Result<int> r = Ok(1);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultTest, OkReturnsFalseForError) {
    Result<int> r = Err<int>(ResultCode::Unknown, "error");
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST(ResultVoidTest, OkReturnsTrueForDefaultVoid) {
    Result<void> r;
    EXPECT_TRUE(r.ok());
}

TEST(ResultVoidTest, OkReturnsFalseForErrorVoid) {
    Result<void> r(Error(ResultCode::DatabaseError, "db fail"));
    EXPECT_FALSE(r.ok());
}

// ============================================================
// Err() helper functions
// ============================================================

TEST(ResultTest, ErrHelperWithCodeAndMessage) {
    auto r = Err<int>(ResultCode::CryptoError, "encryption failed");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code(), ResultCode::CryptoError);
    EXPECT_EQ(r.error().message(), "encryption failed");
}

TEST(ResultTest, ErrHelperFromErrorObject) {
    Error e(ResultCode::ConnectionFailed, "refused");
    auto r = Err<int>(e);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code(), ResultCode::ConnectionFailed);
    EXPECT_EQ(r.error().message(), "refused");
}

TEST(ResultTest, ErrHelperForVoidResult) {
    // Err<void> is not defined, but we can construct Result<void> from Error
    Result<void> r(Error(ResultCode::InvalidRequest, "bad request"));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code(), ResultCode::InvalidRequest);
}

// ============================================================
// Error class
// ============================================================

TEST(ErrorTest, IsMethodChecksCode) {
    Error e(ResultCode::AuthFailed, "auth");
    EXPECT_TRUE(e.is(ResultCode::AuthFailed));
    EXPECT_FALSE(e.is(ResultCode::Unknown));
}

TEST(ErrorTest, Accessors) {
    Error e(ResultCode::NatTraversalFailed, "stun timeout");
    EXPECT_EQ(e.code(), ResultCode::NatTraversalFailed);
    EXPECT_EQ(e.message(), "stun timeout");
}

} // namespace
} // namespace nevo
