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
                      M6 built; every automated row of the test matrix that applies to the demo PASSED; the
                      rows that need a person NOT RUN. The six-room demo is one schema-2 scene file from the
                      shared kit (six rooms, two halls, a vestibule; seven doors with the exit door locked
                      until its circuit is powered; the lamp and the fuse; seven circuits, the hall circuit
                      powered by the fuse in its box and the exit circuit by the fuse in the exit panel; ten
                      fixtures with eight active; a ceiling fan under a real fixture; the machine's route
                      through both halls). Circuits follow item placement every tick and drive emission,
                      sampling, the fan, its sound, and the exit door from one state. The encounter replay
                      (99 s) and its caught-and-restarted variant (118 s) pass their world-state checks on the
                      CPU alone and reproduce the hash under four renderings (T14) and a rendered catch (T15);
                      fifteen image checks frozen at their ticks pass in reference mode (the mirror shows the
                      machine's head behind the open door, the running fan's blade at the integrated angle and
                      the gap a quarter second later, the stopped fan at its rest angle, the fused fixture and
                      floors dark, the emergency wall lit, the exit door closed then open, the vestibule lit);
                      the §17 protocol on the demo (three 180-second runs plus the caught variant) is inside
                      the targets with a large margin (table below). The M6 gate reads "the complete encounter
                      meets the test matrix": every automated row does; T12's review by a person, the
                      play-through, and listening are NOT RUN, so the gate as a whole is NOT RUN.
                      M7 built; the package exists and its automated evidence PASSED; the gate NOT RUN. The
                      package (dist\LastCircuit-0.1.0-e336f1d3f62d-win64, built from the T17 commit; 4.7 MB
                      in 17 files; the zip 2.5 MB, SHA-256 6c1a5c5fa29d90a9ab3724da4fb18b048b4a16f5cdf663
                      dedc40dd1281fd1896; the packaged build's benchmark below was run from the previous
                      package, 9d04601c6110, whose code differs only by the memory line and the end card's
                      text) is the executable with the static CRT (no redistributable; its imports
                      are the in-box ADVAPI32, D3DCOMPILER_47, USER32, d3d12, dxgi, KERNEL32, GDI32, SHELL32,
                      IMM32), the shaders and assets, the two demo replays, the generated NOTICES.txt with the
                      NVIDIA attribution and every shipped license verbatim, README.txt, KNOWN_ISSUES.md, the
                      smoke-test and benchmark commands, and a manifest with per-file SHA-256. A double-click
                      starts the demo in play; settings and the seven rebindable actions persist in the user's
                      data directory; startup failures explain themselves in a message box with the log path;
                      the pause menu states the brightness range, the rendering path and what this build does
                      not offer, the build id, and the notices. The M7 gate reads "a clean-machine test
                      passes; the owner has actual gameplay capture, measured performance, and an accurate
                      known-issues list": no clean machine was available (T18 NOT RUN, KI-036; the substitutes
                      are the import check and the package's smoke test run from outside the build tree under
                      CTest), the gameplay capture is the fixed encounter replay as presented with the
                      interface (twelve frames, not a person's play), the performance is measured (the M6
                      protocol runs and the package's own benchmark command), and the known-issues list is
                      current. The gate as a whole is therefore NOT RUN.
Build or commit:      branch m0-m1-bootstrap; see git log for the exact commit.
                      Presets windows-debug and windows-release both configured, built, and tested (73 tests each).
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
                      M6: scene file schema 2 (lists of doors, items, sockets with accept lists, circuits
                      powered by an item in a socket, the fan object kind, objective steps; the proof migrated
                      to it), the generalised world (doors that open with their circuit, items in the hand or
                      the pocket, EvaluateCircuits every tick, fans with a 3 s spin-up and spin-down, the step
                      list driving phases and checkpoints), routing along the patrol polyline for the machine's
                      off-path moves, assets/scenes/six_room.json (the measured layout of the plan), the
                      encounter and caught replays with world-state and image checks, image checks frozen at
                      their ticks with truncated runs that report the later state checks as not evaluated,
                      pocket items, the fan loop, tests/scripts/bench_m6.ps1.
                      M7: the static CRT in both presets with C enabled (miniaudio's file had taken MSBuild's
                      default runtime: the LNK4098 warning since M5), --version, the §21 scene names
                      (last_circuit, mirror_lab), a launch without arguments as the game, the console of a
                      double-click freed with the standard streams redirected, the default play log in
                      %LOCALAPPDATA%\LastCircuit\logs, the startup message box with the log's last error,
                      game::Settings with the key bindings in a JSON file that only play reads (clamped
                      ranges, problems logged), --settings, the pause menu's Controls (press-a-key
                      rebinding, conflicts, reset), About (build id, notices), the stated brightness range and
                      rendering path, interface scale, the prompts and the controls card with the bound keys,
                      tools/package.ps1 with the import check and the zip, tools/package/{smoke_test.cmd,
                      benchmark.cmd, README.txt}, package_smoke under CTest, --capture-backbuffer as a list,
                      windowed replays drawing the interface, tools/capture_gameplay.ps1.
