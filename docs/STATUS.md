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
                      M5 built; every automated check PASSED; the gate's own condition NOT RUN by a person.
                      Collision (capsule against the kit boxes, lamp sweep, a door that never traps the player),
                      the placeholder body visible in the mirror and clear of the camera by placement, the
                      interface (prompts, objective line, controls card, pause menu with settings, F1 diagnostic
                      panel, reload from the menu), sound (generated clips with provenance, attenuation, pan,
                      occlusion, state-following hums and events, text cues, volumes), and the game rules (the
                      machine's patrol/chase/investigate/wait/return on gameplay data, the objective phases with
                      checkpoints, the catch that restarts from the checkpoint, the menu restart, T14 rule
                      invariance through a CPU-only state hash, T15 route and catch replays) are built, tested,
                      and checked, and the image and performance gates stay passed with the M5 content (table
                      below). The M5 gate reads "a person can play the proof without console commands or a
                      developer explaining each control": no person has played it in this record, so the gate
                      is NOT RUN, not PASSED; the controls card, prompts, menu, and restart exist for that
                      session, and the replays plus the scripted window runs are the evidence until then.
Build or commit:      branch m0-m1-bootstrap; see git log for the exact commit.
                      Presets windows-debug and windows-release both configured, built, and tested (54 tests each).
Environment:          Windows 11 Home 10.0.26200.9278 (25H2); Intel Core i7-14700F, 31.8 GiB RAM;
                      Visual Studio Community 2026 18.9.12120.119, MSVC 14.51.36231 (cl 19.51.36256);
                      CMake 4.3.1-msvc1 (VS-bundled); Windows SDK 10.0.26100.0; DXC 1.8.2502.11 + dxil.dll;
                      NRD v4.17.3, ShaderMake 18f5a344, MathLib v11, Dear ImGui v1.92.9b, miniaudio 0.11.25 as
                      git submodules (docs/DEPENDENCIES.md); output device in the checks: the monitor's HDMI
                      audio through NVIDIA High Definition Audio (WASAPI, 48 kHz).
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
                      M3.5: scene files (schema 1, validation, limits, containment, reload, content hashes).
                      M5 (so far): CollisionWorld (yaw-only boxes from the kit, capsule push-out with substeps,
                      sphere sweep for the carried lamp, segment tests), the door that swings back when it meets
                      the player, the placeholder torso and hands in the scene file and the world, lc_imgui +
                      lc_ui (Dear ImGui through a window message hook and Renderer::RecordOverlay into the back
                      buffer after the present copy), the pause menu with live settings (sensitivity, inverted
                      look, field of view, exposure; volumes and text cues stored for the sound system), the
                      objective line, the controls card, the F1 panel, restart and reload from the menu, --no-ui.
                      Sound: lc_miniaudio (WASAPI only, compiled from miniaudio.c) + lc_audio: 14 generated clips
                      with provenance strings, audio::Director (hums per emitter state and transform, door creak
                      and thud, lamp click and handling, footsteps per 0.62 m, the machine's loop while it moves,
                      chime and sting, text cues), ComputeMix (inverse square clamped at 1 m, pan from the
                      listener's right axis, occlusion 0.3 through the collision solids), AudioSystem (24 voices,
                      effects/ambience groups, master and pause), --no-audio; the diagnostic panel shows the device
                      and voices. Input record kept until a tick reads it (D-042, a dropped-press bug found here).
                      Rules: Threat behaviours patrol/hunt (detection from the lamp state, distance, facing, and
                      a line of sight through the collision solids; chase, investigate, wait, return; contact =
                      catch), objective phases with checkpoints and the restart (menu and catch), the scene
                      file's exit objective, world-state replay checks (objective_state, threat_state,
                      caught_count, player_near) evaluated right after their tick, the per-tick world-state
                      hash, --simulate-only (no graphics), --expect-state-hash, --threat, the t15 route and
                      catch/restart replays, --capture-backbuffer (presented-image evidence).
