# FindOpus.cmake
# 查找 Opus 编解码库

find_path(OPUS_INCLUDE_DIR
    NAMES opus/opus.h
    PATHS
        /usr/include
        /usr/local/include
        $ENV{OPUS_ROOT}/include
)

find_library(OPUS_LIBRARY
    NAMES opus
    PATHS
        /usr/lib
        /usr/local/lib
        $ENV{OPUS_ROOT}/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Opus
    DEFAULT_MSG
    OPUS_LIBRARY
    OPUS_INCLUDE_DIR
)

if(Opus_FOUND AND NOT TARGET Opus::Opus)
    add_library(Opus::Opus SHARED IMPORTED)
    set_target_properties(Opus::Opus PROPERTIES
        IMPORTED_LOCATION ${OPUS_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${OPUS_INCLUDE_DIR}
    )
endif()

mark_as_advanced(OPUS_INCLUDE_DIR OPUS_LIBRARY)
