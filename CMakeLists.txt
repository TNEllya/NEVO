cmake_minimum_required(VERSION 3.21)
project(NEVO VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(CompilerWarnings)
include(PlatformSetup)

# ============================================================
# 1. miniaudio: 单头文件 (embedded in nevo_core include)
# ============================================================
add_library(miniaudio
    ${CMAKE_SOURCE_DIR}/3rdparty/miniaudio.c
)
target_include_directories(miniaudio PUBLIC
    ${CMAKE_SOURCE_DIR}/3rdparty
)
target_compile_definitions(miniaudio PRIVATE MINIAUDIO_IMPLEMENTATION)

# ============================================================
# 2. spdlog: 高性能日志 (embedded in nevo_core include)
# ============================================================
add_library(spdlog_stub INTERFACE)
add_library(spdlog::spdlog ALIAS spdlog_stub)
set(spdlog_FOUND ON)

# ============================================================
# 3. Google Test (可选 FetchContent)
# ============================================================
option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()

# ============================================================
# 4. Protobuf - 使用预生成的轻量级头文件 (proto/generated/)
# ============================================================
find_package(Protobuf QUIET)
if(Protobuf_FOUND)
    set(NEVO_HAS_PROTOBUF ON)
    message(STATUS "Protobuf found: ${Protobuf_VERSION}")
else()
    # Try vcpkg CONFIG mode
    find_package(protobuf CONFIG QUIET)
    if(protobuf_FOUND)
        set(NEVO_HAS_PROTOBUF ON)
        message(STATUS "Protobuf found (vcpkg CONFIG)")
    else()
        set(NEVO_HAS_PROTOBUF OFF)
        message(STATUS "Protobuf not found. Protocol uses stubs.")
    endif()
endif()

# ============================================================
# 5. Boost
# ============================================================
cmake_policy(SET CMP0167 OLD)
find_package(Boost QUIET COMPONENTS system lockfree endian)
if(Boost_FOUND)
    find_path(BOOST_LOCKFREE_INCLUDE boost/lockfree/spsc_queue.hpp HINTS ${Boost_INCLUDE_DIRS})
    set(NEVO_HAS_BOOST ON)
    if(BOOST_LOCKFREE_INCLUDE)
        set(NEVO_HAS_BOOST_LOCKFREE ON)
    else()
        set(NEVO_HAS_BOOST_LOCKFREE OFF)
    endif()
    message(STATUS "Boost found: ${Boost_VERSION} (lockfree: ${NEVO_HAS_BOOST_LOCKFREE})")
else()
    set(NEVO_HAS_BOOST OFF)
    set(NEVO_HAS_BOOST_LOCKFREE OFF)
    message(STATUS "Boost not found. Network module skipped.")
endif()

# ============================================================
# 6. OpenSSL
# ============================================================
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    set(NEVO_HAS_OPENSSL ON)
    message(STATUS "OpenSSL found: ${OpenSSL_VERSION}")
else()
    set(NEVO_HAS_OPENSSL OFF)
    message(STATUS "OpenSSL not found.")
endif()

# ============================================================
# 7. Opus Codec
# ============================================================
find_package(opus CONFIG QUIET)
if(opus_FOUND)
    set(NEVO_HAS_OPUS ON)
    if(NOT TARGET Opus::Opus)
        if(TARGET Opus::opus)
            add_library(Opus::Opus ALIAS Opus::opus)
        elseif(TARGET opus::opus)
            add_library(Opus::Opus ALIAS opus::opus)
        endif()
    endif()
    message(STATUS "Opus found")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPUS QUIET opus)
    endif()
    if(OPUS_FOUND)
        add_library(Opus::Opus SHARED IMPORTED)
        set_target_properties(Opus::Opus PROPERTIES
            IMPORTED_LOCATION ${OPUS_LINK_LIBRARIES}
            INTERFACE_INCLUDE_DIRECTORIES ${OPUS_INCLUDE_DIRS})
        set(NEVO_HAS_OPUS ON)
    else()
        set(NEVO_HAS_OPUS OFF)
        message(STATUS "Opus not found. Audio codec uses stubs.")
    endif()
endif()

# ============================================================
# 8. libsodium
# ============================================================
find_package(unofficial-sodium CONFIG QUIET)
if(unofficial-sodium_FOUND)
    set(NEVO_HAS_SODIUM ON)
    if(NOT TARGET sodium::sodium)
        add_library(sodium::sodium ALIAS unofficial-sodium::sodium)
    endif()
    message(STATUS "libsodium found")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SODIUM QUIET libsodium)
    endif()
    if(SODIUM_FOUND)
        add_library(sodium::sodium SHARED IMPORTED)
        set_target_properties(sodium::sodium PROPERTIES
            IMPORTED_LOCATION ${SODIUM_LINK_LIBRARIES}
            INTERFACE_INCLUDE_DIRECTORIES ${SODIUM_INCLUDE_DIRS})
        set(NEVO_HAS_SODIUM ON)
    else()
        set(NEVO_HAS_SODIUM OFF)
        message(STATUS "libsodium not found. Encryption uses stubs.")
    endif()
