# FindRNNoise.cmake
# 查找 RNNoise 神经网络降噪库

find_path(RNNOISE_INCLUDE_DIR
    NAMES rnnoise.h
    PATHS
        /usr/include
        /usr/local/include
        $ENV{RNNOISE_ROOT}/include
        $ENV{PROGRAMFILES}/rnnoise/include
        C:/rnnoise/include
        ${CMAKE_PREFIX_PATH}
)

find_library(RNNOISE_LIBRARY
    NAMES rnnoise
    PATHS
        /usr/lib
        /usr/local/lib
        $ENV{RNNOISE_ROOT}/lib
        $ENV{PROGRAMFILES}/rnnoise/lib
        C:/rnnoise/lib
        ${CMAKE_PREFIX_PATH}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RNNoise
    DEFAULT_MSG
    RNNOISE_LIBRARY
    RNNOISE_INCLUDE_DIR
)

if(RNNoise_FOUND AND NOT TARGET RNNoise::RNNoise)
    add_library(RNNoise::RNNoise SHARED IMPORTED)
    set_target_properties(RNNoise::RNNoise PROPERTIES
        IMPORTED_LOCATION ${RNNOISE_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${RNNOISE_INCLUDE_DIR}
    )
endif()

mark_as_advanced(RNNOISE_INCLUDE_DIR RNNOISE_LIBRARY)
