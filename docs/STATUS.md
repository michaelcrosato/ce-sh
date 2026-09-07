# Status

Evidence labels follow spec §22: WRITTEN, COMPILED, CPU TESTED, GPU EXECUTED, IMAGE CHECKED,
PERFORMANCE CHECKED, PASSED, FAILED, NOT RUN.

```text
Milestone:            M0 PASSED, M1 PASSED, M2 PASSED, M3 PASSED,
                      M4 PASSED for the native-resolution denoised path (NRD REBLUR with Primary Surface Replacement
                      for the mirror; T12 temporal tests pass; the moving sequences and the performance results are
                      recorded below). DLSS Super Resolution through Streamline: NOT evaluated (deferred, KI-021).
                      M3.5 PASSED: the two-room level is a versioned JSON scene file (assets/scenes/two_room.json,
                      schema 1) with complete validation, limits, and asset-root containment; reload at a frame
                      boundary that never replaces a working scene with a broken one; content hashes in captures and
                      reports; the diagnostic panel is deferred to M5 (KI-017).
                      M5 in progress: collision (capsule against the kit boxes, lamp sweep, door that never traps
                      the player) and the placeholder player body are written and tested on the CPU; the rest of
                      M5 (prompts and menus, sound, threat states, objective, restart) follows the M5 plan.
Build or commit:      branch m0-m1-bootstrap; see git log for the exact commit.
                      Presets windows-debug and windows-release both configured, built, and tested (46 tests each).
Environment:          Windows 11 Home 10.0.26200.9278 (25H2); Intel Core i7-14700F, 31.8 GiB RAM;
                      Visual Studio Community 2026 18.9.12120.119, MSVC 14.51.36231 (cl 19.51.36256);
                      CMake 4.3.1-msvc1 (VS-bundled); Windows SDK 10.0.26100.0; DXC 1.8.2502.11 + dxil.dll;
                      NRD v4.17.3, ShaderMake 18f5a344, MathLib v11 as git submodules (docs/DEPENDENCIES.md).
Implemented in this session:
                      M0/M1: CMake presets, build-time DXC, lc_core, lc_scene, Win32 window, D3D12 device/queue/swap
                      chain/resources/timestamps/BLAS/TLAS, diagnostics, layout probe, captures, resize test, docs.
                      M2: materials (Diffuse, Mirror, Emitter, RoughConductor/GGX), emitter tables, room kit, closed-form
                      references, path_trace.hlsl (NEE + BSDF + MIS, delta mirrors, 4-hit limit, triangle-scale
                      offsets, hash RNG, invalid-value counters), raw and reference modes, --stats, comparison script.
                      M3: strict JSON reader, fixed-step simulation, replays with checks, Raw Input, World (player,
                      door, lamp fixture, threat), the two-room level, --play/--record/--replay/--stop-at-tick,
                      TLAS-rebuild accounting.
                      M4: NRD built from pinned submodules with the SDK DXC (no downloads); NrdDenoiser (D3D12 backend
                      for NRD's API: root signature, 14 pipelines, 21 pool textures, per-frame descriptor slices,
                      barriers, constants); the guided path-tracer variant (PSR through mirror chains, guides:
                      normal+roughness+material id, view Z, world motion; diffuse/specular split with material
                      demodulation; direct/indirect hit-distance mixing; deterministic emission kept aside); compose
                      pass (modulation, emission, exposure, raw-sample accumulation invariant, 11 diagnostic overlays,
                      NRD validation overlay); history handling (first frame, resize, scene, mode, camera cut,
                      blackout hook, optional source-change reset); scaled presentation (--internal, bilinear);
                      per-frame readback with cropped frame sequences and event logs; replay checks motion /
                      static_motion / trail_lag / denoised patches; the benchmark command with the §21 report
                      (build and content hashes, device, sizes, settings, CPU/GPU distributions, memory);
                      typed UAV load/store capability checks for every new format.
Checks actually run:  COMPILED: Debug and Release, /W4 /WX clean; 6 shaders (cs_6_5, HLSL 2021, -WX -Zpr) plus NRD's
                      31 DXIL blobs through ShaderMake with the same DXC.
                      CPU TESTED: lc_cpu_tests 73/73 PASSED (both presets): + NRD matrix conversion and jitter
                      sequence, trail-lag metric, benchmark statistics and report, new option rules.
                      GPU EXECUTED + PASSED (both presets), 43 GPU tests (ctest 44/44):
                        M1-M3 as before (33), all still passing with the reworked renderer (raw and reference paths
                          unchanged; the layout probe now checks 35 fields including GuideConstants).
                        M4: split lossless on the closed form (raw mean -0.24 % / +0.21 %; denoised -3.6 % / -4.0 %
                          within the 5 % tolerance chosen beforehand); t08_box, mirror_box, metals_room denoised;
                          blackout reset (max denoised radiance 0 two frames after the reset; raw mean 0);
                          carried-lamp guide motion = rendered translation, static wall exactly 0 while the camera
                          moves; mirrored threat: identity through the mirror, not directly visible, guide motion =
                          reflected translation (error 6.5e-5 m), lit in the denoised image; trail lag 0 frames
                          (limit 6) with the denoised luminance following the patch occupancy frame by frame;
                          resize test native and 960x540 internal; benchmark smoke report.
                        D3D12 debug layer: 0 errors, 0 warnings in every Debug run; invalid-value counters 0.
GPU and driver used:  NVIDIA GeForce RTX 4070 SUPER, vendor 0x10DE device 0x2783 rev 161, 11997 MiB,
                      driver 32.0.16.1047 (NVIDIA 610.47, 2026-05-18); DXR Tier 1.2, SM 6.8, root signature 1.1.
Image evidence:       IMAGE CHECKED (artifacts/m4, regenerate with docs/BUILD.md and docs/TESTS.md; not committed).
                      t08_box denoised after 16 frames: smooth walls and soft contact shadows, no visible noise.
                      two_room tick 740 denoised (exposure 6): the dark inspection room with the mirror showing the
                      warm-lit hall and the threat silhouette, clean shading, no residual noise; the history overlay
                      shows full history on static surfaces and rejection only at the moving threat's edges and depth
                      discontinuities; NRD's validation layer shows reflected normals and depth in the mirror and
                      non-zero motion only on the mirrored threat. Tick 800 denoised: the shelf lamp's floor gradient
                      without the reference render's fireflies. Sequences: the mirror crop over frames 735-769 shows
                      the silhouette crossing and leaving with crisp edges and no ghost (event log beside it); the
                      door edge and the carried lamp sequences likewise.
Performance evidence: PERFORMANCE CHECKED, instrumentation and the §17 protocol on the two-room proof (not the full
                      encounter): see the benchmark table below. Frame-time distributions come from --benchmark-seconds
                      reports (Release, vsync off, windowed 1920x1080 output, 1280x720 internal, denoised mode).
Failed checks:        None outstanding. During M4: the first denoised analytic run was 7-9 % dark (NRD's
                      pre-accumulation blur and a miss-dominated hit distance); fixed by switching the pre-pass off and
                      mixing the light-sample distance into the diffuse hit distance (D-034), not by widening the
                      tolerance. A static-motion check point was off screen (moved to a pixel). The scaled resize test
                      first judged the internal size instead of the presented size.
Checks not run and reasons:
                      T11 offset sweeps, T13 collision (M5), T14-T18 (later; T16 needs the 180-second full-encounter
                      replay of M5/M6, the command exists). DLSS/Streamline: NOT RUN (deferred). History confidence
                      inputs: not provided (KI-019). Second RTX device: only one GPU present. 30-minute reliability
                      loop, live-object report under a debugger: NOT RUN. T12 review by a person at normal playback
                      speed: NOT RUN (the automated lag metric, the frame sequences, and the overlays are the evidence).
Changed assumptions:  NRD's license is NVIDIA's proprietary RTX SDK license (attribution required; not open source):
                      recorded for the owner's review. The denoiser's spatial filter biases sharp lighting gradients
                      by a few percent (KI-018); the raw mean stays exact. Mirror planes are static for the PSR
                      motion (KI-020).
Next concrete task:   M5 Task 3 of docs/superpowers/plans/2026-09-06-m5-playable-proof.md (Dear ImGui prompts, pause
                      menu, settings, diagnostic panel), then Tasks 4 (sound), 5 (threat states, objective, catch and
                      checkpoint restart), and 6 (records; T13-T15 replays; the human play-through). Tasks 1 and 2
                      (collision, body) are implemented and unit-tested; their GPU replay tests run with Task 6.
```