endif()

# ============================================================
# 9. SQLite3
# ============================================================
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    set(NEVO_HAS_SQLITE ON)
    message(STATUS "SQLite3 found: ${SQLite3_VERSION}")
else()
    set(NEVO_HAS_SQLITE OFF)
    message(STATUS "SQLite3 not found. Server DB uses stubs.")
endif()

# ============================================================
# 10. Qt 6
# ============================================================
find_package(Qt6 COMPONENTS Core Widgets QUIET)
if(Qt6_FOUND)
    set(NEVO_HAS_QT ON)
    message(STATUS "Qt6 found")
else()
    set(NEVO_HAS_QT OFF)
    message(STATUS "Qt6 not found. UI module skipped.")
endif()

# ============================================================
# 汇总
# ============================================================
message(STATUS "")
message(STATUS "=== NEVO Dependency Status ===")
message(STATUS "  miniaudio:    ON (local)")
message(STATUS "  spdlog:       ON (local stub)")
message(STATUS "  Boost:        ${NEVO_HAS_BOOST} (lockfree: ${NEVO_HAS_BOOST_LOCKFREE})")
message(STATUS "  OpenSSL:      ${NEVO_HAS_OPENSSL}")
message(STATUS "  Opus:         ${NEVO_HAS_OPUS}")
message(STATUS "  libsodium:    ${NEVO_HAS_SODIUM}")
message(STATUS "  SQLite3:      ${NEVO_HAS_SQLITE}")
message(STATUS "  Qt6:          ${NEVO_HAS_QT}")
message(STATUS "==============================")
message(STATUS "")

# ============================================================
# 编译定义
# ============================================================
add_library(nevo_config INTERFACE)
if(NEVO_HAS_BOOST)
    target_compile_definitions(nevo_config INTERFACE NEVO_HAS_BOOST)
endif()
if(NEVO_HAS_BOOST_LOCKFREE)
    target_compile_definitions(nevo_config INTERFACE NEVO_HAS_BOOST_LOCKFREE)
endif()
if(NEVO_HAS_OPUS)
    target_compile_definitions(nevo_config INTERFACE NEVO_HAS_OPUS)
endif()
if(NEVO_HAS_SODIUM)
    target_compile_definitions(nevo_config INTERFACE NEVO_HAS_SODIUM)
endif()
if(NEVO_HAS_SQLITE)
    target_compile_definitions(nevo_config INTERFACE NEVO_HAS_SQLITE)
endif()
if(NEVO_HAS_OPENSSL)
    target_compile_definitions(nevo_config INTERFACE NEVO_HAS_OPENSSL)
endif()

add_subdirectory(src)

if(BUILD_TESTING AND TARGET GTest::gtest_main)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================
# Code coverage (Linux only, GCC/Clang)
# ============================================================
option(ENABLE_COVERAGE "Enable code coverage reporting" OFF)
if(ENABLE_COVERAGE AND BUILD_TESTING)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(TARGET nevo_core)
            target_compile_options(nevo_core PUBLIC --coverage -O0 -g)
            target_link_options(nevo_core PUBLIC --coverage)
        endif()
        if(TARGET nevo_network)
            target_compile_options(nevo_network PUBLIC --coverage -O0 -g)
            target_link_options(nevo_network PUBLIC --coverage)
        endif()
        message(STATUS "Code coverage enabled")
    endif()
endif()

# ============================================================
# Install targets
# ============================================================

# 安装可执行文件
install(TARGETS nevo_server
    RUNTIME DESTINATION bin
    COMPONENT Server
)

if(TARGET nevo_client)
    install(TARGETS nevo_client
        RUNTIME DESTINATION bin
        COMPONENT Client
    )
endif()

# 安装公共头文件
install(DIRECTORY src/core/include/nevo/
    DESTINATION include/nevo/core
    COMPONENT Development
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY src/network/include/nevo/
    DESTINATION include/nevo/network
    COMPONENT Development
    FILES_MATCHING PATTERN "*.h"
)

# 安装配置文件示例
install(FILES server_config.example.json
    DESTINATION etc/nevo
    COMPONENT Server
    OPTIONAL
)

# ============================================================
# CPack configuration
# ============================================================

set(CPACK_PACKAGE_NAME "NEVO")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION "Low-latency encrypted VoIP application")
set(CPACK_PACKAGE_VENDOR "NEVO Project")
set(CPACK_PACKAGE_CONTACT "nevo@example.com")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# 平台相关打包格式
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "NEVO")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6core6,libqt6widgets6,libopus0,libsodium23,libboost-system1.74,libsqlite3-0")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
endif()

include(CPack)