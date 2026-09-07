# lc_platform: Win32 window, files, and error reporting. Windows only.
add_library(lc_platform STATIC
    files.cpp
    files.h
    win_error.cpp
    win_error.h
    window.cpp
    window.h
)
target_link_libraries(lc_platform PUBLIC lc_core PRIVATE user32 dxgi d3d12)
