# GPU tests run the real executable on the real adapter. Exit code 3 (no supported hardware) is
# reported by CTest as "skipped", never as passed (spec §22: a missing RTX environment is NOT RUN).

# M1: geometry, identifiers, layouts, resize.
add_test(NAME gpu_list_adapters COMMAND LastCircuit --list-adapters)
add_test(NAME gpu_validate_rt_triangle COMMAND LastCircuit --scene rt_triangle --mode diag --validate --headless --frames 2)
add_test(NAME gpu_validate_rt_boxes COMMAND LastCircuit --scene rt_boxes --mode diag --validate --headless --frames 2 --view ids)
add_test(NAME gpu_validate_rt_boxes_facing COMMAND LastCircuit --scene rt_boxes --mode diag --validate --headless --frames 1 --view facing)
add_test(NAME gpu_resize_test COMMAND LastCircuit --scene rt_boxes --mode raw --resize-test --vsync off)

# M2: raw light transport (spec §19). Tolerances live in the scene descriptions (src/scene/builtin_scenes.cpp).
add_test(NAME gpu_t03_no_sources COMMAND LastCircuit --scene t03_dark_room --mode reference --spp 32 --validate --headless)
add_test(NAME gpu_t04_sealed_room COMMAND LastCircuit --scene t04_sealed --mode reference --spp 32 --validate --headless)
add_test(NAME gpu_t04_open_door COMMAND LastCircuit --scene t04_open --mode reference --spp 64 --validate --headless)
add_test(NAME gpu_t08_analytic_mis COMMAND LastCircuit --scene t09_rect_light --mode reference --spp 256 --strategy mis --validate --headless)
add_test(NAME gpu_t08_analytic_light COMMAND LastCircuit --scene t09_rect_light --mode reference --spp 256 --strategy light --validate --headless)
add_test(NAME gpu_t08_analytic_bsdf COMMAND LastCircuit --scene t09_rect_light --mode reference --spp 4096 --strategy bsdf --validate --headless --samples-per-frame 16)
add_test(NAME gpu_t09_area_large COMMAND LastCircuit --scene t09_rect_light_large --mode reference --spp 256 --validate --headless)
add_test(NAME gpu_t07_indirect_colour COMMAND LastCircuit --scene t07_bleed --mode reference --spp 256 --validate --headless)
add_test(NAME gpu_t08_box_positive COMMAND LastCircuit --scene t08_box --mode reference --spp 64 --validate --headless)
add_test(NAME gpu_mirror_identity_and_energy COMMAND LastCircuit --scene mirror_box --mode reference --spp 64 --validate --headless)

# Cross-run comparisons (patch means within max(relative tolerance, 3 standard errors)).
# Same path family for every estimator: with N hits the NEE-based estimators (mis, light) also
# capture direct light at the N-th surface, which BSDF-only can reach only as an (N+1)-th hit.
set(_lc_compare "${CMAKE_SOURCE_DIR}/tests/scripts/compare_runs.ps1")
set(_lc_compare_out "${CMAKE_BINARY_DIR}/artifacts/compare")
add_test(NAME gpu_t08_box_strategies_agree
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${_lc_compare}" -Exe "$<TARGET_FILE:LastCircuit>" -Scene t08_box
            -OutDir "${_lc_compare_out}" -Runs "mis:512:4,light:1024:4,bsdf:4096:5" -RelTol 0.03)
add_test(NAME gpu_t07_agrees_with_higher_sample_reference
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${_lc_compare}" -Exe "$<TARGET_FILE:LastCircuit>" -Scene t07_bleed
            -OutDir "${_lc_compare_out}" -Runs "mis:2048,mis:256" -RelTol 0.02)
add_test(NAME gpu_t08_box_depth_truncation_report
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${_lc_compare}" -Exe "$<TARGET_FILE:LastCircuit>" -Scene t08_box
            -OutDir "${_lc_compare_out}" -Runs "mis:512:12,mis:512:4" -RelTol 0.02 -Informational)

set(_lc_gpu_tests
    gpu_list_adapters gpu_validate_rt_triangle gpu_validate_rt_boxes gpu_validate_rt_boxes_facing gpu_resize_test
    gpu_t03_no_sources gpu_t04_sealed_room gpu_t04_open_door gpu_t08_analytic_mis gpu_t08_analytic_light gpu_t08_analytic_bsdf
    gpu_t09_area_large gpu_t07_indirect_colour gpu_t08_box_positive gpu_mirror_identity_and_energy
    gpu_t08_box_strategies_agree gpu_t07_agrees_with_higher_sample_reference gpu_t08_box_depth_truncation_report)
set_tests_properties(${_lc_gpu_tests} PROPERTIES SKIP_RETURN_CODE 3 LABELS gpu TIMEOUT 900)
set_tests_properties(gpu_resize_test PROPERTIES LABELS "gpu;window" RUN_SERIAL TRUE)
