# Build-time HLSL compilation with DXC. Every shader becomes bin/shaders/<name>.cso (signed DXIL)
# plus a <name>.pdb for PIX and debuggers. A failing compile prints the DXC diagnostics and the
# command line, so the shader, entry point, profile, and error are all visible.
# -Zpr makes row-major the default matrix packing for every shader, matching the
# #pragma pack_matrix(row_major) in shaders/shared/layouts.hlsli (one explicit layout, spec §9).
#
# Requires: LC_DXC_EXECUTABLE, LC_SHADER_OUTPUT_DIR, LC_SHADER_HEADERS (all .hlsli files; listing
# them all as dependencies keeps the rule simple and always correct). LC_NRD_SHADER_INCLUDE_DIR,
# when set, is added to the include path (NRD.hlsli packing helpers).
#
#   lc_add_shader(<out list> <source> <entry> <profile> [OUTPUT_NAME <name>] [DEFINES <macro=value>...])
#
# OUTPUT_NAME lets one source produce several variants (path_trace.hlsl -> path_trace_guided.cso).

function(lc_add_shader OUT_LIST SOURCE ENTRY PROFILE)
    cmake_parse_arguments(_arg "" "OUTPUT_NAME" "DEFINES" ${ARGN})
    get_filename_component(_name "${SOURCE}" NAME_WE)
    if(_arg_OUTPUT_NAME)
        set(_name "${_arg_OUTPUT_NAME}")
    endif()
    set(_source_abs "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}")
    set(_out "${LC_SHADER_OUTPUT_DIR}/${_name}.cso")
    set(_pdb "${LC_SHADER_OUTPUT_DIR}/${_name}.pdb")

    set(_defines "")
    foreach(_d IN LISTS _arg_DEFINES)
        list(APPEND _defines -D "${_d}")
    endforeach()
    set(_includes -I "${CMAKE_SOURCE_DIR}/shaders")
    if(LC_NRD_SHADER_INCLUDE_DIR)
        list(APPEND _includes -I "${LC_NRD_SHADER_INCLUDE_DIR}")
    endif()

    add_custom_command(
        OUTPUT "${_out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LC_SHADER_OUTPUT_DIR}"
        COMMAND "${LC_DXC_EXECUTABLE}"
                -T ${PROFILE}
                -E ${ENTRY}
                -HV 2021
                -WX
                -Zpr
                -Zi
                -Fd "${_pdb}"
                "$<IF:$<CONFIG:Debug>,-Od,-O3>"
                ${_defines}
                ${_includes}
                -Fo "${_out}"
                "${_source_abs}"
        DEPENDS "${_source_abs}" ${LC_SHADER_HEADERS}
        COMMENT "DXC ${SOURCE} entry=${ENTRY} profile=${PROFILE} ${_arg_DEFINES} -> ${_name}.cso"
        VERBATIM)

    set(${OUT_LIST} ${${OUT_LIST}} "${_out}" PARENT_SCOPE)
endfunction()
