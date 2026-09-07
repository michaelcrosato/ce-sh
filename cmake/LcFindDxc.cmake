# Locates the DirectX Shader Compiler (dxc.exe) and requires its DXIL signing library (dxil.dll)
# to sit beside it; unsigned DXIL is rejected by the D3D12 runtime.
#
# Override with:  cmake --preset windows-debug -DLC_DXC_EXECUTABLE=<path to dxc.exe>
# The version actually used is printed at configure time and recorded in docs/DEPENDENCIES.md.

if(NOT LC_DXC_EXECUTABLE)
    set(_lc_kits_root "")
    get_filename_component(_lc_kits_root
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
    if(NOT _lc_kits_root OR _lc_kits_root MATCHES "registry" OR NOT IS_DIRECTORY "${_lc_kits_root}")
        set(_lc_kits_root "C:/Program Files (x86)/Windows Kits/10")
    endif()

    set(_lc_candidates "")
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        list(APPEND _lc_candidates "${_lc_kits_root}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
    endif()
    file(GLOB _lc_sdk_bins LIST_DIRECTORIES true "${_lc_kits_root}/bin/10.*")
    list(SORT _lc_sdk_bins COMPARE NATURAL ORDER DESCENDING)
    foreach(_dir IN LISTS _lc_sdk_bins)
        list(APPEND _lc_candidates "${_dir}/x64")
    endforeach()

    find_program(LC_DXC_EXECUTABLE
        NAMES dxc.exe dxc
        PATHS ${_lc_candidates}
        NO_DEFAULT_PATH
        DOC "Path to dxc.exe (Windows SDK or a pinned DXC release)")
endif()

if(NOT LC_DXC_EXECUTABLE OR NOT EXISTS "${LC_DXC_EXECUTABLE}")
    message(FATAL_ERROR
        "dxc.exe was not found. Install the Windows 11 SDK (10.0.26100 or newer) or pass "
        "-DLC_DXC_EXECUTABLE=<path to dxc.exe>. dxil.dll must be in the same directory.")
endif()

get_filename_component(_lc_dxc_dir "${LC_DXC_EXECUTABLE}" DIRECTORY)
if(NOT EXISTS "${_lc_dxc_dir}/dxil.dll")
    message(FATAL_ERROR
        "dxil.dll is missing next to ${LC_DXC_EXECUTABLE}. Without it DXC cannot sign shaders and "
        "D3D12 will refuse them. Use the Windows SDK dxc.exe or a full DXC release package.")
endif()

execute_process(
    COMMAND "${LC_DXC_EXECUTABLE}" --version
    OUTPUT_VARIABLE _lc_dxc_version
    ERROR_VARIABLE _lc_dxc_version_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _lc_dxc_result)
if(_lc_dxc_result EQUAL 0)
    string(REPLACE "\n" " | " _lc_dxc_version "${_lc_dxc_version}")
    set(LC_DXC_VERSION "${_lc_dxc_version}" CACHE INTERNAL "Reported DXC version")
    message(STATUS "DXC: ${LC_DXC_EXECUTABLE}")
    message(STATUS "DXC version: ${LC_DXC_VERSION}")
else()
    message(WARNING "Could not query the DXC version from ${LC_DXC_EXECUTABLE}: ${_lc_dxc_version_err}")
endif()
