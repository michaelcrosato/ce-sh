# Status

Evidence labels follow spec §22: WRITTEN, COMPILED, CPU TESTED, GPU EXECUTED, IMAGE CHECKED,
PERFORMANCE CHECKED, PASSED, FAILED, NOT RUN.

```text
Milestone:            M0 PASSED, M1 PASSED, M2 PASSED for the implemented features (diffuse, mirror,
                      emitter; T03, T04, T07, T08, T09, mirror identity and energy). Rough conductor
                      (T10) and the moving scene (M3: T05/T06 motion, door and lamp interaction) not started.
Build or commit:      branch m0-m1-bootstrap (M0-M2 work); see git log for the exact commit.
                      Presets windows-debug and windows-release both configured, built, and tested (19/19 each).
Environment:          Windows 11 Home 10.0.26200.9278 (25H2); Intel Core i7-14700F, 31.8 GiB RAM;
                      Visual Studio Community 2026 18.9.12120.119, MSVC 14.51.36231 (cl 19.51.36256);
                      CMake 4.3.1-msvc1 (VS-bundled); Windows SDK 10.0.26100.0; DXC 1.8.2502.11 + dxil.dll.
Implemented in this session:
                      M0/M1: CMake presets, build-time DXC, lc_core (log, errors, CLI, math contract, PNG/PFM/JSON
                      writers), lc_scene, Win32 window, D3D12 device/queue/swap chain/resources/timestamps/
                      BLAS/TLAS, camera_view.hlsl diagnostics, layout probe, hit-id and facing validation,
                      captures, resize test, environment report, eight docs.
                      M2: materials (Diffuse, Mirror, Emitter) with validation; emitter tables derived from the
                      visible triangles (areas, power-based selection pdf, per-triangle CDF); room kit (0.15 m
                      slabs, doorway frame, hinged 0.04 m door, rectangle emitters); closed-form rectangle
                      irradiance; path_trace.hlsl (NEE + cosine BSDF sampling + power-heuristic MIS in solid
                      angle, one-sided emission counted once, delta mirrors, 4-hit limit with NEE at the last
                      vertex, RTG ray offsets, hash RNG with a dimension table, invalid-value counters);
                      raw and progressive-reference modes with accumulation sums/squares and reset keys;
                      radiance expectations and patch statistics in --validate; --stats; cross-run
                      comparison script; scenes t03_dark_room, t04_sealed, t04_open, t07_bleed, t08_box,
                      t09_rect_light(_large), mirror_box. Review fixes: UAV ordering between frames,
                      non-throwing shutdown drain, TLAS headroom, grazing exclusion, transform checks.
Checks actually run:  COMPILED: Debug and Release, /W4 /WX clean; 3 shaders (cs_6_5, HLSL 2021, -WX -Zpr).
                      CPU TESTED: lc_cpu_tests 50/50 PASSED (both presets).
                      GPU EXECUTED + PASSED (both presets), 18 GPU tests:
                        M1: list adapters, rt_triangle, rt_boxes (ids, facing), resize test.
                        T03 no sources: max |radiance| <= 1e-6 over 921,600 pixels (32 spp).
                        T04 sealed: <= 1e-6; T04 open door: floor patch lit through the real opening.
                        T08 analytic (open floor, 1 x 0.5 m emitter): mis 256 spp, light 256 spp, bsdf 4096 spp
                          all within 2 % / 3 SE of the closed-form irradiance at the centre and 3 % beside it.
                        T09 area x4 emitter: same analytic agreement with the larger closed form.
                        T07 indirect colour: red-wall floor patch R/(G+B) exceeds the white-wall patch by > 1.2x;
                          256 spp agrees with a 2048 spp reference within 2 % / 3 SE on all three patches.
                        T08 box: mis (4 hits) vs light (4 hits) vs bsdf (5 hits, same path family) agree on
                          4 patches x 3 channels within 1 % (tolerance 3 % / 3 SE).
                        Mirror: first non-mirror hit id through the mirror equals the box behind the camera and the
                          +Z wall; mirror centre radiance equals the wall lamp radiance 3.0 within 0.1 %.
                        Invalid-value counters (NaN, inf, negative, zero pdf): 0 in every run.
                        D3D12 debug layer: 0 errors, 0 warnings in every Debug run; GPU-based validation run once (PASSED).
                      Depth truncation report (informational): t08_box 12 hits vs 4 hits agree within 2 % / 3 SE
                        on the recorded patches.
GPU and driver used:  NVIDIA GeForce RTX 4070 SUPER, vendor 0x10DE device 0x2783 rev 161, 11997 MiB,
                      driver 32.0.16.1047 (NVIDIA 610.47, 2026-05-18); DXR Tier 1.2, SM 6.8, root signature 1.1.
Image evidence:       IMAGE CHECKED (artifacts/m1, artifacts/m2; regenerate with the commands in docs/BUILD.md,
                      artifacts/ is not committed): M1 diagnostic views as described in TESTS.md; M2 reference
                      captures of t08_box (soft shadows, red/green bleeding, no seams), mirror_box (box and lamps
                      visible only in the mirror, correct handedness), t09_rect_light (expected falloff),
                      t04_open (light only through the doorway, void above the panel black), t07_bleed; a raw
                      1-spp capture of t08_box shows the expected per-pixel noise without structure.
Performance evidence: PERFORMANCE CHECKED for instrumentation only, not a benchmark (no replay, static camera,
                      tiny scenes): Release, headless, t08_box (98 triangles), 1280x720, 4 hits, MIS:
                      path_trace 2.945 ms per frame of 4 samples = 0.74 ms per 1-spp sample; M1 camera-ray
                      pass 0.06-0.67 ms. The §17 target (16.67 ms frame at 1280x720 internal) is not tested;
                      T16 NOT RUN.
Failed checks:        None outstanding. During the session: (1) the M1 winding flag inverted facing (fixed,
                      D-004); (2) first T07 layout gave ratio 1.18 vs factor 1.2 (scene redesigned, D-020);
                      (3) bsdf-only vs mis at equal hits differed 5-9 % in the closed box until the comparison
                      used the same path family (D-017); (4) the comparison script failed under Windows
                      PowerShell because stderr logging became a terminating error (fixed).
Checks not run and reasons:
                      T05/T06 with motion (door moving, threat moving): M3. T10 rough conductor: not implemented.
                      T11 offset sweeps beyond the seal tests, T12-T18: later milestones.
                      Second RTX device: only one GPU present. Non-Windows CPU build: no other platform.
                      30-minute reliability loop and live-object report under a debugger: NOT RUN.
Changed assumptions:  DXR default facing rule (no winding flag). Estimator comparisons need matched path
                      families. In-box Windows SDK suffices (no Agility SDK). Internal resolution equals
                      window size until M4.
Next concrete task:   Add the RoughConductor material (GGX NDF, Smith height-correlated masking, VNDF
                      sampling, Schlick Fresnel with F0 = reflectance) to materials.hlsli and the path
                      tracer, with T10 checks: no invalid values across a roughness sweep and a documented
                      white-furnace-style energy test (a unit-F0 conductor sphere-less enclosure test on a
                      rough plane under a uniform emitter, recording the single-scattering energy loss).
                      Then start M3 (docs/superpowers/plans: moving two-room scene, input, deterministic
                      replay, current/previous render state).
```
