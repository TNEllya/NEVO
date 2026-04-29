# tests/CMakeLists.txt
# NEVO 测试

# 核心库测试（always available - core is always built）
add_executable(nevo_core_tests
    core_tests/TestResult.cpp
    core_tests/TestChannel.cpp
    core_tests/TestPermission.cpp
)
target_link_libraries(nevo_core_tests PRIVATE nevo_core GTest::gtest_main)
add_test(NAME core_tests COMMAND nevo_core_tests)

# 音频模块测试（needs Opus）
if(NEVO_HAS_OPUS)
    add_executable(nevo_audio_tests
        audio_tests/TestOpusCodec.cpp
        audio_tests/TestJitterBuffer.cpp
        audio_tests/TestAudioMixer.cpp
        audio_tests/TestAudioMemoryPool.cpp
    )
    target_link_libraries(nevo_audio_tests PRIVATE nevo_core GTest::gtest_main)
    add_test(NAME audio_tests COMMAND nevo_audio_tests)
else()
    message(STATUS "Skipping audio tests: Opus not found")
endif()

# 网络模块测试（needs Boost for network module）
if(NEVO_HAS_BOOST)
    add_executable(nevo_network_tests
        network_tests/TestTcpConnection.cpp
        network_tests/TestVoiceCrypto.cpp
        network_tests/TestNatTraversal.cpp
    )
    target_link_libraries(nevo_network_tests PRIVATE nevo_network GTest::gtest_main)
    add_test(NAME network_tests COMMAND nevo_network_tests)
else()
    message(STATUS "Skipping network tests: Boost not found")
endif()

# 服务端集成测试（needs Boost and SQLite3）
if(NEVO_HAS_BOOST AND NEVO_HAS_SQLITE)
    add_executable(nevo_server_tests
        server_tests/TestServerIntegration.cpp
    )
    target_link_libraries(nevo_server_tests PRIVATE nevo_server GTest::gtest_main)
    add_test(NAME server_tests COMMAND nevo_server_tests)
else()
    message(STATUS "Skipping server tests: Boost=${NEVO_HAS_BOOST}, SQLite3=${NEVO_HAS_SQLITE}")
endif()