Checks actually run:  COMPILED: Debug and Release, /W4 /WX clean (ImGui and miniaudio at /W3, no warnings); 6
                      shaders (cs_6_5, HLSL 2021, -WX -Zpr) plus NRD's 31 DXIL blobs through ShaderMake with the
                      same DXC.
                      CPU TESTED: lc_cpu_tests 110/110 PASSED (both presets); M7: --version, the scene aliases,
                      the launch arguments, --no-dialog and --settings, the settings file (round trip,
                      clamping, defaults on a broken file, key names, bindable keys, conflicts, the input
                      mapping through the bindings), the log's last error. Earlier: + scene-file golden load, rejection
                      of every rule, limits and containment (3 cases); collision (6 cases); sound rules (7 cases);
                      objective, hunt, detection, state hash, state checks, the t15 replays (7 cases); M6: schema
                      2 goldens and rejections, the fan build, the fuse/fan/locked-door proof, pocket items and
                      their write rule, the fan loop, the six-room level (enclosure, door swings, budgets,
                      routing around the corner, the mirror through the CPU caster, sockets and markers).
                      T17 (spec §19 reliability target) GPU EXECUTED + PERFORMANCE CHECKED + PASSED for the
                      automated part (tests/scripts/reliability_m7.ps1, Release, artifacts/m7/reliability):
                        The 30-minute loop: the caught replay (a catch and a checkpoint restart, the fuse's
                          source change, seven doors, the mirror, the moving machine, the camera walk) looped
                          137 times under the benchmark in a 1920x1080 window from 1280x720, denoised, vsync
                          off, sound on: 971,041 frames in 1800 s, exit code 0, no error in the log, GPU
                          average 1.83 / p95 2.06 / p99 2.25 / max 11.20 ms, CPU p95 2.09 ms, no frame above
                          33.3 ms, 275 history resets. Memory sampled every minute: video memory 344.9 MiB and
                          the denoiser pool 65.7 MiB constant for 30 minutes; the working set 295.5 -> 307.4
                          MiB (peak 309.6), a rise of 0.4 MiB per minute that is the benchmark's own per-frame
                          sample storage (two vectors of doubles, 971,041 entries each = 14.8 MiB, growing in
                          steps) and not present outside a benchmark; the report's final working set 308 MB.
                        The resize and focus sequence: the encounter replay looped for 300 s while the window
                          was resized every five seconds through 1600x900, 800x600, 1920x1080, and 1280x720 (54
                          resizes; the log records 54 swap-chain resizes) and minimised and restored every
                          twenty seconds (13 cycles; focus lost and regained): exit code 0, no error, 156,196
                          frames, 26 loops, GPU p95 3.84 / p99 4.19 / max 9.15 ms across the sizes, no frame
                          above 33.3 ms; video memory follows the size (205 MiB at 1280x720 to 622 MiB at
                          1920x1080, the denoiser pool 34 to 147 MiB) and returns to the same value for the
                          same size (431.8 MiB at 121 s and at 301 s), working set 338-362 MiB (peak 371). The
                          live-object report under a debugger stays NOT RUN (KI-004).
                      GPU EXECUTED + PASSED (both presets), 67 GPU tests + 4 CPU-only replay runs + the package
                      smoke test (ctest 73/73; Debug 186 s, Release 152 s, with the static CRT):
                        M7: package_smoke assembles the package of the configuration, checks the executable's
                          imports, and runs the package's smoke test from its directory (adapters and the
                          environment report; the T01 diagnostic validation; 600 reconstructed frames of the
                          encounter with the layout probe; a ten-second benchmark report: 16 s per
                          configuration). The startup dialog, the console release, the default log, the
                          menu's Controls and About sections, a rebinding of Interact to T through the menu,
                          the prompt showing [T], and the same binding after a restart from the saved file
                          were exercised in windowed runs driven by posted messages (artifacts/m7).
                        M6: the static six-room mirror (the machine parked in Hall A seen through the open
                          inspection door after one bounce, 2e-4); T14 on the encounter (raw, denoised,
                          exposure 0.25, 960x540 internal: the CPU-only hash 797e827499be64cf reproduced bit for
                          bit with all 15 state checks); T15 on the caught variant (chase 3479, caught 3513,
                          the restart to the fuse_carried checkpoint, escaped at 7043; hash e72a6407b234f9b3 on
                          the CPU and under the denoised renderer); nine frozen reference renders at the
                          image-check ticks of the encounter and one of the caught variant (docs/TESTS.md lists
                          each check with its threshold and measured value).
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
Image evidence:       IMAGE CHECKED (artifacts/m4, artifacts/m5/ui, artifacts/m6, and artifacts/m7, regenerate
                      with docs/BUILD.md and docs/TESTS.md; not committed).
                      M7 (artifacts/m7): the startup dialog for a bad level name (the log's last error and
                      the log path in the box); the pause menu's Settings section with the brightness note
                      and the rendering line, the Controls section before and after binding Interact to T
                      ("press a key..." while waiting), the About section (build id, the NVIDIA attribution,
                      the component list, the notices and settings paths), the Controls section after a
                      restart from the saved file (still T), and the prompt "[T] Open the door". Gameplay
                      capture (artifacts/m7/gameplay, tools/capture_gameplay.ps1): twelve presented
                      1920x1080 frames of the encounter replay with the interface: the security door prompt,
                      Hall A, the lamp prompt, the mirror pose, the machine in the mirror, the Plant door,
                      the running fan, the fuse pulled, the wait in the dark Switch room, the dark return
                      behind the machine's silhouette under the emergency glow with "[F] Switch the lamp on",
                      the exit panel, and the vestibule with the end card. This is the fixed replay as
                      presented by the program, not a person's play.
                      M6 (artifacts/m6/encounter, the nine reference renders of the encounter's image-check
                      ticks with their logs; artifacts/m6/six_room_static): tick 1945 shows the dark inspection
                      room with the mirror framing the lit doorway and the machine's silhouette (head above
                      body) crossing behind it, fireflies of the 64 spp dark-room render (KI-014) and nothing
                      else in the room; tick 2700 shows the Plant doorway with the open leaf, the fixture on the
                      ceiling and the fan's hub under it (the blades are edge-on from the doorway; the identity
                      checks are the evidence for them); tick 5879 shows the vestibule lit by its fixture with
                      the open exit leaf on the left and the panel box on the right; tick 4373 (denoised
                      capture during calibration) shows Hall A dark with the emergency fixture's orange glow at
                      the far end, the machine's silhouette walking ahead, and the fused fixture as a dark
                      rectangle on the ceiling.
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
Performance evidence: PERFORMANCE CHECKED with the §17 protocol on the complete encounter (M6 table below): three
                      180-second Release runs of the encounter replay and one of the caught variant (vsync off,
                      windowed 1920x1080 output, 1280x720 internal, denoised mode, sound on, interface off, 5 s
                      warm-up excluded and recorded): GPU frame average 1.83-1.93 ms, median 1.80-1.83, p95
                      2.06-2.37, p99 2.23-2.46, maximum 2.87-3.12 ms; CPU p95 2.09-2.39 ms; no frame above
                      33.3 ms or 50 ms; working set 292-295 MB (peak 298), video memory in use 345 MB (budget
                      11.0 GiB). The pass criterion (p95 <= 16.67 ms, p99 <= 22 ms) holds with a wide margin;
                      the benchmark runs one simulation tick per rendered frame uncapped, so each 180-second
                      window covers the 99 s encounter 15-16 times (the report records the loops). The
                      packaged build (M7) measured once from its own benchmark command: GPU p95 2.40 / p99
                      2.52 ms, working set 308 MB (below the M6 table). Earlier tables (M4, M5) below for the
                      proof.