Checks actually run:  COMPILED: Debug and Release, /W4 /WX clean (ImGui and miniaudio at /W3, no warnings); 6
                      shaders (cs_6_5, HLSL 2021, -WX -Zpr) plus NRD's 31 DXIL blobs through ShaderMake with the
                      same DXC.
                      CPU TESTED: lc_cpu_tests 96/96 PASSED (both presets): + scene-file golden load, rejection
                      of every rule, limits and containment (3 cases); collision (6 cases); sound rules (7 cases);
                      objective, hunt, detection, state hash, state checks, the t15 replays (7 cases).
                      GPU EXECUTED + PASSED (both presets), 51 GPU tests + 2 CPU-only replay runs (ctest 54/54):
                        M5: t13 reflection (the mirror reports the torso after one bounce, the torso is not
                          directly visible, the reflected torso is lit); every M3/M4 replay now runs against the
                          collision solids with unchanged expectations (Release t06 door-open patch 0.01963);
                          T14: the route replay in raw, denoised, exposure 0.25, and 960x540 internal-size runs
                          reproduces the CPU-only state hash bit for bit with all world-state checks passing;
                          T15: the route completes (escaped at tick 1461, never chased) and the catch replay
                          (chase at tick 156, caught at 208, restart to the checkpoint, escaped at 1670) pass on
                          the CPU alone and under the denoised renderer.
                        M3.5: scene reload test, --scene-file option.
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
Image evidence:       IMAGE CHECKED (artifacts/m4 and artifacts/m5/ui, regenerate with docs/BUILD.md and
                      docs/TESTS.md; not committed).
                      M5 interface: seven screen captures of the live window (controls card, door prompt, pause
                      menu, menu with the F1 panel, play with the panel, "Close the door" after the door opened
                      with the threat in the doorway, the start pose after Restart from the menu); the pass table
                      shows the overlay at 0.01 ms; no D3D12 debug-layer messages during the run.
                      M5 sound: two more captures with the panel's device line (4-5 voices on the HDMI output at
                      48 kHz, 0 dropped) and the cue text under the objective; the log lists the cues at the
                      events (creak at the door's state change, the click at the lamp toggle). Listening by a
                      person: NOT RUN in this record.
                      M5 rules: the catch replay in a window with the panel (chase, the panel after the restart
                      with catches 1 / restarts 1 / history resets 2, the end card's phase); headless captures of
                      the same ticks are clean in denoised and reference modes. Desktop screen captures of the
                      window show transparent holes the presented back buffer does not contain (KI-028; proven
                      with the in-app back-buffer readback: 0 pixels with alpha below 255 at frame 340).
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
                      encounter): see the benchmark tables below (M4, and M5 with the body, the hunting machine,
                      sound on, interface off). Frame-time distributions come from --benchmark-seconds reports
                      (Release, vsync off, windowed 1920x1080 output, 1280x720 internal, denoised mode). With the
                      M5 content the GPU frame averages 1.8 ms with p99 at 2.0-2.3 ms and no frame above 33.3 ms
                      in four 60-second runs; the working set grew from about 180 MB to about 300 MB (clips, the
                      audio engine, the interface); video memory in use 345 MB.
Failed checks:        None outstanding. During M5: the first body placement (torso 5 cm in front of the eye) hid
                      the T06 floor patch a metre ahead and the door-open check fell from 0.0196 to 0.0048 (below
                      its 0.005 minimum); fixed by moving the body behind the eye axis (D-039), not the threshold.
                      The mirror replay grazed the sink block once collision existed (the sink moved 20 cm). A
                      posted F press did nothing in the sound check: the input record was cleared every frame and
                      more than half the 144 Hz frames run no 60 Hz tick (D-042 fixes it). Standing at the door
                      and looking level lost the prompt (the aim point was the leaf's centre at 1.0 m): the door
                      is now aimed at eye height within the leaf; the closing-door CPU test was adjusted to look
                      level instead of down. The machine's loop restarted every tick-less frame (a 0.25 s hold
                      fixes it, tested).
                      During M4: the first denoised analytic run was 7-9 % dark (NRD's pre-accumulation blur and a
                      miss-dominated hit distance); fixed by switching the pre-pass off and mixing the light-sample
                      distance into the diffuse hit distance (D-034), not by widening the tolerance. A static-motion
                      check point was off screen (moved to a pixel). The scaled resize test first judged the
                      internal size instead of the presented size.
Checks not run and reasons:
                      T11 offset sweeps, T13's sound part (Task 4), T14-T18 (later; T16 needs the 180-second
                      full-encounter replay of M5/M6, the command exists). DLSS/Streamline: NOT RUN (deferred).
                      History confidence inputs: not provided (KI-019). Second RTX device: only one GPU present.
                      30-minute reliability loop, live-object report under a debugger: NOT RUN. T12 review by a
                      person at normal playback speed: NOT RUN (the automated lag metric, the frame sequences, and
                      the overlays are the evidence). The menu's mouse path was driven by posted messages, not a
                      person's hand. The M5 gate play-through by a person (spec §20): NOT RUN; listening to the
                      sound: NOT RUN. Both need the owner or a tester at the keyboard with headphones; the command
                      is `LastCircuit.exe --scene two_room --play --mode denoised --exposure 4`.
