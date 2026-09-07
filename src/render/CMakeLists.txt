# lc_render_contracts: header-only CPU/GPU data contracts and view modes (portable, testable).
add_library(lc_render_contracts INTERFACE)
target_sources(lc_render_contracts INTERFACE
    FILE_SET HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES gpu_layouts.h nrd_settings.h view_mode.h
)
target_include_directories(lc_render_contracts INTERFACE "${CMAKE_SOURCE_DIR}/src")

# lc_render: scene residency, the frame renderer, and captures. Windows only.
if(WIN32)
    add_library(lc_render STATIC
        capture.cpp
        capture.h
        nrd_denoiser.cpp
        nrd_denoiser.h
        render_snapshot.h
        renderer.cpp
        renderer.h
        scene_gpu.cpp
        scene_gpu.h
    )
    target_link_libraries(lc_render PUBLIC lc_graphics lc_scene lc_render_contracts NRD)
endif()
