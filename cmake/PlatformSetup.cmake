# PlatformSetup.cmake
# 平台特定的编译器/链接器配置

function(nevo_platform_setup target)
    # --- Windows 特定 ---
    if(WIN32)
        target_compile_definitions(${target} PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _WINSOCK_DEPRECATED_NO_WARNINGS
        )
        # 链接 Winsock（网络模块需要）+ mswsock（AcceptEx 等扩展函数）
        target_link_libraries(${target} PRIVATE ws2_32 mswsock)
    endif()

    # --- macOS 特定 ---
    if(APPLE)
        # macOS 框架
        target_link_libraries(${target} PRIVATE
            "-framework CoreAudio"
            "-framework CoreFoundation"
        )
    endif()

    # --- Linux 特定 ---
    if(UNIX AND NOT APPLE)
        target_link_libraries(${target} PRIVATE pthread)
    endif()

    # --- Android 特定 ---
    if(ANDROID)
        target_link_libraries(${target} PRIVATE log)
    endif()

    # --- 通用设置 ---
    # 要求 POSIX 线程（用于 std::thread, std::mutex）
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${target} PRIVATE Threads::Threads)
endfunction()
