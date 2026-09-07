# NVIDIA Real-Time Denoisers (NRD) and its two build helpers, built from the pinned git submodules
# (docs/DEPENDENCIES.md records the exact commits and licenses). Nothing is downloaded: ShaderMake
# is told not to look for compilers and receives the Windows SDK DXC that compiles our own shaders,
# MathLib is header-only, and NRD embeds DXIL shaders only (no DXBC, no SPIR-V).
#
# Requires LC_DXC_EXECUTABLE (cmake/LcFindDxc.cmake) to be set before inclusion.

set(_lc_external "${CMAKE_SOURCE_DIR}/external")
foreach(_dep NRD ShaderMake MathLib)
    if(NOT EXISTS "${_lc_external}/${_dep}/CMakeLists.txt")
        message(FATAL_ERROR
            "external/${_dep} is empty. The denoiser dependencies are git submodules; run\n"
            "    git submodule update --init --recursive\n"
            "from the repository root and configure again.")
    endif()
endforeach()

# ShaderMake: build the tool (Release, nested project) and the blob reader; hand it our DXC.
set(SHADERMAKE_FIND_COMPILERS OFF CACHE BOOL "Last Circuit supplies the SDK DXC; no downloads" FORCE)
set(SHADERMAKE_FIND_DXC OFF CACHE BOOL "" FORCE)
set(SHADERMAKE_FIND_DXC_VK OFF CACHE BOOL "" FORCE)
set(SHADERMAKE_FIND_FXC OFF CACHE BOOL "" FORCE)
set(SHADERMAKE_FIND_SLANG OFF CACHE BOOL "" FORCE)
set(SHADERMAKE_TOOL ON CACHE BOOL "" FORCE)
set(SHADERMAKE_DXC_PATH "${LC_DXC_EXECUTABLE}" CACHE INTERNAL "DXC used for NRD's shaders (the SDK copy)")
set(SHADERMAKE_DXC_VK_PATH "" CACHE INTERNAL "")
set(SHADERMAKE_FXC_PATH "" CACHE INTERNAL "")
add_subdirectory("${_lc_external}/ShaderMake" "${CMAKE_BINARY_DIR}/external/ShaderMake")

# MathLib: header-only, used by NRD's own sources and shaders.
add_subdirectory("${_lc_external}/MathLib" "${CMAKE_BINARY_DIR}/external/MathLib")

# NRD: static library with embedded DXIL; shader blobs compiled into the build tree.
set(NRD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(NRD_NRI OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_SPIRV_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXIL_SHADERS ON CACHE BOOL "" FORCE)
set(NRD_SUPPORTS_CHECKERBOARD OFF CACHE BOOL "" FORCE)
set(NRD_SUPPORTS_VIEWPORT_OFFSET OFF CACHE BOOL "" FORCE)
set(NRD_NORMAL_ENCODING "2" CACHE STRING "R10G10B10A2 with material id (nrd::NormalEncoding::R10_G10_B10_A2_UNORM)" FORCE)
set(NRD_ROUGHNESS_ENCODING "1" CACHE STRING "Linear roughness (nrd::RoughnessEncoding::LINEAR)" FORCE)
set(NRD_SHADERS_PATH "${CMAKE_BINARY_DIR}/nrd_shaders" CACHE STRING "" FORCE)
add_subdirectory("${_lc_external}/NRD" "${CMAKE_BINARY_DIR}/external/NRD")

# Where application shaders find NRD.hlsli and the configure-time NRDConfig.hlsli.
set(LC_NRD_SHADER_INCLUDE_DIR "${_lc_external}/NRD/Shaders" CACHE INTERNAL "NRD shader include directory")
file(READ "${_lc_external}/NRD/Include/NRD.h" _lc_nrd_header)
string(REGEX MATCH "NRD_VERSION_MAJOR ([0-9]+)" _ "${_lc_nrd_header}")
set(_lc_nrd_major "${CMAKE_MATCH_1}")
string(REGEX MATCH "NRD_VERSION_MINOR ([0-9]+)" _ "${_lc_nrd_header}")
set(_lc_nrd_minor "${CMAKE_MATCH_1}")
string(REGEX MATCH "NRD_VERSION_BUILD ([0-9]+)" _ "${_lc_nrd_header}")
set(_lc_nrd_build "${CMAKE_MATCH_1}")
set(LC_NRD_VERSION "${_lc_nrd_major}.${_lc_nrd_minor}.${_lc_nrd_build}" CACHE INTERNAL "NRD version from Include/NRD.h")
message(STATUS "NRD ${LC_NRD_VERSION}: static library, DXIL shaders via ${SHADERMAKE_DXC_PATH}")

# Keep the third-party targets out of the IDE's top level.
foreach(_t NRD NRDShaders ShaderMakeBlob MathLib)
    if(TARGET ${_t})
        set_target_properties(${_t} PROPERTIES FOLDER "external")
    endif()
endforeach()
