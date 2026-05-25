# src/client/CMakeLists.txt
# NEVO 客户端核心库

add_library(nevo_client
    src/ClientCore.cpp
    src/AudioInput.cpp
    src/AudioOutput.cpp
    src/NetworkManager.cpp
)

target_include_directories(nevo_client PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(nevo_client PUBLIC
    nevo_core
    nevo_network
)

if(Qt6_FOUND AND NOT MINGW)
    target_link_libraries(nevo_client PUBLIC Qt6::Core)
    target_compile_definitions(nevo_client PUBLIC NEVO_HAS_QT)
endif()

nevo_set_compiler_warnings(nevo_client)
nevo_platform_setup(nevo_client)

if(NEVO_HAS_BOOST AND NEVO_HAS_SODIUM AND NEVO_HAS_OPUS)
    add_executable(nevo_console_client
        console_main.cpp
    )
    target_link_libraries(nevo_console_client PRIVATE
        nevo_client
        nevo_core
        nevo_network
    )
    nevo_set_compiler_warnings(nevo_console_client)
    nevo_platform_setup(nevo_console_client)
    message(STATUS "Console client executable: ENABLED")
else()
    message(STATUS "Skipping console client: missing dependencies")
endif()
