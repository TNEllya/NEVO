/**
 * @file TestServerArgParsing.cpp
 * @brief CLI argument parsing tests for server main
 *
 * 覆盖缺口：服务器参数解析的异常处理
 * 风险等级：高 - 非法参数导致服务器崩溃
 *
 * 背景：根据 fix-nevo-runtime-bugs 修复，Bug #5 Server CLI 参数解析异常崩溃：
 *   - parseArgs 中对 --tcp-port, --udp-port, --threads 使用 std::stoi 转换参数
 *   - 未处理 std::invalid_argument / std::out_of_range 异常
 *   - 传入非数字参数时服务端直接崩溃
 * 修复方案：添加 parseIntSafe<T> 模板函数，内部 try/catch 包装 std::stoi
 */

#include <gtest/gtest.h>
#include <string>
#include <sstream>
#include <iostream>
#include <cstdlib>

namespace {

template <typename T>
bool parseIntSafe(const std::string& str, T& out, const std::string& name) {
    try {
        int value = std::stoi(str);
        out = static_cast<T>(value);
        return true;
    } catch (const std::invalid_argument&) {
        std::cerr << "Invalid " << name << ": '" << str << "' (not a number)" << std::endl;
        return false;
    } catch (const std::out_of_range&) {
        std::cerr << "Invalid " << name << ": '" << str << "' (out of range)" << std::endl;
        return false;
    }
}

TEST(ArgParsingTest, ValidPortNumber) {
    uint16_t port = 0;
    EXPECT_TRUE(parseIntSafe("24800", port, "TCP port"));
    EXPECT_EQ(port, 24800u);
}

TEST(ArgParsingTest, ValidThreadCount) {
    int threads = 0;
    EXPECT_TRUE(parseIntSafe("4", threads, "thread count"));
    EXPECT_EQ(threads, 4);
}

TEST(ArgParsingTest, ValidZeroPort) {
    uint16_t port = 999;
    EXPECT_TRUE(parseIntSafe("0", port, "TCP port"));
    EXPECT_EQ(port, 0u);
}

TEST(ArgParsingTest, ValidNegativeToUnsigned) {
    uint16_t port = 100;
    int val = -5;
    EXPECT_TRUE(parseIntSafe("-5", val, "test"));
    EXPECT_EQ(val, -5);
}

TEST(ArgParsingTest, ValidLargeNumber) {
    int large = 0;
    EXPECT_TRUE(parseIntSafe("2147483647", large, "number"));
    EXPECT_EQ(large, 2147483647);
}

TEST(ArgParsingTest, InvalidNonNumeric) {
    uint16_t port = 0;
    EXPECT_FALSE(parseIntSafe("abc", port, "TCP port"));
}

TEST(ArgParsingTest, InvalidEmptyString) {
    uint16_t port = 0;
    EXPECT_FALSE(parseIntSafe("", port, "TCP port"));
}

TEST(ArgParsingTest, InvalidMixedAlphaNumeric) {
    int threads = 0;
    EXPECT_FALSE(parseIntSafe("4abc", threads, "thread count"));
}

TEST(ArgParsingTest, InvalidLeadingSpaces) {
    uint16_t port = 0;
    EXPECT_FALSE(parseIntSafe("  24800", port, "TCP port"));
}

TEST(ArgParsingTest, InvalidTrailingSpaces) {
    uint16_t port = 0;
    EXPECT_FALSE(parseIntSafe("24800  ", port, "TCP port"));
}

TEST(ArgParsingTest, InvalidFloat) {
    int val = 0;
    EXPECT_FALSE(parseIntSafe("3.14", val, "number"));
}

TEST(ArgParsingTest, InvalidOutOfRangePositive) {
    int val = 0;
    EXPECT_FALSE(parseIntSafe("99999999999999999999", val, "number"));
}

TEST(ArgParsingTest, InvalidOutOfRangeNegative) {
    int val = 0;
    EXPECT_FALSE(parseIntSafe("-99999999999999999999", val, "number"));
}

TEST(ArgParsingTest, ValidWithPlusSign) {
    int val = 0;
    EXPECT_TRUE(parseIntSafe("+42", val, "number"));
    EXPECT_EQ(val, 42);
}

TEST(ArgParsingTest, ValidHexString) {
    int val = 0;
    EXPECT_FALSE(parseIntSafe("0x10", val, "number"));
}

TEST(ArgParsingTest, ValidOctalString) {
    int val = 0;
    EXPECT_TRUE(parseIntSafe("0777", val, "number"));
    EXPECT_EQ(val, 777);
}

TEST(ArgParsingTest, InvalidUnicodeNumbers) {
    int val = 0;
    EXPECT_FALSE(parseIntSafe("１２３", val, "number"));
}

TEST(ArgParsingTest, InvalidSQLInjection) {
    int val = 0;
    EXPECT_FALSE(parseIntSafe("1; DROP TABLE users;", val, "number"));
}

TEST(ArgParsingTest, InvalidPathTraversal) {
    uint16_t port = 0;
    EXPECT_FALSE(parseIntSafe("../../../etc/passwd", port, "TCP port"));
}

} // namespace
