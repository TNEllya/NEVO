# ============================================================
# NEVO VoIP System - 顶层 CMake 构建配置
# 依赖: vcpkg (Boost, SQLite3, libsodium, Opus, Protobuf, OpenSSL)
# 构建: cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
# ============================================================
cmake_minimum_required(VERSION 3.21)

project(NEVO
    VERSION 0.1.0
    DESCRIPTION "NEVO VoIP communication system"
    LANGUAGES C CXX
)

# C++20 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出目录统一
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================
# 依赖检测（通过 vcpkg）
# ============================================================
# Boost CMP0167 策略（vcpkg wrapper 兼容；CMake < 3.30 无此策略）
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost REQUIRED COMPONENTS system)
find_package(SQLite3 REQUIRED)
# vcpkg 提供 SQLite3::SQLite3；系统 FindSQLite3 模块提供 SQLite::SQLite3，统一为 SQLite3::SQLite3
if(NOT TARGET SQLite3::SQLite3 AND TARGET SQLite::SQLite3)
    add_library(SQLite3::SQLite3 INTERFACE IMPORTED)
    set_target_properties(SQLite3::SQLite3 PROPERTIES
        INTERFACE_LINK_LIBRARIES SQLite::SQLite3)
endif()
# libsodium：优先 vcpkg 的 unofficial-sodium 配置，否则回退系统 pkg-config
find_package(unofficial-sodium CONFIG QUIET)
if(NOT TARGET unofficial-sodium::sodium)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
    add_library(unofficial-sodium::sodium INTERFACE IMPORTED)
    set_target_properties(unofficial-sodium::sodium PROPERTIES
        INTERFACE_LINK_LIBRARIES PkgConfig::SODIUM)
endif()
# 代码引用 sodium::sodium，创建别名指向 unofficial-sodium::sodium
if(TARGET unofficial-sodium::sodium AND NOT TARGET sodium::sodium)
    add_library(sodium::sodium INTERFACE IMPORTED)
    set_target_properties(sodium::sodium PROPERTIES
        INTERFACE_LINK_LIBRARIES unofficial-sodium::sodium)
endif()
# Opus：优先 vcpkg 的 Opus 配置，否则回退系统 pkg-config
find_package(Opus CONFIG QUIET)
if(NOT TARGET Opus::opus)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
    add_library(Opus::opus INTERFACE IMPORTED)
    set_target_properties(Opus::opus PROPERTIES
        INTERFACE_LINK_LIBRARIES PkgConfig::OPUS)
endif()
# vcpkg 提供 Opus::opus（小写），代码引用 Opus::Opus（大写），创建别名
if(TARGET Opus::opus AND NOT TARGET Opus::Opus)
    add_library(Opus::Opus INTERFACE IMPORTED)
    set_target_properties(Opus::Opus PROPERTIES
        INTERFACE_LINK_LIBRARIES Opus::opus)
endif()
# Protobuf：优先 CONFIG 模式（vcpkg 提供），否则回退 CMake 模块模式（系统 libprotobuf-dev）
find_package(Protobuf CONFIG QUIET)
if(NOT TARGET protobuf::libprotobuf)
    find_package(Protobuf REQUIRED)
endif()
find_package(OpenSSL REQUIRED)

# 设置 NEVO_HAS_* 变量供子目录使用
set(NEVO_HAS_BOOST ON)
set(NEVO_HAS_BOOST_LOCKFREE ON)
set(NEVO_HAS_SQLITE ON)
set(NEVO_HAS_SODIUM ON)
set(NEVO_HAS_OPUS ON)
set(NEVO_HAS_PROTOBUF ON)
set(NEVO_HAS_OPENSSL ON)

# Qt6 可选（服务端管理 GUI 需要）
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    set(NEVO_HAS_QT ON)
    message(STATUS "Qt6 found - server GUI enabled")
else()
    message(STATUS "Qt6 not found - server GUI disabled (nevo_server core still builds)")
endif()

# vcpkg 安装目录（供服务端 POST_BUILD 复制 DLL 用）
if(DEFINED ENV{VCPKG_ROOT})
    set(_VCPKG_INSTALLED_DIR "$ENV{VCPKG_ROOT}/installed" CACHE PATH "vcpkg installed dir")
elseif(EXISTS "C:/vcpkg/installed")
    set(_VCPKG_INSTALLED_DIR "C:/vcpkg/installed" CACHE PATH "vcpkg installed dir")
endif()
if(NOT VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "vcpkg triplet")
endif()

# ============================================================
# nevo_config — 编译定义接口库
# ============================================================
add_library(nevo_config INTERFACE)
target_compile_definitions(nevo_config INTERFACE
    NEVO_HAS_OPUS
    NEVO_HAS_SODIUM
    NEVO_HAS_PROTOBUF
    NEVO_HAS_SQLITE
    NEVO_HAS_BOOST
    NEVO_HAS_BOOST_LOCKFREE
    NEVO_HAS_OPENSSL
    SPDLOG_HEADER_ONLY
)

