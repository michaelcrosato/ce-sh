# lc_render_contracts: header-only CPU/GPU data contracts and view modes (portable, testable).
add_library(lc_render_contracts INTERFACE)
target_sources(lc_render_contracts INTERFACE
    FILE_SET HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES gpu_layouts.h view_mode.h
)
target_include_directories(lc_render_contracts INTERFACE "${CMAKE_SOURCE_DIR}/src")
