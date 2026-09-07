# Script mode: cmake -DLC_SOURCE_DIR=... -DLC_OUTPUT=... -DLC_TEMPLATE=... -DLC_GIT=... -DLC_CONFIG=... -DLC_VERSION=... -P LcBuildInfoStamp.cmake
# Writes the header only when its content changes, so unchanged builds do not recompile.

set(LC_GIT_COMMIT "unknown")
set(LC_GIT_DIRTY 0)

if(LC_GIT)
    execute_process(
        COMMAND "${LC_GIT}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${LC_SOURCE_DIR}"
        OUTPUT_VARIABLE _commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _result
        ERROR_QUIET)
    if(_result EQUAL 0 AND NOT "${_commit}" STREQUAL "")
        set(LC_GIT_COMMIT "${_commit}")
    endif()

    execute_process(
        COMMAND "${LC_GIT}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${LC_SOURCE_DIR}"
        OUTPUT_VARIABLE _status
        RESULT_VARIABLE _result
        ERROR_QUIET)
    if(_result EQUAL 0 AND NOT "${_status}" STREQUAL "")
        set(LC_GIT_DIRTY 1)
    endif()
endif()

if(NOT LC_CONFIG)
    set(LC_CONFIG "unknown")
endif()

configure_file("${LC_TEMPLATE}" "${LC_OUTPUT}" @ONLY)
