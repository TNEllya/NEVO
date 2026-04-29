# src/ui/CMakeLists.txt
# NEVO Qt 6 界面

# 仅当 Qt6 Widgets 可用时构建
find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6Widgets_FOUND)
    message(STATUS "Skipping UI module: Qt6::Widgets not found")
    return()
endif()

# Translation support
find_package(Qt6 COMPONENTS LinguistTools QUIET)

set(TS_FILES
    translations/nevo_client_en.ts
    translations/nevo_client_zh_CN.ts
    translations/nevo_client_zh_TW.ts
)

add_executable(nevo_client_ui WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/ChannelTreeModel.cpp
    src/ChannelItemDelegate.cpp
    src/UserListModel.cpp
    src/AudioSettingsWidget.cpp
    src/ConnectionBar.cpp
    src/ThemeManager.cpp
    src/IconProvider.cpp
    include/nevo/ui/MainWindow.h
    include/nevo/ui/ChannelTreeModel.h
    include/nevo/ui/ChannelItemDelegate.h
    include/nevo/ui/UserListModel.h
    include/nevo/ui/ConnectionBar.h
    include/nevo/ui/AudioSettingsWidget.h
    include/nevo/ui/ThemeManager.h
    include/nevo/ui/IconProvider.h
)

set_target_properties(nevo_client_ui PROPERTIES
    AUTOMOC ON
    CMAKE_AUTOMOC ON
)

# Build .qm files from .ts translations and embed as Qt resources
if(Qt6LinguistTools_FOUND)
    qt_add_translations(nevo_client_ui
        TS_FILES ${TS_FILES}
        RESOURCE_PREFIX "/i18n"
    )
endif()

target_include_directories(nevo_client_ui PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(nevo_client_ui PRIVATE
    Qt6::Core
    Qt6::Widgets
    nevo_core
)

# 如果有 nevo_client 可用则链接（客户端需要 Boost）
if(TARGET nevo_client)
    target_link_libraries(nevo_client_ui PRIVATE nevo_client)
endif()

nevo_set_compiler_warnings(nevo_client_ui)
nevo_platform_setup(nevo_client_ui)
