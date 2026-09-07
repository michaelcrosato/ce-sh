# lc_imgui: Dear ImGui (pinned submodule, docs/DEPENDENCIES.md) with its Win32 and D3D12 backends.
# Third-party code: compiled at /W3 without our warnings-as-errors.
set(_imgui "${CMAKE_SOURCE_DIR}/external/imgui")
if(NOT EXISTS "${_imgui}/imgui.cpp")
    message(FATAL_ERROR "external/imgui is empty; run git submodule update --init --recursive")
endif()
add_library(lc_imgui STATIC
    "${_imgui}/imgui.cpp"
    "${_imgui}/imgui_draw.cpp"
    "${_imgui}/imgui_tables.cpp"
    "${_imgui}/imgui_widgets.cpp"
    "${_imgui}/backends/imgui_impl_win32.cpp"
    "${_imgui}/backends/imgui_impl_dx12.cpp"
)
target_include_directories(lc_imgui PUBLIC "${_imgui}" "${_imgui}/backends")
target_compile_definitions(lc_imgui PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN IMGUI_DISABLE_DEMO_WINDOWS)
target_compile_options(lc_imgui PRIVATE /W3 /EHsc)
target_link_libraries(lc_imgui PUBLIC d3dcompiler)  # The D3D12 backend compiles its two shaders once at start-up.
set_target_properties(lc_imgui PROPERTIES FOLDER "external")

# lc_ui: the game's screens (prompts, cues, pause menu, settings, diagnostic panel) on top of ImGui.
add_library(lc_ui STATIC
    ui.cpp
    ui.h
)
target_link_libraries(lc_ui PUBLIC lc_imgui lc_graphics lc_platform lc_game lc_core lc_options)
