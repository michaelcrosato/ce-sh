# lc_app_options: command-line handling, portable so it can be unit-tested without a GPU.
add_library(lc_app_options STATIC
    options.cpp
    options.h
)
target_link_libraries(lc_app_options PUBLIC lc_core lc_render_contracts)
