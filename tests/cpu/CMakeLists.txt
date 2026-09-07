# CPU tests: portable checks for math, parsing, image encoding, scene data, and layouts.
add_executable(lc_cpu_tests
    cpu_raycast.h
    lc_test.h
    lc_test_main.cpp
    test_cli.cpp
    test_emitters.cpp
    test_image_write.cpp
    test_json.cpp
    test_json_reader.cpp
    test_layouts.cpp
    test_log.cpp
    test_material.cpp
    test_math.cpp
    test_radiometry.cpp
    test_scene.cpp
    test_simulation.cpp
)
target_include_directories(lc_cpu_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lc_cpu_tests PRIVATE lc_core lc_scene lc_game lc_app_options lc_render_contracts lc_options)

add_test(NAME cpu_tests COMMAND lc_cpu_tests)
set_tests_properties(cpu_tests PROPERTIES LABELS cpu)