Changed assumptions:  NRD's license is NVIDIA's proprietary RTX SDK license (attribution required; not open source):
                      recorded for the owner's review. The denoiser's spatial filter biases sharp lighting gradients
                      by a few percent (KI-018); the raw mean stays exact. Mirror planes are static for the PSR
                      motion (KI-020).
Next concrete task:   M6, the six-room demo (spec §4 room table, §20): write the plan (layout from the shared kit
                      as a schema-1 scene file, the fuse as the objective's second carried object with its
                      circuit change, the return sequence in the dark, the exit onto a lit space, waypoint routing
                      for the machine, the 180-second full-encounter replay for T16), then build it room by
                      room with the same evidence discipline. The M5 gate's play-through waits for a person.
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

## Benchmark table (M5, playable proof content)

Filled from `artifacts/m5/benchmark_release_*.json` (`tests/scripts/bench_m5.ps1`: Release at
commit d6596bb, `--vsync off`, windowed 1920x1080 with `--internal 1280x720`, denoised mode, 4
hits, 30-frame history, 5 s warm-up excluded, 60 s measured, sound on through the HDMI output,
no interface, the replay looped with a world reset). The route and catch replays run the machine's
hunt behaviour, the placeholder body, the collision solids, and the objective; the mirror replay is
the M4 comparison run.

| Replay | Frames (60 s) | Loops | GPU avg / p95 / p99 / max (ms) | CPU avg / p95 / p99 / max (ms) | GPU > 33.3 ms | Working set / VRAM |
|---|---|---|---|---|---|---|
| t15_route (hunt), run 1 | 32162 | 23 | 1.84 / 2.11 / 2.30 / 2.73 | 1.86 / 2.14 / 2.39 / 3.55 | 0 | 309 MB / 345 MB |
| t15_route (hunt), run 2 | 32397 | 23 | 1.83 / 1.96 / 2.00 / 2.59 | 1.85 / 2.02 / 2.15 / 3.03 | 0 | 293 MB / 345 MB |
| t15_catch_restart (hunt, a catch and a checkpoint restart per loop) | 32283 | 20 | 1.83 / 1.96 / 2.01 / 2.55 | 1.86 / 2.04 / 2.18 / 2.69 | 0 | 312 MB / 345 MB |
| t12_mirror_motion (patrol; the M4 comparison) | 33108 | 38 | 1.79 / 1.86 / 1.91 / 2.45 | 1.81 / 1.97 / 2.13 / 2.71 | 0 | 296 MB / 345 MB |

GPU pass averages over the runs: path_trace 0.62-0.64 ms, denoise 0.83-0.84 ms, compose 0.24-0.25 ms,
upscale 0.03 ms, scene_update 0.06 ms, copy_out 0.02 ms; denoiser pool 66 MB. The mirror replay
measures 1.79 ms against 2.00-2.05 ms in the M4 table for the same replay; the cause was not
isolated (same GPU and driver; the M4 runs predate the scene-file loader, the collision solids, and
the body) and the difference is inside the run-to-run spread already seen in M4.

Reading: the two-room proof, reconstructed, uses about 2 ms of the 16.67 ms frame period on this
GPU (spec §17 planning allocation 13 ms); the 95th and 99th percentiles are within 3.3 ms. The
proof is far smaller than the six-room encounter, so these are not the product's numbers: the
§17 pass criterion (p95 <= 16.67 ms, p99 <= 22 ms) is checked again with the M6 content on the
180-second replay. M4 process working set about 180 MB, video memory in use about 300 MB
(`artifacts/m4/benchmark_release_*.json`). The Debug build with the debug layer on measures the
same GPU time (its `-Od` shaders do not dominate a RayQuery-bound frame). An earlier set of
mirror-replay runs was invalid: the benchmark loop then still executed the trail-lag test's
per-frame readback (60 ms CPU frames); the loop now never reads back (recorded, not hidden).
