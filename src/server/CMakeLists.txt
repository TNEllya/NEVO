# src/server/CMakeLists.txt
# NEVO 服务端可执行文件

add_executable(nevo_server
    src/main.cpp
    src/ServerCore.cpp
    src/ClientSession.cpp
    src/ChannelManager.cpp
    src/AudioRelay.cpp
    src/VideoRelay.cpp
    src/ControlServer.cpp
    src/Database.cpp
    src/ServerConfig.cpp
)

target_include_directories(nevo_server PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Conditional linking based on available dependencies
set(NEVO_SERVER_LINK_LIBS nevo_core nevo_network)

if(NEVO_HAS_SQLITE)
    list(APPEND NEVO_SERVER_LINK_LIBS SQLite3::SQLite3)
endif()

if(NEVO_HAS_SODIUM)
    list(APPEND NEVO_SERVER_LINK_LIBS sodium::sodium)
endif()

# argon2 is needed for password hashing - keep as required if available
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ARGON2 QUIET libargon2)
endif()
if(ARGON2_FOUND)
    list(APPEND NEVO_SERVER_LINK_LIBS argon2)
else()
    # Try to find argon2 header directly
    find_path(ARGON2_INCLUDE_DIR NAMES argon2.h)
    find_library(ARGON2_LIBRARY NAMES argon2 argon2d)
    if(ARGON2_INCLUDE_DIR AND ARGON2_LIBRARY)
        list(APPEND NEVO_SERVER_LINK_LIBS ${ARGON2_LIBRARY})
    endif()
endif()

target_link_libraries(nevo_server PRIVATE ${NEVO_SERVER_LINK_LIBS})

nevo_set_compiler_warnings(nevo_server)
nevo_platform_setup(nevo_server)

# Copy vcpkg DLLs to output directory after build (Windows only)
if(WIN32)
    set(NEVO_VCPKG_DLLS
        sqlite3.dll libprotobuf.dll libsodium.dll
        libssl-3-x64.dll libcrypto-3-x64.dll abseil_dll.dll
    )
    foreach(dll ${NEVO_VCPKG_DLLS})
        add_custom_command(TARGET nevo_server POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/${dll}"
                "$<TARGET_FILE_DIR:nevo_server>/${dll}"
            COMMENT "Copying ${dll} to output directory"
        )
    endforeach()
endif()

# ============================================================
# Server GUI (optional, requires Qt6)
# ============================================================
if(NEVO_HAS_QT)
add_executable(nevo_server_gui WIN32
    ui/src/server_gui_main.cpp
    ui/src/SessionTableModel.cpp
    ui/src/ServerStatusBar.cpp
    ui/src/ServerLogView.cpp
    ui/src/ServerMainWindow.cpp
    ui/src/ServerConfigPanel.cpp
    ui/src/ChannelPanel.cpp
    src/ServerCore.cpp
    src/ClientSession.cpp
    src/ChannelManager.cpp
    src/AudioRelay.cpp
    src/VideoRelay.cpp
    src/ControlServer.cpp
    src/Database.cpp
    src/ServerConfig.cpp
    ../ui/src/ThemeManager.cpp
    ../ui/include/nevo/ui/ThemeManager.h
    ui/include/nevo/server/ui/SessionTableModel.h
    ui/include/nevo/server/ui/ServerStatusBar.h
    ui/include/nevo/server/ui/ServerLogView.h
    ui/include/nevo/server/ui/ServerMainWindow.h
    ui/include/nevo/server/ui/ServerConfigPanel.h
    ui/include/nevo/server/ui/ChannelPanel.h
)

    set_target_properties(nevo_server_gui PROPERTIES
        AUTOMOC ON
        CMAKE_AUTOMOC ON
    )

    # Translation support for server GUI
    find_package(Qt6 COMPONENTS LinguistTools QUIET)
    set(SERVER_TS_FILES
        ui/translations/nevo_server_en.ts
        ui/translations/nevo_server_zh_CN.ts
        ui/translations/nevo_server_zh_TW.ts
    )
    if(Qt6LinguistTools_FOUND)
        qt_add_translations(nevo_server_gui
            TS_FILES ${SERVER_TS_FILES}
            RESOURCE_PREFIX "/i18n"
        )
    endif()

    target_include_directories(nevo_server_gui PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ui/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/ui/include>
    )

    target_link_libraries(nevo_server_gui PRIVATE
        ${NEVO_SERVER_LINK_LIBS}
        Qt6::Core
        Qt6::Widgets
        nevo_core
    )

    nevo_set_compiler_warnings(nevo_server_gui)
    nevo_platform_setup(nevo_server_gui)
endif()