## Benchmark table (M4, two-room proof)

Filled from `artifacts/m4/benchmark_*.json` (Release, `--vsync off`, windowed 1920x1080 with
`--internal 1280x720`, denoised mode, 4 hits, 30-frame history, 5 s warm-up excluded, three
60-second runs of the mirror replay and one each of the door and lamp replays; the replay loops
with a world reset at its end). CPU is the whole frame on the render thread (simulation, record,
submit, present), GPU is the `frame_gpu` timestamp pair.

| Replay | Run | Frames (60 s) | GPU avg / p95 / p99 / max (ms) | CPU avg / p95 / p99 / max (ms) | GPU > 33.3 ms | Note |
|---|---|---|---|---|---|---|
| t12_mirror_motion (camera walk, door, mirror, moving threat) | 1 | 28866 | 2.05 / 2.85 / 3.16 / 3.49 | 2.08 / 2.94 / 3.24 / 3.63 | 0 | 33 replay loops |
| t12_mirror_motion | 2 | 29624 | 2.00 / 2.72 / 2.88 / 3.38 | 2.02 / 2.78 / 2.94 / 4.49 | 0 | 34 loops |
| t12_mirror_motion | 3 | 29561 | 2.00 / 2.73 / 2.88 / 3.54 | 2.03 / 2.79 / 2.94 / 4.39 | 0 | 34 loops |
| t06_door_light (door motion, source change through the doorway) | 1 | 28577 | 2.07 / 2.78 / 2.97 / 3.59 | 2.10 / 2.85 / 3.01 / 4.58 | 0 | 73 loops |
| t12_lamp_motion (carried lamp, lamp switch, shelf placement) | 1 | 29050 | 2.04 / 2.75 / 2.91 / 3.54 | 2.06 / 2.81 / 2.95 / 4.15 | 0 | 39 loops |
| t12_mirror_motion, raw mode (no denoiser) | 1 | 37243 (30 s) | 0.78 / 1.41 / 1.64 / 2.02 | 0.80 / 1.41 / 1.62 / 2.22 | 0 | denoising costs ~1.2 ms per frame |
| t12_mirror_motion, native 1920x1080 internal | 1 | 6667 (30 s) | 4.47 / 5.04 / 5.26 / 5.99 | 4.50 / 5.09 / 5.33 / 6.82 | 0 | 2.25x the pixels, 2.2x the time |

Reading: the two-room proof, reconstructed, uses about 2 ms of the 16.67 ms frame period on this
GPU (spec §17 planning allocation 13 ms); the 95th and 99th percentiles are within 3.3 ms. The
proof is far smaller than the six-room encounter, so these are not the product's numbers: the
§17 pass criterion (p95 <= 16.67 ms, p99 <= 22 ms) is checked again with the M5/M6 content on the
180-second replay. Process working set about 180 MB, video memory in use about 300 MB
(`artifacts/m4/benchmark_release_*.json`). The Debug build with the debug layer on measures the
same GPU time (its `-Od` shaders do not dominate a RayQuery-bound frame). An earlier set of
mirror-replay runs was invalid: the benchmark loop then still executed the trail-lag test's
per-frame readback (60 ms CPU frames); the loop now never reads back (recorded, not hidden).