Failed checks:        None outstanding. During M7: the LNK4098 warning's source was miniaudio's C file compiled
                      without CMake's runtime flags (C was not an enabled language); the first console
                      release doubled every log line (the log file reused the freed console's handle value
                      through the C runtime's stderr; the streams now point at NUL); the first rebinding
                      never saw the key (the frame's input clear ran before the menu; moved after it) and
                      a second attempt missed the button (the brightness note's wrap shifted the layout);
                      the package's smoke test could not run its executable from a PowerShell-started cmd
                      (full paths now) and CTest's PowerShell lacked Get-FileHash (hashing and zipping
                      through .NET now); the end card's capture frame was one frame early (ImGui sizes a
                      new window on its first frame).
                      During M6: the first hand rule ("one held item") refused the fuse while
                      the lamp was carried, so the encounter stalled at the fuse box (D-051: pocket items). The
                      first frozen reference render after the fuse pull never converged: the pocketed fuse's
                      transform was rewritten every frame and counted as motion (a TLAS rebuild and an
                      accumulation reset per frame); the write now follows the feet pose. A world-state check
                      sharing a stop tick with an image check was reported as an unknown image check (the two
                      loops are now separate). The mirror check aimed at the machine's body found the player's
                      own torso in the mirror first (spec §14: the body is real geometry), so the aim moved
                      above it to the head. The glance at the stopped fan turned the wrong way (the look sign).
                      A denoised-mode calibration read 0.010 on a floor whose converged value is 2e-5 (KI-031),
                      which is why the image checks run frozen in reference mode. The M5 record omitted a
                      linker warning (LNK4098, KI-034) that has been present since the sound task.
                      During M5: the first body placement (torso 5 cm in front of the eye) hid
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
                      T11 offset sweeps; T17's live-object report under a debugger (KI-004); T18 on a clean
                      machine (none available: no second machine, virtual machine, or Windows Sandbox on
                      Windows 11 Home; the import check and the package's smoke test from outside the build
                      tree are the substitutes, KI-036). DLSS/Streamline: NOT RUN (deferred). History
                      confidence inputs: not provided (KI-019). Second RTX device: only one GPU present. T12
                      review by a person at normal playback speed: NOT RUN (the automated lag metric, the
                      frame sequences, and the overlays are the evidence). The menu's mouse path was driven
                      by posted messages, not a person's hand. The M5 and M6 gate play-throughs by a person
                      (spec §20): NOT RUN; listening to the sound: NOT RUN. Both need the owner or a tester at
                      the keyboard with headphones; the commands are a double-click on the packaged
                      executable (the demo), `LastCircuit.exe --scene mirror_lab --play --mode denoised` (the
                      proof), and the package's smoke_test.cmd and benchmark.cmd. The demo's image checks are
                      numerical identities and patch means at nine ticks; no reviewer has watched the
                      encounter at playback speed. The usability check of spec §24 (three new players) is
                      NOT RUN.
Changed assumptions:  NRD's license is NVIDIA's proprietary RTX SDK license (attribution required; not open source):
                      recorded for the owner's review. The denoiser's spatial filter biases sharp lighting gradients
                      by a few percent (KI-018); the raw mean stays exact. Mirror planes are static for the PSR
                      motion (KI-020).
Next concrete task:   The items that need a person or a second machine: the play-throughs of the proof and
                      the demo, listening, the T12 review at playback speed, the §24 usability check with
                      three new players, and a clean-machine run of the package (T18) with its smoke test
                      and benchmark. Until then every milestone's automated evidence exists and the human
                      gates are recorded NOT RUN. Deferred engineering, in the order the spec's optimisation
                      rule suggests: the loading phase before the first frame (KI-022), the tone curve
                      (KI-008, KI-016), the reusable-mesh budget (KI-029), the live-object report under a
                      debugger (KI-004), DLSS through Streamline (KI-021), the glTF/GLB subset.
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
encounter replay (the M6 table). M4 process working set about 180 MB, video memory in use about 300 MB
(`artifacts/m4/benchmark_release_*.json`). The Debug build with the debug layer on measures the
same GPU time (its `-Od` shaders do not dominate a RayQuery-bound frame). An earlier set of
mirror-replay runs was invalid: the benchmark loop then still executed the trail-lag test's
per-frame readback (60 ms CPU frames); the loop now never reads back (recorded, not hidden).

## Benchmark table (M6, the six-room demo; spec §17 protocol)

Filled from `artifacts/m6/benchmark_release_*.json` (`tests/scripts/bench_m6.ps1`: Release at the
M6 Task 5 commit, `--vsync off`, no frame cap, windowed 1920x1080 with `--internal 1280x720`,
denoised mode, 4 hits, 30-frame history, anti-firefly on, sound on through the HDMI output, no
interface; each run 5 s of warm-up (about 2500 frames, excluded and recorded in the report) then
180 s measured). The fixed replay is the encounter (camera movement through six rooms and two
halls, the mirror, the moving machine, the carried lamp and its switch, the fuse's source change
on five fixtures and the fan, seven doors); the benchmark runs one simulation tick per rendered
frame uncapped and loops the replay with a world reset, so the 99 s encounter is covered 15-16
times in each window. The caught variant adds a catch and a checkpoint restart per loop.

| Replay | Frames (180 s) | Loops | GPU avg / median / p95 / p99 / max (ms) | CPU avg / median / p95 / p99 / max (ms) | GPU > 33.3 / 50 ms | Working set (peak) / VRAM |
|---|---|---|---|---|---|---|
| t16_encounter, run 1 | 92022 | 15 | 1.93 / 1.83 / 2.37 / 2.46 / 3.12 | 1.95 / 1.88 / 2.39 / 2.51 / 4.04 | 0 / 0 | 292 (293) MB / 345 MB |
| t16_encounter, run 2 | 93133 | 16 | 1.91 / 1.83 / 2.28 / 2.44 / 2.95 | 1.93 / 1.89 / 2.27 / 2.45 / 4.23 | 0 / 0 | 294 (295) MB / 345 MB |
| t16_encounter, run 3 | 95543 | 16 | 1.86 / 1.81 / 2.09 / 2.29 / 2.87 | 1.88 / 1.86 / 2.12 / 2.29 / 3.75 | 0 / 0 | 295 (298) MB / 345 MB |
| t16_caught (a catch and a restart per loop) | 96753 | 14 | 1.83 / 1.80 / 2.06 / 2.23 / 2.87 | 1.86 / 1.85 / 2.09 / 2.24 / 4.29 | 0 / 0 | 292 (296) MB / 345 MB |

GPU pass averages over the runs: path_trace 0.61-0.64 ms, denoise 0.86-0.90 ms, compose
0.24-0.26 ms, upscale 0.03 ms, scene_update 0.07-0.08 ms, copy_out 0.02 ms; denoiser pool 66 MB;
video memory budget reported by the adapter 11.0 GiB. Reading: the complete encounter costs the
same 2 ms of the 16.67 ms frame period as the proof on this GPU (the level is larger but the work
per pixel is the same: one path per internal pixel, four hits, one TLAS); the §17 pass criterion
(p95 <= 16.67 ms, p99 <= 22 ms) holds with an eight-fold margin, and the memory targets (8 GiB
working, 6 GiB video) by a factor of 25 and 17. This is one GPU (RTX 4070 SUPER) above the spec's
RTX 4060 test target; the target device was not measured (KI-006). The first run's p95 (2.37 ms)
against the third's (2.09) is the run-to-run spread also seen in M4 and M5; nothing in the logs
shows a stall.

The packaged build (M7, `dist\LastCircuit-0.1.0-9d04601c6110-win64\benchmark.cmd`, the same
protocol run once from the package directory): 92764 frames in 180 s (15 loops), GPU average 1.91 /
median 1.81 / p95 2.40 / p99 2.52 / max 2.96 ms, CPU p95 2.38 ms, no frame above 33.3 ms, working
set 308 MB, video memory 345 MB; the command prints the criterion as met. The package's own
report (`benchmark\benchmark_20260907-041206.json`) carries the build hash 9d04601c6110.
