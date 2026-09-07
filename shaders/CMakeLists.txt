# Shaders are compiled at build time into ${CMAKE_BINARY_DIR}/bin/shaders next to the executable.
include(LcFindDxc)
include(LcShaders)

set(LC_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders")
set(LC_SHADER_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/layouts.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/materials.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/math.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/sampling.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/trace/ray_interface.hlsli"
)

set(LC_SHADER_OUTPUTS "")
lc_add_shader(LC_SHADER_OUTPUTS trace/camera_view.hlsl main cs_6_5)
lc_add_shader(LC_SHADER_OUTPUTS trace/path_trace.hlsl main cs_6_5)
lc_add_shader(LC_SHADER_OUTPUTS tests/layout_probe.hlsl main cs_6_5)

# The .hlsl files are deliberately not listed as target sources: the Visual Studio generator would
# otherwise hand them to the legacy FXC "HLSL Compiler" item type.
add_custom_target(lc_shaders ALL DEPENDS ${LC_SHADER_OUTPUTS})
set_property(GLOBAL PROPERTY LC_SHADER_OUTPUT_DIR "${LC_SHADER_OUTPUT_DIR}")