# ============================================================
# miniaudio — 单头文件音频库（需在一个 TU 中定义 IMPLEMENTATION）
# ============================================================
set(MINIAUDIO_IMPL ${CMAKE_BINARY_DIR}/generated/miniaudio_impl.c)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
file(WRITE ${MINIAUDIO_IMPL}
    "/* 自动生成 - miniaudio 实现单元 */\n"
    "#define MINIAUDIO_IMPLEMENTATION\n"
    "#include \"miniaudio.h\"\n"
)
add_library(miniaudio STATIC ${MINIAUDIO_IMPL})
target_include_directories(miniaudio PUBLIC ${CMAKE_SOURCE_DIR}/src/core/include)

# ============================================================
# spdlog — 头文件库（已内嵌在 src/core/include/spdlog/）
# ============================================================
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE ${CMAKE_SOURCE_DIR}/src/core/include)
target_compile_definitions(spdlog INTERFACE SPDLOG_HEADER_ONLY)
# 创建 spdlog::spdlog 别名供子目录引用
add_library(spdlog::spdlog ALIAS spdlog)

# ============================================================
# Protobuf 代码生成（生成到 proto/generated/）
# ============================================================
set(PROTO_GEN_DIR ${CMAKE_SOURCE_DIR}/proto/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})
set(NEVO_PROTO_FILES common control voice video)
set(NEVO_PROTO_SRCS "")
set(NEVO_PROTO_HDRS "")
foreach(proto ${NEVO_PROTO_FILES})
    set(src ${PROTO_GEN_DIR}/${proto}.pb.cc)
    set(hdr ${PROTO_GEN_DIR}/${proto}.pb.h)
    list(APPEND NEVO_PROTO_SRCS ${src})
    list(APPEND NEVO_PROTO_HDRS ${hdr})
    add_custom_command(
        OUTPUT ${src} ${hdr}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${CMAKE_SOURCE_DIR}/proto
            --cpp_out=${PROTO_GEN_DIR}
            ${CMAKE_SOURCE_DIR}/proto/${proto}.proto
        DEPENDS ${CMAKE_SOURCE_DIR}/proto/${proto}.proto
        COMMENT "Protobuf 生成: ${proto}.proto -> C++"
        VERBATIM
    )
endforeach()
# 自定义目标确保生成顺序
add_custom_target(nevo_proto_gen DEPENDS ${NEVO_PROTO_SRCS} ${NEVO_PROTO_HDRS})

# ============================================================
# 编译器警告与平台设置宏
# ============================================================
macro(nevo_set_compiler_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4        # 高警告级别
            /WX        # 警告视为错误
            /utf-8     # 源文件为 UTF-8 编码
        )
    else()
        # GCC/Clang：保留告警但不作为错误（避免系统包环境下新代码的
        # sign-compare/reorder 等非致命告警阻断 Linux/Docker 构建）
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wno-unused-parameter
        )
    endif()
endmacro()

macro(nevo_platform_setup target)
    if(WIN32)
        target_compile_definitions(${target} PRIVATE
            _WIN32_WINNT=0x0A00
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            # 暴露数学常量（M_PI 等）
            _USE_MATH_DEFINES
        )
        # Winsock：htonl/ntohl（PacketCodec）与 Boost.Asio 需要；
        # 对静态库消费方（如 core_test）必须显式链接
        target_link_libraries(${target} PRIVATE ws2_32 mswsock)
    endif()
    # 确保依赖 protobuf 生成
    add_dependencies(${target} nevo_proto_gen)
endmacro()

# ============================================================
# 源码子目录
# ============================================================
add_subdirectory(src)

# ============================================================
# 测试（可选）
# ============================================================
option(BUILD_TESTING "Build NEVO test suite" ON)
if(BUILD_TESTING)
    # GTest 通过 FetchContent 获取
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL "https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"
    )
    set(gtest_force_shared_rt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================
# 构建信息摘要
# ============================================================
message(STATUS "============================================================")
message(STATUS "  NEVO Build Configuration")
message(STATUS "  Boost:     ${Boost_VERSION}")
message(STATUS "  SQLite3:   ${SQLite3_VERSION}")
message(STATUS "  Sodium:    ${sodium_VERSION}")
message(STATUS "  Opus:      ${Opus_VERSION}")
message(STATUS "  Protobuf:  ${Protobuf_VERSION}")
message(STATUS "  OpenSSL:   ${OPENSSL_VERSION}")
message(STATUS "  Qt6 GUI:   ${NEVO_HAS_QT}")
message(STATUS "  Tests:     ${BUILD_TESTING}")
message(STATUS "============================================================")
