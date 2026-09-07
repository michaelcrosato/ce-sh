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
add_test(NAME gpu_t10_furnace_r05 COMMAND LastCircuit --scene t10_furnace_r05 --mode reference --spp 512 --validate --headless)
add_test(NAME gpu_t10_furnace_r35 COMMAND LastCircuit --scene t10_furnace_r35 --mode reference --spp 512 --validate --headless)
add_test(NAME gpu_t10_furnace_r70 COMMAND LastCircuit --scene t10_furnace_r70 --mode reference --spp 512 --validate --headless)
add_test(NAME gpu_t10_furnace_closed_form COMMAND LastCircuit --scene t10_furnace_r100_normal --mode reference --spp 1024 --validate --headless --samples-per-frame 16)
add_test(NAME gpu_metals_room_positive COMMAND LastCircuit --scene metals_room --mode reference --spp 64 --validate --headless)

# M3: motion through deterministic replays (spec §4, §19 T05/T06). Each check runs in its own process
# stopped at its tick; reference mode for radiance checks, raw mode for identity checks in motion.
set(_lc_replay "${CMAKE_SOURCE_DIR}/tests/replay")
add_test(NAME gpu_two_room_static_mirror COMMAND LastCircuit --scene two_room --mode reference --spp 32 --validate --headless)
add_test(NAME gpu_t05_mirror_threat_reference COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t05_mirror_threat.json" --stop-at-tick 740 --mode reference --spp 64 --validate --headless)
add_test(NAME gpu_t05_mirror_threat_raw COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t05_mirror_threat.json" --stop-at-tick 740 --mode raw --validate --headless)
add_test(NAME gpu_t05_mirror_threat_absent COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t05_mirror_threat.json" --stop-at-tick 930 --mode reference --spp 16 --validate --headless)
add_test(NAME gpu_t06_door_open COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t06_door_light.json" --stop-at-tick 300 --mode reference --spp 64 --validate --headless)
add_test(NAME gpu_t06_door_closed COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t06_door_light.json" --stop-at-tick 420 --mode reference --spp 64 --validate --headless)
add_test(NAME gpu_t06_lamp_on_shelf COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t06_lamp_shelf.json" --stop-at-tick 800 --mode reference --spp 128 --validate --headless)
add_test(NAME gpu_t06_lamp_off COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t06_lamp_shelf.json" --stop-at-tick 900 --mode reference --spp 128 --validate --headless)
# Motion frame by frame: one tick per frame in raw mode; TLAS rebuilds must match the frames with motion.
add_test(NAME gpu_replay_motion_tlas_rebuilds COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t06_door_light.json" --mode raw --frames 120 --validate --headless)

# M4: stable real-time image (spec §13, §19 T12). Denoised mode: one sample per frame, NRD REBLUR, compose.
# The recomposed raw mean must meet the reference tolerances (the split is lossless); the denoised image
# has its own documented tolerance (docs/TESTS.md).
add_test(NAME gpu_denoised_split_lossless COMMAND LastCircuit --scene t09_rect_light --mode denoised --frames 64 --validate --headless)
add_test(NAME gpu_denoised_box_static COMMAND LastCircuit --scene t08_box --mode denoised --frames 120 --validate --headless)
add_test(NAME gpu_denoised_mirror_box COMMAND LastCircuit --scene mirror_box --mode denoised --frames 64 --validate --headless)
add_test(NAME gpu_denoised_metals_room COMMAND LastCircuit --scene metals_room --mode denoised --frames 64 --validate --headless)
add_test(NAME gpu_denoised_blackout_reset COMMAND LastCircuit --scene t08_box --mode denoised --blackout-at-frame 60 --frames 62 --validate --headless)
add_test(NAME gpu_denoised_motion_lamp COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t12_lamp_motion.json" --mode denoised --frames 400 --validate --headless)
add_test(NAME gpu_denoised_motion_mirror_threat COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t12_mirror_motion.json" --mode denoised --frames 740 --validate --headless)
add_test(NAME gpu_denoised_trail_lag COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t12_mirror_motion.json" --mode denoised --frames 930 --validate --headless)
add_test(NAME gpu_denoised_resize_test COMMAND LastCircuit --scene rt_boxes --mode denoised --resize-test --vsync off)
add_test(NAME gpu_denoised_scaled_resize_test COMMAND LastCircuit --scene rt_boxes --mode denoised --internal 960x540 --resize-test --vsync off)
# M3.5: scene files (spec §16). The two_room tests above run on assets/scenes/two_room.json; the reload
# test swaps the scene at a frame boundary after parse + validate and expects the history to reset.
add_test(NAME gpu_scene_reload_test COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t06_door_light.json" --mode denoised --reload-test --frames 6 --validate --headless)
add_test(NAME gpu_scene_file_option COMMAND LastCircuit --scene-file scenes/two_room.json --mode reference --spp 8 --validate --headless)
# M5: T13 part 1: the placeholder body appears in the mirror while it is below the direct view.
add_test(NAME gpu_t13_player_in_reflection COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t13_reflection.json" --stop-at-tick 760 --mode reference --spp 64 --validate --headless)
# Benchmark command (spec §21): a short headless run must produce a complete report.
add_test(NAME gpu_benchmark_smoke COMMAND LastCircuit --scene two_room --replay "${_lc_replay}/t12_mirror_motion.json" --mode denoised
         --benchmark-seconds 2 --warmup-seconds 0.5 --report "${CMAKE_BINARY_DIR}/artifacts/benchmark_smoke.json" --headless)

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
    gpu_t10_furnace_r05 gpu_t10_furnace_r35 gpu_t10_furnace_r70 gpu_t10_furnace_closed_form gpu_metals_room_positive
    gpu_two_room_static_mirror gpu_t05_mirror_threat_reference gpu_t05_mirror_threat_raw gpu_t05_mirror_threat_absent
    gpu_t06_door_open gpu_t06_door_closed gpu_t06_lamp_on_shelf gpu_t06_lamp_off gpu_replay_motion_tlas_rebuilds
    gpu_denoised_split_lossless gpu_denoised_box_static gpu_denoised_mirror_box gpu_denoised_metals_room gpu_denoised_blackout_reset
    gpu_denoised_motion_lamp gpu_denoised_motion_mirror_threat gpu_denoised_trail_lag gpu_denoised_resize_test gpu_denoised_scaled_resize_test
    gpu_benchmark_smoke gpu_scene_reload_test gpu_scene_file_option gpu_t13_player_in_reflection
    gpu_t08_box_strategies_agree gpu_t07_agrees_with_higher_sample_reference gpu_t08_box_depth_truncation_report)
set_tests_properties(${_lc_gpu_tests} PROPERTIES SKIP_RETURN_CODE 3 LABELS gpu TIMEOUT 900)
set_tests_properties(gpu_resize_test gpu_denoised_resize_test gpu_denoised_scaled_resize_test PROPERTIES LABELS "gpu;window" RUN_SERIAL TRUE)
