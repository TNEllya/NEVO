# ============================================================================
# NEVO Android JNI bridge（nevo_jni）
#
# 【当前状态：未接入 Gradle 构建】
# app/build.gradle.kts 目前没有配置 externalNativeBuild，本目录不会参与构建。
# 原因：仓库内缺少 libsodium / opus 的 Android 预编译库（.a/.so）或对应源码。
#
# 【接入前需先提供依赖，位置约定】
#   mobile/android/native/libs/<abi>/libsodium.a   （abi 如 arm64-v8a / armeabi-v7a / x86_64）
#   mobile/android/native/libs/<abi>/libopus.a
#   对应头文件放入 mobile/android/native/libs/include/（sodium.h、opus/opus.h）
#   （或使用 vcpkg 的 Android triplet 构建后拷贝到上述位置；
#     仓库根的 vcpkg/ 目录仅为 ports 注册表，未预构建任何库）
#
# 【接入步骤】
#   1. 提供上述预编译库后，在 app/build.gradle.kts 的 android.defaultConfig 中增加：
#        externalNativeBuild {
#            cmake {
#                path = file("${'$'}{project.rootDir}/native/CMakeLists.txt")
#            }
#        }
#        ndk {
#            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
#        }
#   2. 下方 find_library 仅能定位 NDK 自带系统库；libsodium/opus 需要改为
#      显式 IMPORTED 目标指向 libs/<abi>/ 下的预编译库后再链接。
#
# 【未接入时的运行时行为】
#   CryptoManager / NativeAudioEngine 在 System.loadLibrary("nevo_jni") 失败时
#   记录警告；密钥对生成、crypto_box_seal、Opus 编解码将不可用，
#   CryptoManager 会抛出 IllegalStateException（"原生加密库 nevo_jni 未加载"），
#   登录流程将失败并断开连接（失败即断开，不做不安全降级）。
# ============================================================================

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