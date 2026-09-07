# Shared compiler settings for every Last Circuit target.
# Link the lc_options interface target to inherit them.

add_library(lc_options INTERFACE)

if(MSVC)
    target_compile_options(lc_options INTERFACE
        /W4                 # High warning level; warnings are errors below.
        /WX
        /permissive-        # Standards conformance.
        /Zc:__cplusplus     # Report the real C++ standard macro.
        /Zc:preprocessor    # Conforming preprocessor.
        /utf-8              # Source and execution character sets are UTF-8.
        /EHsc               # C++ exceptions, no SEH unwinding of extern "C".
        /MP                 # Parallel compilation within a project.
        /Zi                 # Symbols in every configuration (profiling needs release symbols).
        /external:anglebrackets   # SDK and CRT headers are external: keep their warnings out of /WX.
        /external:W0
    )
    target_compile_definitions(lc_options INTERFACE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_options(lc_options INTERFACE
        /DEBUG
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
else()
    target_compile_options(lc_options INTERFACE -Wall -Wextra -Werror)
endif()
