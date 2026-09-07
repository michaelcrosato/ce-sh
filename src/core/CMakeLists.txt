# lc_core: portable utilities with no Windows or graphics dependencies.
add_library(lc_core STATIC
    cli.cpp
    cli.h
    clock.cpp
    clock.h
    error.cpp
    error.h
    ids.h
    image_write.cpp
    image_write.h
    json_writer.cpp
    json_writer.h
    log.cpp
    log.h
    math/camera_math.cpp
    math/camera_math.h
    math/mat.cpp
    math/mat.h
    math/vec.h
)

target_include_directories(lc_core PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${LC_GENERATED_DIR}"
)
target_link_libraries(lc_core PUBLIC lc_options)
add_dependencies(lc_core lc_build_info)
