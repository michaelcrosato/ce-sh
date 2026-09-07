# lc_core: portable utilities with no Windows or graphics dependencies.
add_library(lc_core STATIC
    clock.cpp
    clock.h
    error.cpp
    error.h
    ids.h
    log.cpp
    log.h
)

target_include_directories(lc_core PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${LC_GENERATED_DIR}"
)
target_link_libraries(lc_core PUBLIC lc_options)
add_dependencies(lc_core lc_build_info)
