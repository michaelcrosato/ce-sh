cmake_minimum_required(VERSION 3.28)

# C is enabled for miniaudio.c: without it the file took MSBuild's default runtime library (the
# release static CRT in every configuration), the source of the LNK4098 warning since M5 (KI-034).
project(LastCircuit
    VERSION 0.1.0
    DESCRIPTION "Last Circuit: a small custom Windows engine with mandatory hardware path tracing"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The static CRT in both configurations (M7, D-053): the package needs no Visual C++ redistributable
# and every static library in the link (NRD, ShaderMake's blob reader, Dear ImGui, miniaudio) uses
# the same runtime as the executable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# One flat output layout regardless of generator: build/<preset>/bin/LastCircuit.exe.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_cfg}" _CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/lib")
endforeach()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(LcCompilerOptions)
include(LcBuildInfo)

enable_testing()

if(WIN32)
    include(LcFindDxc)   # LC_DXC_EXECUTABLE: compiles our shaders and NRD's.
    include(LcNrd)       # NRD, ShaderMake, MathLib from external/ (git submodules).
endif()

add_subdirectory(src)
if(WIN32)
    add_subdirectory(shaders)
endif()
add_subdirectory(tests)
