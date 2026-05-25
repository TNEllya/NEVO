# NEVO Test Suite
cmake_minimum_required(VERSION 3.21)

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
gtest_discover_tests(core_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

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
gtest_discover_tests(audio_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

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
gtest_discover_tests(nat_traversal_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ============================================================
# 注册所有测试
# ============================================================

include(GoogleTest)
