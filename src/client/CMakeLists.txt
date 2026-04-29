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

if(Qt6_FOUND)
    target_link_libraries(nevo_client PUBLIC Qt6::Core)
endif()

nevo_set_compiler_warnings(nevo_client)
nevo_platform_setup(nevo_client)
