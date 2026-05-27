cmake_minimum_required(VERSION 3.21)
project(nevo_android_jni)

set(NEVO_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

add_library(nevo_jni SHARED
    jni_bridge.cpp
    jni_audio.cpp
    jni_crypto.cpp
)

find_library(sodium-lib sodium)
if(sodium-lib)
    target_link_libraries(nevo_jni ${sodium-lib})
endif()

find_library(opus-lib opus)
if(opus-lib)
    target_link_libraries(nevo_jni ${opus-lib})
endif()

target_include_directories(nevo_jni PRIVATE
    ${NEVO_SRC_ROOT}/src/core
    ${NEVO_SRC_ROOT}/src/network
    ${NEVO_SRC_ROOT}/3rdparty
)

target_compile_features(nevo_jni PRIVATE cxx_std_20)