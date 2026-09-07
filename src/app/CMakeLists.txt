# lc_app_options: command-line handling, portable so it can be unit-tested without a GPU.
add_library(lc_app_options STATIC
    benchmark.cpp
    benchmark.h
    options.cpp
    options.h
    temporal_checks.cpp
    temporal_checks.h
)
target_link_libraries(lc_app_options PUBLIC lc_core lc_render_contracts)

# LastCircuit.exe: console-subsystem development build (logs go to stderr). Windows only.
if(WIN32)
    add_executable(LastCircuit
        application.cpp
        application.h
        environment_report.cpp
        environment_report.h
        main.cpp
    )
    target_link_libraries(LastCircuit PRIVATE lc_app_options lc_ui lc_render lc_graphics lc_platform lc_game lc_scene lc_core lc_options advapi32)
    add_dependencies(LastCircuit lc_shaders)
    # Assets live beside the executable (the approved asset root; the M7 package keeps this layout).
    add_custom_command(TARGET LastCircuit POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:LastCircuit>/assets"
        COMMENT "Copying assets beside LastCircuit.exe")
    set_target_properties(LastCircuit PROPERTIES
        VS_DEBUGGER_COMMAND_ARGUMENTS "--scene rt_boxes"
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
