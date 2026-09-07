# Generates <build>/generated/core/build_info.h on every build (not just at configure time)
# so the executable always reports the commit it was actually built from.

set(LC_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(LC_BUILD_INFO_HEADER "${LC_GENERATED_DIR}/core/build_info.h")

find_package(Git QUIET)
if(GIT_FOUND)
    set(_lc_git "${GIT_EXECUTABLE}")
else()
    set(_lc_git "")
endif()

add_custom_target(lc_build_info ALL
    COMMAND "${CMAKE_COMMAND}"
        -DLC_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DLC_OUTPUT=${LC_BUILD_INFO_HEADER}
        -DLC_TEMPLATE=${CMAKE_SOURCE_DIR}/cmake/build_info.h.in
        -DLC_GIT=${_lc_git}
        -DLC_CONFIG=$<CONFIG>
        -DLC_VERSION=${PROJECT_VERSION}
        -P "${CMAKE_SOURCE_DIR}/cmake/LcBuildInfoStamp.cmake"
    BYPRODUCTS "${LC_BUILD_INFO_HEADER}"
    COMMENT "Stamping build_info.h"
    VERBATIM)
