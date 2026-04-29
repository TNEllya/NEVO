# src/network/CMakeLists.txt
# NEVO 网络库

add_library(nevo_network
    src/TcpConnection.cpp
    src/UdpSocket.cpp
    src/SslWrapper.cpp
    src/ConnectionManager.cpp
    src/PacketRouter.cpp
    src/NatTraversal.cpp
    src/VoiceCrypto.cpp
    src/TcpVoiceTunnel.cpp
)

target_include_directories(nevo_network PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Conditional linking based on available dependencies
set(NEVO_NETWORK_LINK_LIBS nevo_core)

if(NEVO_HAS_BOOST)
    list(APPEND NEVO_NETWORK_LINK_LIBS Boost::system)
endif()

if(NEVO_HAS_OPENSSL)
    list(APPEND NEVO_NETWORK_LINK_LIBS OpenSSL::SSL OpenSSL::Crypto)
endif()

if(NEVO_HAS_SODIUM)
    list(APPEND NEVO_NETWORK_LINK_LIBS sodium::sodium)
endif()

target_link_libraries(nevo_network PUBLIC ${NEVO_NETWORK_LINK_LIBS})

nevo_set_compiler_warnings(nevo_network)
nevo_platform_setup(nevo_network)
