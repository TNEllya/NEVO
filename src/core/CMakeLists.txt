# src/core/CMakeLists.txt
# NEVO 核心共享库

add_library(nevo_core
    # common
    src/common/Logger.cpp
    # model
    src/model/User.cpp
    src/model/Channel.cpp
    src/model/Permission.cpp
    # protocol
    src/protocol/PacketCodec.cpp
    # audio
    src/audio/AudioMemoryPool.cpp
    src/audio/OpusEncoder.cpp
    src/audio/OpusDecoder.cpp
    src/audio/VoiceActivity.cpp
    src/audio/JitterBuffer.cpp
    src/audio/AudioMixer.cpp
    src/audio/Resampler.cpp
    src/audio/AudioEngine.cpp
)

target_include_directories(nevo_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/proto/generated>
    $<INSTALL_INTERFACE:include>
)

# Link nevo_config for compile definitions (NEVO_HAS_OPUS, etc.)
target_link_libraries(nevo_core PUBLIC nevo_config)

# Conditional linking based on available dependencies
set(NEVO_CORE_LINK_LIBS "")

if(NEVO_HAS_OPUS)
    list(APPEND NEVO_CORE_LINK_LIBS Opus::Opus)
endif()

# miniaudio is always available (FetchContent)
list(APPEND NEVO_CORE_LINK_LIBS miniaudio)

# spdlog is always available (FetchContent)
list(APPEND NEVO_CORE_LINK_LIBS spdlog::spdlog)

if(NEVO_HAS_BOOST AND NEVO_HAS_BOOST_LOCKFREE)
    list(APPEND NEVO_CORE_LINK_LIBS Boost::lockfree)
endif()

if(NEVO_HAS_PROTOBUF)
    list(APPEND NEVO_CORE_LINK_LIBS protobuf::libprotobuf)
endif()

if(NEVO_HAS_SODIUM)
    list(APPEND NEVO_CORE_LINK_LIBS sodium::sodium)
endif()

target_link_libraries(nevo_core PUBLIC ${NEVO_CORE_LINK_LIBS})

# RNNoise（可选）
find_package(RNNoise QUIET)
if(RNNoise_FOUND)
    target_link_libraries(nevo_core PUBLIC RNNoise::RNNoise)
    target_compile_definitions(nevo_core PUBLIC NEVO_HAS_RNNOISE)
endif()

nevo_set_compiler_warnings(nevo_core)
nevo_platform_setup(nevo_core)
