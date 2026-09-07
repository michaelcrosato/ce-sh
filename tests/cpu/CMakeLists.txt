# CPU tests: portable checks for math, parsing, image encoding, scene data, and layouts.
add_executable(lc_cpu_tests
    lc_test.h
    lc_test_main.cpp
    test_log.cpp
)
target_include_directories(lc_cpu_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lc_cpu_tests PRIVATE lc_core lc_options)

add_test(NAME cpu_tests COMMAND lc_cpu_tests)
set_tests_properties(cpu_tests PROPERTIES LABELS cpu)
