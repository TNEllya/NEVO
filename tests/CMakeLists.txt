# NEVO Test Suite
cmake_minimum_required(VERSION 3.21)

include(GoogleTest)

# ============================================================
# 核心模块测试
# ============================================================

add_executable(core_test
    core_tests/TestChannel.cpp
    core_tests/TestChannelAdvanced.cpp
    core_tests/TestPermission.cpp
    core_tests/TestPermissionEdgeCases.cpp
    core_tests/TestResult.cpp
    core_tests/TestResultEdgeCases.cpp
    core_tests/TestUser.cpp
    core_tests/TestPacketCodec.cpp
)
target_link_libraries(core_test
    PRIVATE
    nevo_core
    nevo_config
    GTest::gtest_main
)
target_include_directories(core_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/proto/generated
)
gtest_add_tests(TARGET core_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
nevo_platform_setup(core_test)
nevo_set_compiler_warnings(core_test)
if(MSVC)
    # 测试代码存在大量历史 size_t->uint32 告警，与 server_test 一致放宽 /WX
    target_compile_options(core_test PRIVATE /WX-)
    target_compile_definitions(core_test PRIVATE _USE_MATH_DEFINES)
endif()

# ============================================================
# 音频模块测试
# ============================================================

add_executable(audio_test
    audio_tests/TestAudioMemoryPool.cpp
    audio_tests/TestAudioMixer.cpp
    audio_tests/TestAudioMixerAdvanced.cpp
    audio_tests/TestJitterBuffer.cpp
    audio_tests/TestJitterBufferMultiUser.cpp
    audio_tests/TestJitterBufferThreadSafety.cpp
    audio_tests/TestOpusCodec.cpp
    audio_tests/TestResampler.cpp
    audio_tests/TestVoiceActivity.cpp
)
target_link_libraries(audio_test
    PRIVATE
    nevo_core
    nevo_config
    GTest::gtest_main
)
target_include_directories(audio_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/include
)
gtest_add_tests(TARGET audio_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
nevo_platform_setup(audio_test)
nevo_set_compiler_warnings(audio_test)
if(MSVC)
    # 测试代码存在大量历史 size_t->uint32 告警，与 server_test 一致放宽 /WX
    target_compile_options(audio_test PRIVATE /WX-)
    target_compile_definitions(audio_test PRIVATE _USE_MATH_DEFINES)
endif()

# ============================================================
# 网络模块测试
# ============================================================

add_executable(nat_traversal_test
    network_tests/TestNatTraversal.cpp
    network_tests/TestNatTraversalAuth.cpp
    network_tests/TestNatTraversalCodec.cpp
    network_tests/TestTcpConnection.cpp
    network_tests/TestTcpConnectionTimeout.cpp
    network_tests/TestTcpPayloadValidation.cpp
    network_tests/TestVoiceCrypto.cpp
)
target_link_libraries(nat_traversal_test
    PRIVATE
    nevo_network
    nevo_core
    nevo_config
    GTest::gtest_main
)
target_include_directories(nat_traversal_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/network/include
    ${CMAKE_SOURCE_DIR}/src/core/include
)
gtest_add_tests(TARGET nat_traversal_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
nevo_platform_setup(nat_traversal_test)
nevo_set_compiler_warnings(nat_traversal_test)
if(MSVC)
    # 测试代码存在历史告警（未引用静态函数等），与 server_test 一致放宽 /WX
    target_compile_options(nat_traversal_test PRIVATE /WX-)
    target_compile_definitions(nat_traversal_test PRIVATE _USE_MATH_DEFINES)
endif()

# ============================================================
# 服务端集成测试
# ============================================================

add_executable(server_test
    server_tests/TestServerIntegration.cpp
    server_tests/TestServerArgParsing.cpp
    server_tests/TestVoiceRelayIntegration.cpp
    server_tests/TestAudioRelaySecurity.cpp
)
target_link_libraries(server_test
    PRIVATE
    nevo_server_lib
    nevo_config
    GTest::gtest_main
)
target_include_directories(server_test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/server/include
    ${CMAKE_SOURCE_DIR}/src/network/include
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/proto/generated
)
gtest_add_tests(TARGET server_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
nevo_platform_setup(server_test)
nevo_set_compiler_warnings(server_test)
if(MSVC)
    target_compile_options(server_test PRIVATE /WX-)
endif()

