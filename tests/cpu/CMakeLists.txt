# CPU tests: portable checks for math, parsing, image encoding, scene data, and layouts.
add_executable(lc_cpu_tests
    cpu_raycast.h
    lc_test.h
    lc_test_main.cpp
    test_audio.cpp
    test_benchmark.cpp
    test_cli.cpp
    test_collision.cpp
    test_emitters.cpp
    test_image_write.cpp
    test_json.cpp
    test_json_reader.cpp
    test_layouts.cpp
    test_log.cpp
    test_material.cpp
    test_math.cpp
    test_objective.cpp
    test_radiometry.cpp
    test_reconstruction.cpp
    test_replay_scripts.cpp
    test_scene.cpp
    test_scene_file.cpp
    test_settings.cpp
    test_simulation.cpp
    test_six_room.cpp
    test_temporal_checks.cpp
    test_world.cpp
)
target_include_directories(lc_cpu_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(lc_cpu_tests PRIVATE LC_REPLAY_DIR="${CMAKE_SOURCE_DIR}/tests/replay")
# The scene loader in lc_scene reads LC_ASSET_ROOT when no override is set; the tests point it at the source tree.
target_compile_definitions(lc_scene PRIVATE LC_ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets")
target_link_libraries(lc_cpu_tests PRIVATE lc_core lc_scene lc_game lc_audio lc_app_options lc_render_contracts lc_options)

add_test(NAME cpu_tests COMMAND lc_cpu_tests)
set_tests_properties(cpu_tests PROPERTIES LABELS cpu)
