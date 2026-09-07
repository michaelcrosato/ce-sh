# Status

Evidence labels follow spec §22: WRITTEN, COMPILED, CPU TESTED, GPU EXECUTED, IMAGE CHECKED,
PERFORMANCE CHECKED, PASSED, FAILED, NOT RUN.

```text
Milestone:            M0 PASSED, M1 PASSED, M2 PASSED (T03, T04, T07, T08, T09, T10, mirror identity and energy),
                      M3 PASSED (moving two-room scene: T05 and T06 through deterministic replays; door and
                      source changes correct in raw and reference modes). M4 (stable real-time image: NRD,
                      mirror guidance, reconstruction) not started; M3.5 (JSON scene files, reload) not started.
Build or commit:      branch m0-m1-bootstrap (M0-M3 work); see git log for the exact commit.
                      Presets windows-debug and windows-release both configured, built, and tested (33/33 each).
Environment:          Windows 11 Home 10.0.26200.9278 (25H2); Intel Core i7-14700F, 31.8 GiB RAM;
                      Visual Studio Community 2026 18.9.12120.119, MSVC 14.51.36231 (cl 19.51.36256);
                      CMake 4.3.1-msvc1 (VS-bundled); Windows SDK 10.0.26100.0; DXC 1.8.2502.11 + dxil.dll.
Implemented in this session:
                      M0/M1: CMake presets, build-time DXC, lc_core (log, errors, CLI, math contract, PNG/PFM/JSON
                      writers), lc_scene, Win32 window, D3D12 device/queue/swap chain/resources/timestamps/
                      BLAS/TLAS, camera_view.hlsl diagnostics, layout probe, hit-id and facing validation,
                      captures, resize test, environment report, eight docs.
                      M2: materials (Diffuse, Mirror, Emitter, RoughConductor/GGX), emitter tables from the
                      visible triangles, room kit, closed-form rectangle irradiance and GGX directional albedo
                      as references, path_trace.hlsl (NEE + BSDF sampling + MIS, one-sided emission counted
                      once, delta mirrors, 4-hit limit, triangle-scale RTG offsets, hash RNG, invalid-value
                      counters), raw and reference modes, radiance expectations, --stats, comparison script.
                      M3: strict JSON reader; fixed-step 60 Hz simulation; replay files (run-length input
                      segments + checks); Raw Input mouse, key edges, cursor capture, pause on focus loss;
                      World (player controller, door state machine, lamp fixture held/placed/toggled on
                      sockets, threat ping-pong path, view-aligned interaction, interpolated render poses,
                      transforms written only on change); the §4 two-room level (Room A with fixture, crate,
                      lamp, door; L-shaped hall with an emergency fixture; Room B with sink block, angled
                      mirror, lamp shelf, dark circuit); --play/--record/--replay/--stop-at-tick; replay
                      checks in --validate; TLAS-rebuild accounting; three committed replays.
Checks actually run:  COMPILED: Debug and Release, /W4 /WX clean; 3 shaders (cs_6_5, HLSL 2021, -WX -Zpr).
                      CPU TESTED: lc_cpu_tests 67/67 PASSED (both presets), including CPU runs of the three
                      replays (poses at check ticks, door/lamp states, mirror identity, occlusion).
                      GPU EXECUTED + PASSED (both presets), 32 GPU tests (ctest 33/33):
                        M1: list adapters, rt_triangle, rt_boxes (ids, facing), resize test.
                        M2: T03/T04 zero and open-door tests; T08 analytic (mis/light/bsdf); T09 area; T07
                          indirect colour and higher-sample agreement; T08 box strategies (bsdf at 5 hits, same
                          path family) within 1 %; mirror identity and energy; T10 furnace at roughness
                          0.05/0.35/0.70 and the closed form 1 - ln 2; metals room; depth report.
                        M3: static two_room mirror identity; T05 at tick 740 (reference and raw): mirror pixel =
                          threat_body after 1 mirror bounce, threat not directly visible, mirrored threat lit;
                          T05 at tick 930: mirror shows the hall wall; T06 door open (hall patch lit, leaf in the
                          hall) and closed (patch dark, leaf fills the doorway); T06 lamp carried to the shelf
                          (housing and shelf identities, floor lit) and switched off (floor dark); frame-by-frame
                          replay: TLAS rebuilds == motion frames (+1).
                        D3D12 debug layer: 0 errors, 0 warnings in every Debug run; invalid-value counters 0.
                      Live play smoke (Release, windowed, 90 frames, no input): raw mouse registered, cursor
                      captured, interaction prompt logged, 0.77 ms GPU per frame, exit 0.
GPU and driver used:  NVIDIA GeForce RTX 4070 SUPER, vendor 0x10DE device 0x2783 rev 161, 11997 MiB,
                      driver 32.0.16.1047 (NVIDIA 610.47, 2026-05-18); DXR Tier 1.2, SM 6.8, root signature 1.1.
Image evidence:       IMAGE CHECKED (artifacts/m1, m2, m3; regenerate with the commands in docs/BUILD.md and
                      docs/TESTS.md; artifacts/ is not committed). M3: two_room tick 740 (exposure 6) shows the
                      dark inspection room with the mirror at the left reflecting the warm-lit hall through the
                      doorway and the threat's silhouette; tick 300 shows Room A's light through the open
                      doorway onto the hall floor with the open leaf beside it and the lamp on Room A's floor;
                      tick 420 shows the hall dark with the door closed; tick 800 shows the lamp on the shelf
                      lighting the floor of the dark inspection room; tick 400 shows the hall with the carried
                      lamp lighting the way. Rendering is consistent across ticks (no missing faces, no leaks).
Performance evidence: PERFORMANCE CHECKED for instrumentation only, not a benchmark: Release, two_room raw mode
                      at 1280x720, 4 hits, MIS, live play: path_trace 0.70 ms, scene_update 0.05 ms, copy 0.02 ms
                      per frame (0.77 ms GPU, 0.9 ms CPU). t08_box reference: 0.74 ms per 1-spp sample. No
                      replay benchmark, no denoiser, no reconstruction yet; the §17 protocol (T16) NOT RUN.
Failed checks:        None outstanding. During the session: M1 winding flag (D-004); T07 scene redesign
                      (D-020); estimator path families (D-017); PowerShell stderr handling in the comparison
                      script; furnace self-hits on 400 m triangles (D-022); an under-resolved CPU quadrature
                      (reparameterized); the static two_room lit-threat threshold was a default, replaced by the
                      derived value 1e-4; the first two-room layout had the threat walking along the mirror's
                      sight line (redesigned to cross it) and the camera facing away from the mirror.
Checks not run and reasons:
                      T11 offset sweeps, T12 temporal (needs M4), T13 collision (M5), T14-T18 (later).
                      Second RTX device: only one GPU present. Non-Windows CPU build: no other platform.
                      30-minute reliability loop, live-object report under a debugger: NOT RUN.
                      Human play-through of the proof with a person at the controls: NOT RUN (the replays and a
                      90-frame no-input smoke run are the evidence).
Changed assumptions:  DXR default facing rule (no winding flag). Estimator comparisons need matched path
                      families. Ray offsets need a triangle-scale term. Room B's fixture is off in the proof so
                      the lamp matters; the emergency fixture is a third of Room A's power for readability.
Next concrete task:   M4 step 1 (write docs/superpowers/plans/2026-09-06-m4-stable-image.md first): add the
                      guide-buffer pass (world normal, roughness, view depth, motion vectors from the previous
                      transforms and camera, diffuse/specular radiance split with hit distance) with a T02-style
                      probe test, then evaluate NRD (pinned release, license recorded) with the Primary Surface
                      Replacement approach for the planar mirror, keeping raw and reference modes untouched.
                      Interleave M3.5 (JSON scene files, reload) when the M4 buffer contract is stable.
```
