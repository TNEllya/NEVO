# src/CMakeLists.txt
# NEVO 源码子目录入口

# 共享核心库（音频引擎、协议、数据模型）
add_subdirectory(core)

# 网络库（TCP/UDP、NAT穿透、语音加密）——需要 Boost
if(NEVO_HAS_BOOST)
    add_subdirectory(network)
else()
    message(STATUS "Skipping network module: Boost not found")
endif()

# 服务端可执行文件 ——需要 Boost 和 SQLite3
if(NEVO_HAS_BOOST AND NEVO_HAS_SQLITE)
    add_subdirectory(server)
else()
    message(STATUS "Skipping server module: Boost=${NEVO_HAS_BOOST}, SQLite3=${NEVO_HAS_SQLITE}")
endif()

# 客户端核心库 ——需要 Boost (依赖 nevo_network)
if(NEVO_HAS_BOOST)
    add_subdirectory(client)
else()
    message(STATUS "Skipping client module: Boost not found")
endif()

# Qt 6 UI（可选，需要 Qt6）
if(Qt6_FOUND)
    add_subdirectory(ui)
else()
    message(STATUS "Skipping UI module: Qt6 not found")
endif()

# 语音通信模拟测试（独立可执行文件，不依赖 GTest）
if(NEVO_HAS_BOOST AND NEVO_HAS_SODIUM AND NEVO_HAS_OPUS)
    add_executable(voice_relay_sim
        voice_relay_sim.cpp
    )
    target_link_libraries(voice_relay_sim
        PRIVATE
        nevo_network
        nevo_core
        nevo_config
    )
    nevo_set_compiler_warnings(voice_relay_sim)
    nevo_platform_setup(voice_relay_sim)
    message(STATUS "Voice relay simulation test: ENABLED")
else()
    message(STATUS "Skipping voice_relay_sim: Boost=${NEVO_HAS_BOOST}, Sodium=${NEVO_HAS_SODIUM}, Opus=${NEVO_HAS_OPUS}")
endif()
