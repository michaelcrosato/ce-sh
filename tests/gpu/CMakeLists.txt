# GPU tests run the real executable on the real adapter. Exit code 3 (no supported hardware) is
# reported by CTest as "skipped", never as passed (spec §22: a missing RTX environment is NOT RUN).
set(_lc_gpu_tests gpu_list_adapters gpu_validate_rt_triangle gpu_validate_rt_boxes gpu_validate_rt_boxes_facing gpu_resize_test)

add_test(NAME gpu_list_adapters COMMAND LastCircuit --list-adapters)
add_test(NAME gpu_validate_rt_triangle COMMAND LastCircuit --scene rt_triangle --validate --headless --frames 2)
add_test(NAME gpu_validate_rt_boxes COMMAND LastCircuit --scene rt_boxes --validate --headless --frames 2 --view ids)
add_test(NAME gpu_validate_rt_boxes_facing COMMAND LastCircuit --scene rt_boxes --validate --headless --frames 1 --view facing)
add_test(NAME gpu_resize_test COMMAND LastCircuit --scene rt_boxes --resize-test --vsync off)

set_tests_properties(${_lc_gpu_tests} PROPERTIES SKIP_RETURN_CODE 3 LABELS gpu TIMEOUT 120)
set_tests_properties(gpu_resize_test PROPERTIES LABELS "gpu;window" RUN_SERIAL TRUE)
