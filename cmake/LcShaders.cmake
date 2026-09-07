# Build-time HLSL compilation with DXC. Every shader becomes bin/shaders/<name>.cso (signed DXIL)
# plus a <name>.pdb for PIX and debuggers. A failing compile prints the DXC diagnostics and the
# command line, so the shader, entry point, profile, and error are all visible.
#
# Requires: LC_DXC_EXECUTABLE, LC_SHADER_OUTPUT_DIR, LC_SHADER_HEADERS (all .hlsli files; listing
# them all as dependencies keeps the rule simple and always correct).

function(lc_add_shader OUT_LIST SOURCE ENTRY PROFILE)
    get_filename_component(_name "${SOURCE}" NAME_WE)
    set(_source_abs "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}")
    set(_out "${LC_SHADER_OUTPUT_DIR}/${_name}.cso")
    set(_pdb "${LC_SHADER_OUTPUT_DIR}/${_name}.pdb")

    add_custom_command(
        OUTPUT "${_out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LC_SHADER_OUTPUT_DIR}"
        COMMAND "${LC_DXC_EXECUTABLE}"
                -T ${PROFILE}
                -E ${ENTRY}
                -HV 2021
                -WX
                -Zi
                -Fd "${_pdb}"
                "$<IF:$<CONFIG:Debug>,-Od,-O3>"
                -I "${CMAKE_SOURCE_DIR}/shaders"
                -Fo "${_out}"
                "${_source_abs}"
        DEPENDS "${_source_abs}" ${LC_SHADER_HEADERS}
        COMMENT "DXC ${SOURCE} entry=${ENTRY} profile=${PROFILE} -> ${_name}.cso"
        VERBATIM)

    set(${OUT_LIST} ${${OUT_LIST}} "${_out}" PARENT_SCOPE)
endfunction()
