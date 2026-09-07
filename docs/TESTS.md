# Tests

## Commands

```powershell
. .\tools\env.ps1
ctest --preset windows-debug --output-on-failure      # everything (72 tests; about 3 minutes in Debug, 2.5 in Release on the reference machine)
ctest --preset windows-debug -L cpu                    # portable unit tests only
ctest --preset windows-debug -L gpu                    # tests that need the GPU
.\build\windows-debug\bin\lc_cpu_tests.exe --list      # case names
.\build\windows-debug\bin\lc_cpu_tests.exe --filter math
```

GPU tests run `LastCircuit.exe`. Exit code 3 (no adapter with DXR Tier 1.1 and Shader Model 6.5)
is reported by CTest as *skipped* (`SKIP_RETURN_CODE 3`); it is never a pass. A machine without
a supported GPU therefore reports the GPU tests as NOT RUN.

Tolerances were chosen before the tests were run and are recorded here; they must not be widened
to pass. When a scene was redesigned (T07, see below) the tolerance stayed and the scene changed.

## CPU tests (`cpu_tests`, 103 cases in `tests/cpu`)

| File | Checks |
|---|---|
| `test_log.cpp` | Error counting through level filtering; clock monotonic; timestamp shapes; `LC_THROW` message carries file and line; strong ids |
| `test_cli.cpp` | Unknown/repeated/valueless options are errors; `--opt value` and `--opt=value`; integer and float validation; `AppOptions` mapping; view-name errors list valid names; conflicting flags; M7: `--version`, the §21 scene names (`last_circuit`, `mirror_lab`) resolve to the level files, a launch without arguments is the demo in play, `--no-dialog`, `--settings` belongs to play |
| `test_settings.cpp` | M7: the settings round trip through JSON and keep their defaults for missing members; every value stays inside its documented range (clamped from a struct and from a file, with the problems listed; a mistyped member and an unbindable key keep their defaults; unknown members are ignored; text that is not JSON is refused with the reason); a missing file is the defaults, a saved file loads back, a broken file is the defaults with one problem; key names, bindable keys (not Escape, F1, mouse buttons, the Windows key), conflicts; the input follows the bindings (the default keys do nothing once rebound), press edges and mouse deltas count only when consumed, inverted look flips the pitch |
| `test_math.cpp` | Right-handed cross product; rotation directions; product order; translation column; direction transform ignores translation; inverse round trip with non-uniform scale; singular detection; transpose; `LookAtRh`; projection depth range and axis signs; FOV conversion; yaw/pitch basis; `Mat3x4` rows |
| `test_image_write.cpp` | CRC-32 and Adler-32 known values; PNG signature, IHDR, chunk CRCs and order, stored-deflate payload decoded byte for byte, multi-block streams; PFM header and row order |
| `test_json.cpp` | Nesting, commas, escaping, empty containers, null |
| `test_material.cpp` | Material ranges (no energy gain, no negative or non-finite values, only emitters emit, active emitters need radiance); luminance weights; default material; material index validation; emitter on/off revision |
| `test_emitters.cpp` | Emitter areas follow instance transforms (uniform scale 2 gives area x4); per-emitter triangle CDF ends at 1; power-proportional selection (radiance 3:1 gives 0.75/0.25); off sources excluded; empty table |
| `test_radiometry.cpp` | Closed-form rectangle irradiance vs 400x400 midpoint quadrature (1e-3 relative) at the centre and at arbitrary points inside, on the edge, and outside the footprint (2e-3); limit `pi * L` for a huge rectangle; GGX directional albedo bounded by 1, monotonic in roughness, equal to `1 - ln 2` at alpha 1 / normal incidence within 0.2 %, stable under grid refinement (0.1 %) |
| `test_material.cpp` (conductor part) | Roughness range [0.02, 1] enforced; type names |
| `test_scene.cpp` | Box closed and outward; quad faces +Y; mesh validation; stable ids and transform history; every built-in hit expectation agrees with a CPU Möller–Trumbore ray cast that follows mirrors; every radiance patch is visible and unoccluded; projection inverts ray generation; `LookAt`; room slabs enclose the volume with solid corners and 0.15 m thickness; the closed door blocks 9 rays through the doorway and clears it when open; rectangle emitters face the requested axis |
| `test_layouts.cpp` | `sizeof`/`offsetof` of every shared GPU record; view-mode names |
| `test_json_reader.cpp` | Nested documents, escapes and surrogate pairs, rejection of trailing commas, bare words, duplicate keys, depth over 64, line/column in errors, writer round trip |
| `test_simulation.cpp` | Whole ticks and alpha, the 0.25 s cap, exact tick runs; replay run-length recording and lookup; JSON round trip; version, ordering, and kind validation |
| `test_world.cpp` | Player movement and pitch clamp; door state machine and interpolated angle; threat path and ping-pong; lamp held pose, sockets, toggle; the world writes only moving transforms and keeps rendered history; the two-room level: triangle and emitter budgets, the mirror shows the threat that is not directly visible, the door blocks the fixture when closed and not when open |
| `test_replay_scripts.cpp` | Runs `t05_mirror_threat`, `t06_door_light`, `t06_lamp_shelf` on the CPU: poses at check ticks, door and lamp states, mirror identity through the ray caster, direct invisibility of the threat, light paths blocked/unblocked by the door, the shelf lamp's line to the floor patch |
| `test_reconstruction.cpp` | NRD matrix conversion to column-major; the Halton(2,3) jitter stays within half a pixel, does not repeat within its sequence, and wraps |
| `test_temporal_checks.cpp` | Trail-lag metric: measured lag on synthetic series, pass/fail against the limit, and invalid series (never leaves, too short, re-entry, no contrast) reported instead of guessed |
| `test_benchmark.cpp` | Nearest-rank percentiles, median, counts above 33.3 / 50 ms; the report parses back as JSON with every required section |
| `test_scene_file.cpp` | The golden `two_room.json` reproduces the proof level (material and instance counts, circuit states, door hinge, sockets, path, markers, the derived mirror aim within 1e-5, collider flags, deterministic reload hash); every validation rule rejects a patched document with a message naming the list, identifier, or field (schema, JSON syntax, duplicate id, unknown material, reflectance range, unknown circuit, box without centre, negative extent, opening leaving no wall, non-axis facing, unknown marker, objectives not implemented, zero speed); limits (object count, file size); asset-root containment (parent traversal and absolute paths refused, missing file reported); schema 2 (M6): the golden file's doors, items, sockets with accept lists, powered circuits, and objective steps; rejection of a door leaf without a door entity, a socket accepting an unknown item, a `poweredBy` whose socket does not accept the item or whose initial state disagrees, a fan with a non-whole blade count or one outside the limits; a fan builds its hub and blades as separate instances with radial frames |
| `test_collision.cpp` | Oriented boxes from the level's kit parts (the door leaf follows its angle); the capsule is pushed out of a wall it enters and slides along it; a centre on a face never crosses to the far side; the sphere sweep stops the carried lamp at a wall; a closing door that meets the player swings back open (`DoorBlocks` counts it); segment tests see through an open doorway and not through the closed leaf; the player cannot walk through the closed door or the walls in the replays (`t05`, `t06`) |
| `test_objective.cpp` | The objective advances only on its events (lamp taken, placed on the shelf socket and not the floor one, taken back; the exit counts only with the lamp retrieved) and saves a checkpoint at each phase; in hunt mode the machine ignores the lit lamp behind the closed door, chases it in the open doorway, catches, and the restart restores the checkpoint pose, phase, door, and lamp with the machine back at its path start; detection needs range (2.5 m dark, 8 m lit), facing, and a clear line; the state hash is equal for equal inputs and differs for one different input or one extra tick; the state-check log evaluates checks after their tick and reports pending ones; the committed `t15_route` and `t15_catch_restart` replays pass every check on the CPU, deterministically, and hash differently; M6 on the proof file patched with a fuse, a fuse box, an exit panel, a powered circuit, a fan, and a locked door: the fuse is offered beside the locked door, hides in the torso when taken, powers the circuit from the exit panel (the fixture lights, the fan spins up to full speed in 3 s with its blades' transforms changing every tick, the locked door opens by itself), taking it back darkens the circuit and spins the fan down to a stop with the door left open, and the hash covers the circuit; a pocketed item is taken with the lamp in the hand, each socket takes the held item it accepts (the panel the fuse, the floor socket the lamp), and the pocketed item's transform is written only when the player moves |
| `test_audio.cpp` | Inverse-square attenuation clamped inside the reference distance and silent past the maximum; pan +1/-1/0 and the diagonal from the listener's right axis, turning the listener turns the pan; the occlusion factor scales the level and keeps the pan; every generated clip is non-empty, finite, under three seconds, peaks between 0.05 and 1, carries a `generated:` provenance, and is deterministic (the hum loop's ends meet, the room tone is exactly 2 s, the footstep variants differ); the director starts hums for lit fixtures only, stops one when its circuit goes off and starts one that comes on, moves the lamp's hum with its transform; door creak and thud on the state edges with the "creaks"/"shuts" cues, lamp click and handling, three footsteps over 2 m alternating variants and never occludable; the machine's loop runs only while it moves (a tick-less frame keeps it, a stop longer than the hold ends it), its cue respects the 6 m / 3 m occluded rule and the 5 s period; chime and sting; the system without a device keeps the cues and drops nothing; fans (M6): the loop starts when a fan turns, its level follows the speed fraction, and it stops with the fan |
| `test_six_room.cpp` | The demo file loads with its expected entities (7 doors with the exit door locked, 2 items, 1 fan, 7 steps, 7 circuits, 4 sockets, 3 path points; 81 instances, 872 triangle instances, 78 meshes; 10 fixtures with 8 active; walkable area 193.1 m²); every room, hall, and the vestibule is enclosed (62-direction ray sweeps from each centre at 1.4 m hit a solid) and every door leaf's corners stay clear of solids at 0, 45, and 90 degrees; the machine's route from the Plant doorway to the Hall B line goes around the corner along the polyline and it is back on patrol in 58 ticks; the mirror's aim bisects the check camera and the hall point, the parked machine is seen through the open inspection door by the CPU caster, the route is clear of solids, and every socket and marker rests on its support inside a room |

## GPU tests (`tests/gpu/CMakeLists.txt`)

### M1: geometry and identifiers

| Test | Command | Pass criteria |
|---|---|---|
| `gpu_list_adapters` | `--list-adapters` | At least one supported hardware adapter |
| `gpu_validate_rt_triangle` | `--scene rt_triangle --mode diag --validate --headless --frames 2` | Layout probe 30/30 fields bit-identical; centre pixel hits id 1 front-facing; four corners miss; RayQuery facing agrees with the geometric normal on every non-grazing hit pixel; zero debug-layer errors |
| `gpu_validate_rt_boxes` | `--scene rt_boxes --mode diag --validate --headless --frames 2 --view ids` | Same on the asymmetric scene (ids 2 left, 3 right, floor 1, misses) |
| `gpu_validate_rt_boxes_facing` | `... --view facing` | Same with the facing view |
| `gpu_resize_test` | `--scene rt_boxes --mode raw --resize-test --vsync off` | Window cycles 640x360, 1920x1080, 800x600, 1280x720; swap chain and outputs follow; no errors |

### M2: raw light transport (spec §19 T03–T09)

All run `--mode reference --headless --validate` with a fixed seed (default 0) and the tolerances
stored in the scene descriptions (`src/scene/builtin_scenes.cpp`). Every run also requires the
invalid-value counters (NaN, inf, negative, zero pdf) to be zero and zero debug-layer errors.

| Test | Scene, samples | Pass criteria |
|---|---|---|
| `gpu_t03_no_sources` | `t03_dark_room`, 32 spp: sealed grey room, one *off* emitter panel, a crate | Every pixel `max |radiance| <= 1e-6` |
| `gpu_t04_sealed_room` | `t04_sealed`, 32 spp: sealed room, a radiance-10 panel 1 m outside the +Z wall facing it | Every pixel `<= 1e-6` |
| `gpu_t04_open_door` | `t04_open`, 64 spp: the same wall with a doorway and the door open 90 degrees | Floor patch 1 m inside the door: mean luminance `> 1e-3` |
| `gpu_t08_analytic_mis` / `_light` / `_bsdf` | `t09_rect_light`, 256 / 256 / 4096 spp, strategies `mis`, `light`, `bsdf`: 10 x 10 m floor (albedo 0.5), 1 x 0.5 m emitter of radiance 4 at 1.5 m facing down | Floor radiance under the emitter centre equals `0.5 * E / pi` with `E` from the closed-form rectangle irradiance within `max(2 %, 3 SE)`; a second patch beside the footprint within `max(3 %, 3 SE)` |
| `gpu_t09_area_large` | `t09_rect_light_large`, 256 spp: emitter 2 x 1 m (area x4) | Same analytic checks with the larger emitter's closed form (the response is not 4x: near field) |
| `gpu_t07_indirect_colour` | `t07_bleed`, 256 spp: white room, red -X wall, white +X wall, ceiling panel, a crate | `R/(G+B)` of the floor patch 0.15 m from the red wall exceeds 1.2x the same ratio 0.15 m from the white wall; both patches lit |
| `gpu_t08_box_positive` | `t08_box`, 64 spp: closed Cornell-style box | Floor, red wall, and back wall patches lit |
| `gpu_mirror_identity_and_energy` | `mirror_box`, 64 spp: 4 x 7 m room, unit mirror on the -Z wall, orange box and a small wall lamp behind the camera | Mirror pixel that reflects the box reports the box's stable id as the first non-mirror hit; a neighbouring mirror pixel reports the +Z wall; the mirror centre's radiance equals the wall lamp's radiance (3.0) within 0.1 % (delta path, noise-free); the mirrored box is lit |
| `gpu_t10_furnace_r05` / `_r35` / `_r70` | `t10_furnace_r*`, 512 spp: F0 = 1 conductor floor (400 m) under a 400 m uniform emitter of radiance 1, camera at 45 degrees; roughness 0.05, 0.35, 0.70 | Reflected radiance equals the single-scattering GGX directional albedo integrated on the CPU from the same formulas (`GgxDirectionalAlbedo`, checked against grid refinement to 0.1 %) within `max(2 %, 3 SE)`; no invalid values; energy never exceeds 1 |
| `gpu_t10_furnace_closed_form` | `t10_furnace_r100_normal`, 1024 spp: roughness 1 at normal incidence | Radiance equals the closed form `1 - ln 2 = 0.3069` within 2 % (D = 1/pi, Lambda = (sec - 1)/2) |
| `gpu_metals_room_positive` | `metals_room`, 64 spp: four steel boxes (roughness 0.05 to 0.8) and a copper slab under a ceiling panel | Copper patch lit; no invalid values (visual scene for inspection) |

### M3: motion through deterministic replays (spec §4, §19 T05/T06)

The replays in `tests/replay/*.json` (version 1) hold run-length input segments (`from`, `to`,
`moveX`, `moveZ`, `lookDx`, `lookDy`, `sprint`, `interact`, `lamp`) and checks at ticks:
`hit` (the pixel of a world point or `[u, v]` must report an entity's stable id as the first
non-mirror surface), `not_visible` (no pixel sees the entity directly, i.e. with zero mirror
bounces), `patch_positive` (mean luminance > `minimum`), `patch_dark` (< `maximum`),
`patch_zero`, `patch_ratio`. `LastCircuit --scene two_room --replay <file> --stop-at-tick N --validate`
advances exactly N ticks, renders, and evaluates the checks at tick N. The CPU test
`test_replay_scripts.cpp` runs every committed replay without a GPU and verifies the player pose,
door and lamp states, threat position, and the mirror/occlusion facts with the ray caster first.

| Test | Replay, tick, mode | Pass criteria |
|---|---|---|
| `gpu_two_room_static_mirror` | static scene, reference 32 spp | The mirror pixel's first non-mirror hit is the threat parked at the crossing point; that mirror patch is lit (`> 1e-4`, derived from the emergency fixture and the threat albedo) |
| `gpu_t05_mirror_threat_reference` / `_raw` | `t05_mirror_threat.json`, tick 740, reference 64 spp and raw 1 spp | The player has walked from Room A through the door and the hall to the Room B check pose; the mirror pixel reports `threat_body` after one mirror bounce; no pixel sees the threat directly; the mirrored threat patch is lit (`> 1e-4`) |
| `gpu_t05_mirror_threat_absent` | tick 930, reference 16 spp | With the threat at the far end of its path the same pixel reports the hall wall `a_h1_wall_neg_z` |
| `gpu_t06_door_open` | `t06_door_light.json`, tick 300, reference 64 spp | The hall floor patch by the open door is lit by Room A's fixture (`> 5e-3`); the open leaf is seen standing in the hall |
| `gpu_t06_door_closed` | tick 420, reference 64 spp | The same patch is dark (`< 2e-3`, only the emergency spill remains); the closed leaf fills the doorway |
| `gpu_t06_lamp_on_shelf` | `t06_lamp_shelf.json`, tick 800, reference 128 spp | The lamp, picked up in Room A and carried through the level, rests on the shelf (housing and shelf identities); the floor in front of the shelf is lit (`> 3e-4`) in the otherwise dark inspection room |
| `gpu_t06_lamp_off` | tick 900, reference 128 spp | After the F press the same patch is dark (`< 1e-4`) |
| `gpu_replay_motion_tlas_rebuilds` | `t06_door_light.json`, one tick per frame, raw, 120 frames | TLAS rebuild count equals the number of frames with motion (plus the first frame); zero debug-layer errors |

### M4: denoised mode and temporal checks (spec §13, §19 T12)

`--mode denoised` traces one sample per pixel per frame and reconstructs with NRD REBLUR
(docs/RENDERING.md, "Denoised mode"). Two images come out of every run: the denoised production
image (`linear`) and the mean of the recomposed raw samples since the last history reset
(`rawMean`, written as `<capture>_raw.pfm`). `--validate` holds the raw mean to the scene's
reference tolerances (the split into signals and the demodulation are lossless) and the denoised
image to a documented denoiser tolerance: for analytic patches 5 % relative
(`kDenoisedRelativeTolerance`), chosen before the first measurement; the other patch kinds keep
their thresholds. Replay checks gained `denoised_patch_positive/dark` (on the denoised image),
`motion`, `static_motion`, and `trail_lag` (definitions in `src/game/replay.h`). Checks on
frame-by-frame replays are evaluated at the final tick (`--frames N` renders ticks 0..N-1 and the
checks at tick N are evaluated at the end).

| Test | Run | Pass criteria and result on the recorded machine |
|---|---|---|
| `gpu_denoised_split_lossless` | `t09_rect_light`, 64 frames | Raw mean within the M2 tolerance of the closed-form irradiance (measured -0.24 % and +0.21 %); denoised within 5 % (-3.6 % under the emitter, -4.0 % beside it: the spatial filter's bias on the sharpest direct-light gradient in the test set; it was -7 / -9 % before NRD's pre-accumulation blur was switched off and the light-sample distance was mixed into the diffuse hit distance) |
| `gpu_denoised_box_static` | `t08_box`, 120 frames | Raw-mean and denoised lit patches (denoised within 1.5 % of the raw mean); invalid-value counters 0; 0 debug-layer errors |
| `gpu_denoised_mirror_box`, `gpu_denoised_metals_room` | 64 frames each | The M2 mirror identity/energy scene and the conductor scene run through the PSR path and the specular signal with their expectations on the raw mean and the denoised image |
| `gpu_denoised_blackout_reset` | `t08_box`, `--blackout-at-frame 60 --frames 62` | Reset correctness (spec §19): after every emitter is switched off and the history is explicitly reset, the denoised image two frames later has a maximum radiance <= 1e-4 and the raw mean since the reset is exactly 0 (measured 0 and 0) |
| `gpu_denoised_motion_lamp` | `t12_lamp_motion.json`, 400 frames | The carried lamp housing at the lower right reports its identity and a guide motion equal to its rendered translation between the previous and this image (within 2e-4 m); a hall wall pixel has exactly zero motion while the camera moves |
| `gpu_denoised_motion_mirror_threat` | `t12_mirror_motion.json`, 740 frames | Mirror pixel: threat identity after one mirror bounce, threat not directly visible, guide motion equal to the threat's translation reflected in the mirror plane (PSR; measured error 6.5e-5 m against 2e-4 m), the wall facing the camera exactly static, the mirrored threat lit in the denoised image (`> 1e-4`) and in the raw mean |
| `gpu_denoised_trail_lag` | `t12_mirror_motion.json`, 930 frames, per-frame readback | Trail lag (below) on the 5x5 mirror patch after the threat leaves it: measured 0 frames against the 6-frame (100 ms) limit; the denoised luminance follows the occupancy of the patch frame by frame (1.0e-3 with 25 threat pixels, 2.6e-3 with 5, 3.0e-3 with 0) |
| `gpu_denoised_resize_test`, `gpu_denoised_scaled_resize_test` | windowed | The NRD instance is recreated on every resize (native, and 960x540 internal with the presented size following the window); 0 debug-layer errors |
| `gpu_benchmark_smoke` | `--benchmark-seconds 2 --warmup-seconds 0.5`, headless | The benchmark loop runs, restarts the replay, and writes a report with every §21 section |
| `gpu_scene_reload_test` | `t06_door_light.json`, denoised, `--reload-test --frames 6` | The scene file is reloaded at frame 2 (parse and validate, GPU wait, new world, upload, history reset); one reload, no refusal, two history resets, 0 debug-layer errors. All two_room tests now run on the JSON level |

**Trail lag** (the automated measure of spec §19 "no obvious obsolete silhouette persisting longer
than 100 ms"): per frame, the mean denoised luminance of the patch and the number of patch pixels
whose hit identity is the moving entity are recorded. Departure is the first frame with zero
entity pixels after a frame with some. The settled value is the mean over 12 frames starting 20
frames after departure (the entity must stay out that long). The lag is the number of frames after
departure until the luminance is within 20 % (`settleFraction` 0.8) of the step from the last
occupied frame to the settled value. Implemented in `src/app/temporal_checks.*` and pinned by
`test_temporal_checks.cpp`; global image averages are never used (the patch is the reflected
threat).

Frame sequences (spec §18): `--capture-sequence <dir> --capture-from A --capture-to B
--capture-every K --capture-crop x,y,w,h` writes one cropped display PNG per selected frame plus
`sequence.json` (frame, tick, door and lamp state, threat and player positions, history resets)
from the running program. Sequences kept for the M4 record: the mirror during the threat's exit
(t12 frames 735–769, 200x200 crop), the door edge while it closes (t06 frames 290–329), the
carried lamp while walking (t12 lamp frames 380–419).

Cross-run comparisons via `tests/scripts/compare_runs.ps1` (patch means per channel within
`max(relTol * |ref|, 3 * sqrt(se_a^2 + se_b^2))`):

| Test | Runs | Pass criteria |
|---|---|---|
| `gpu_t08_box_strategies_agree` | `t08_box`: `mis` 512 spp 4 hits, `light` 1024 spp 4 hits, `bsdf` 4096 spp **5** hits | Every patch and channel agrees within 3 % (observed: within 1 %). `bsdf` gets one extra hit so all three cover the same path family (see RENDERING.md, "Path families") |
| `gpu_t07_agrees_with_higher_sample_reference` | `t07_bleed`: `mis` 2048 spp vs 256 spp | Within 2 % or 3 SE (spec T07: agreement with a higher-sample reference) |
| `gpu_t08_box_depth_truncation_report` | `t08_box`: 12 hits vs 4 hits, 512 spp | Informational (never fails): reports the truncation bias of the 4-hit production budget |

### M5: the playable proof (spec §14, §15, §19 T13–T15)

| Test | Command (abridged) | Checks |
|---|---|---|
| `gpu_t13_player_in_reflection` | `--replay t13_reflection.json --stop-at-tick 760 --mode reference --spp 64 --validate` | At the mirror-check pose the mirror pixel aimed at (6.4, 1.35, 12.0) reports `player_torso` after one mirror bounce (the player is not omitted from reflections); the torso is not directly visible at level pitch; the reflected torso patch is lit (minimum 5e-6, measured 2e-5 with the lamp on the floor socket and the hall spill) |
| `sim_t15_route_cpu_only` | `--replay t15_route.json --simulate-only --validate --expect-state-hash <route hash>` | No graphics at all: 1481 ticks of the careful route (lamp taken, switched off, the machine's round waited out behind the wall, the hall crossed behind it at a sprint, the lamp placed on the shelf and switched on, taken back and switched off, the crossing back, the exit reached with the lamp) with the world-state checks: `lamp_acquired` at tick 11, `patrol` at 300 and 1000 (never seen), `lamp_placed` 715, `lamp_retrieved` 731, `escaped` at 1480, `caught_count` 0, the player within 0.5 m of the exit; the final state hash equals the recorded constant |
| `sim_t15_catch_restart_cpu_only` | `--replay t15_catch_restart.json --simulate-only --validate --expect-state-hash <catch hash>` | The lit lamp carried into the open doorway is seen from the hall (`chase` at tick 157), the walk continues into the machine (`caught_count` 1 at tick 209), the restart puts the player at the checkpoint pose within 0.05 m with the phase kept (`lamp_acquired`) and the machine back on its round (`patrol`); the careful route then completes (`lamp_placed` at 924, `escaped` at 1690, still one catch) |
| `gpu_t14_rules_raw` / `_denoised` / `_exposure` / `_internal_size` | `--replay t15_route.json --frames 1481 --validate --headless --expect-state-hash <route hash>` with `--mode raw`, `--mode denoised`, `--mode denoised --exposure 0.25`, `--mode denoised --internal 960x540` | T14: the same replay rendered four different ways runs the same world-state checks and reproduces the CPU-only hash bit for bit (the hash covers every tick's objective phase, threat state and pose, player pose, lamp, door, catches) |
| `gpu_t15_catch_restart_denoised` | `--replay t15_catch_restart.json --mode denoised --frames 1691 --validate --headless --expect-state-hash <catch hash>` | T15 under the production renderer: the catch, the checkpoint restart (a history reset), and the completion, with the CPU-only hash |

Performance with this content (spec §17 protocol, `tests/scripts/bench_m5.ps1`): the M5 benchmark
table in `docs/STATUS.md`. The gate's play-through by a person is recorded there as NOT RUN.

The hash constants live in `tests/gpu/CMakeLists.txt` (`LC_T15_ROUTE_HASH`, `LC_T15_CATCH_HASH`); a deliberate rule change regenerates them with the `--simulate-only` command, and the CPU tests check the replays' outcomes and determinism without the constant. World-state check kinds: `objective_state`, `threat_state`, `caught_count`, `player_near` (docs in `src/game/replay.h`); they run right after their tick in every mode, including runs frozen with `--stop-at-tick`.

The collision part of T13 (neither the body nor the lamp passes through an opaque wall) runs on
the CPU (`test_collision.cpp`), and every replay now moves the player against the same solids: a
replay that walked through geometry would stop at the wall and its pose checks would fail. The
T06 door-open test caught the first body placement (D-039): the torso hid the floor patch a metre
ahead and the check dropped from 0.0196 to 0.0048; the body moved, the threshold did not.

### M6: the six-room demo (spec §4, §5, §15, §17, §19 T04–T06, T12–T16)

The demo is `assets/scenes/six_room.json` (schema 2, D-046) with two replays, both in hunt mode.
`t16_encounter.json` (5961 ticks, 99 s): Security → Hall A while the machine walks away east →
Equipment (the lamp taken and switched off) → Inspection (the lamp on the shelf and switched on;
the mirror check pose, where the machine's head is seen in the mirror as it crosses Hall A behind
the open door) → the lamp taken back → Hall A east and Hall B south, the Plant door opened (the
running fan seen from the doorway) → the Switch room (`fuse_available`), the fuse pulled while the
lamp is carried (`fuse_carried`: the hall circuit dies, so the Switch room, both halls, the Plant
room and its fan lose power) → the machine's pass waited out → the return north behind it at
walking pace in the dark, with a glance at the stopped fan → Hall A west behind it → into the
Equipment room while the machine turns at the west end and comes back east → Hall A west → the
Exit room, the fuse into the exit panel (`exit_powered`: the vestibule lights and the locked exit
door opens) → the vestibule with the lamp (`escaped` at tick 5920). `t16_caught.json` (7084 ticks):
the same inputs until the fuse, then out of the Switch room too early up dark Hall B into the
machine coming south (chase at tick 3479, caught at 3513), the restart from the `fuse_carried`
checkpoint (the player back at the fuse box, the machine back at the start of its round), and the
encounter's inputs from the wait onward shifted by 1123 ticks to the restarted round's timing
(`escaped` at 7043). The image checks of the replays are evaluated frozen at their tick (D-052).

| Test | Command (abridged) | Checks (and the values measured on the recorded machine) |
|---|---|---|
| `gpu_six_room_static_mirror` | `--scene six_room --mode reference --spp 32 --validate --headless` | The static scene with the inspection door opened for the check (D-050): the mirror pixel reports the machine parked at the hall point after one mirror bounce; the mirror patch is lit (`> 1e-4`, measured 2e-4) |
| `sim_t16_encounter_cpu_only` | `--replay t16_encounter.json --simulate-only --validate --expect-state-hash 797e827499be64cf` | 15 world-state checks: the phases `lamp_acquired` 811, `lamp_placed` 1356, `lamp_retrieved` 2053, `fuse_available` 3170, `fuse_carried` 3219, `exit_powered` 5779, `escaped` 5960; `patrol` at 300 (the machine passes the open Security door), 1945 (the mirror pass), 3600 (past the Switch room door), 5215 (after the dark return); the mirror pose at 1430 and the hiding place at 4900 within 0.2–0.3 m; `caught_count` 0; the vestibule at 5960 |
| `sim_t16_caught_cpu_only` | `--replay t16_caught.json --simulate-only --validate --expect-state-hash e72a6407b234f9b3` | 20 checks: `chase` at 3480, `caught_count` 1 at 3514 with the phase kept (`fuse_carried`), the machine back on `patrol`, the player within 0.05 m of the fuse box; the encounter's later checks at their shifted ticks; one catch at the end |
| `gpu_t16_rules_raw` / `_denoised` / `_exposure` / `_internal_size` | `--replay t16_encounter.json --frames 5961 --validate --headless --expect-state-hash <encounter hash>` with `--mode raw`, `--mode denoised`, `--exposure 0.25`, `--internal 960x540` | T14 on the demo: the four renderings run the same state checks and reproduce the CPU-only hash |
| `gpu_t16_caught_restart_denoised` | `--replay t16_caught.json --mode denoised --frames 7084 --validate --headless --expect-state-hash <caught hash>` | T15 on the demo under the production renderer: the catch, the checkpoint restart, the completion |
| `gpu_t16_image_tick421` | `--replay t16_encounter.json --stop-at-tick 421 --mode reference --spp 64 --validate --headless` | Before the fuse: Hall A's fixture face emits (`> 1.0`, measured 4.39) and the floor beside it is lit (`> 0.01`, 0.026) |
| `gpu_t16_image_tick1945` | `--stop-at-tick 1945 ...` | T05 on the demo: the mirror pixel aimed at (10.3, 1.5, 16.52) reports `threat_head` after one mirror bounce (the machine crossing Hall A behind the open inspection door; the aim is above the player's own torso, which the mirror shows too, D-039); neither the head nor the body is directly visible |
| `gpu_t16_image_tick2700` / `_tick2715` | `--stop-at-tick 2700` / `2715 ...` | The running fan from the Plant doorway: the pixel of (19.55, 2.45, 6.7) reports `plant_fan_blade0` at tick 2700 (the angle `pi * (2700 - 89.5) / 60` from the spin-up integral, D-048) and the `ceiling` a quarter second later (45 degrees on, the gap between blades); the floor under the fan is lit (`> 0.01`, 0.039) |
| `gpu_t16_image_tick4014` | `--stop-at-tick 4014 ...` | The glance back after the fuse: the pixel of (19.773, 2.45, 6.503) on the rest angle `pi * 3218 / 60` reports `plant_fan_blade0` (the fan stopped where the symmetric integrals put it); the Plant floor is dark (`< 1e-3`, 0.00000) |
| `gpu_t16_image_tick4373` | `--stop-at-tick 4373 ...` | The dark return in Hall A: the fixture face is dark (`< 0.01`, 4e-5) and the floor beside it (`< 1e-3`, 2e-5); the machine walks ahead in the dark |
| `gpu_t16_image_tick5527` | `--stop-at-tick 5527 ...` | The emergency fixture on its own circuit still lights the west wall of Hall A (`> 0.005`, 0.011) |
| `gpu_t16_image_tick5729` / `_tick5879` | `--stop-at-tick 5729` / `5879 ...` | The pixel of (2.2, 1.2, 3.3) reports the closed `door_exit` before the fuse goes in and `vestibule_wall_south` after (the door opened with its circuit); the wall is lit by the exit fixture (`> 0.02`, 0.092) |
| `gpu_t16_caught_image_tick7002` | `--replay t16_caught.json --stop-at-tick 7002 ...` | The exit door open and the vestibule lit after the restart (the 5879 checks at their shifted tick) |

The hash constants live in `tests/gpu/CMakeLists.txt` (`LC_T16_ENCOUNTER_HASH`,
`LC_T16_CAUGHT_HASH`), regenerated only by a deliberate rule change with the `--simulate-only`
command (the caught variant is derived from the encounter file by a generator that shifts the
inputs after the catch by the restart's timing; both are committed as plain replay files). A run
stopped with `--frames N` or `--stop-at-tick N` below a replay's last check tick leaves the later
state checks unevaluated and says so; a state check inside the run that never fired still fails.

The patch thresholds were chosen before the reference runs; the measured values above are the
record. A first calibration in denoised mode (the recomposed 1 spp raw mean) read 0.010 on the
Hall A floor whose converged value is 2e-5 (one bright sample in 25 pixels), which is why the
replays' image checks run in reference mode (KI-031). That calibration also found two errors that
the checks were built to catch: the mirror check aimed at the machine's body was blocked by the
player's own torso in the mirror (the aim moved above it to the head), and the first frozen render
after the fuse pull never converged because the pocketed fuse's transform was rewritten every
frame (D-051).

Performance with this content (spec §17 protocol, `tests/scripts/bench_m6.ps1`: three 180-second
Release runs of the encounter and one of the caught variant): the M6 benchmark table in
`docs/STATUS.md`. The M6 gate's human items are recorded there.

### M7: the package (spec §20 M7, §21, §19 T01/T18)

| Test | Command (abridged) | Checks |
|---|---|---|
| `package_smoke` | `powershell -File tools/package.ps1 -Preset windows-<config> -NoBuild -NoZip -SmokeTest -OutDir <build>/artifacts/package` | The packaging command assembles the package of the current configuration (executable, shaders, assets, replays, generated notices, README, known issues, the two commands, the manifest), refuses an executable that imports anything outside Windows' in-box libraries (the PE import and delay-import tables), then runs the package's `smoke_test.cmd /quiet` from the package directory: adapters and the environment report, the T01 diagnostic validation of `rt_boxes`, 600 reconstructed frames of the encounter replay with the layout probe and the world-state checks inside that range, and a ten-second benchmark report; exit 3 (no supported adapter) is reported as skipped |

The startup dialog, the console release, the settings file, the rebinding, and the persisted
settings were checked in windowed runs driven by posted messages (`artifacts/m7`): the dialog for
a bad level name (with the log's last error and the log path), the pause menu's Settings,
Controls (press-a-key rebinding of Interact to T, the conflict note, the reset), and About
sections, the prompt showing `[T]`, and the same binding after a restart from the saved file.

### T17: the reliability target (spec §19)

`tests/scripts/reliability_m7.ps1` (Release, `artifacts/m7/reliability`): the 30-minute loop of
the caught replay under the benchmark (a catch and a checkpoint restart, the fuse's source change,
seven doors, the mirror, the moving machine, the camera walk; 1920x1080 from 1280x720, denoised,
vsync off, sound on) and a separate five-minute sequence of the encounter replay while the script
resizes the window every five seconds through four sizes and minimises and restores it every
twenty seconds. Every rendered run logs a memory line each minute (working set and peak, video
memory and budget, denoiser pool, history resets). Pass: exit code 0, no error in the log, memory
without a trend that the run does not explain. Results on the recorded machine are in
`docs/STATUS.md` (137 loops in 30 minutes and 54 resizes with 13 focus cycles, no error; the only
working-set growth is the benchmark's sample storage, KI-038). The live-object report under a
debugger and device-removal recovery remain NOT RUN (KI-004, KI-007).

### Mirror expectation derivation (`mirror_box`)

Camera at (0, 1.2, 3.0) looking along -Z; mirror front face at z = -1.98 (4.98 m away). A ray that
hits the mirror at `(xm, ym, -1.98)` reflects into direction `(xm, ym - 1.2, 4.98)` (unnormalized)
and reaches the box's front face at z = 3.2 after `s = (3.2 + 1.98) / 4.98 = 1.0402`, where
`x = xm (1 + s)` and `y = 1.2 + (ym - 1.2)(1 + s)`. The box front-face centre (0.8, 0.4) therefore
appears at `xm = 0.392`, `ym = 0.808`. The mirror centre reflects straight back through the camera
to the wall lamp at (0, 1.2, 4.99).

## Manual inspection (IMAGE CHECKED)

Regenerate with `--capture` (see `docs/BUILD.md`) and inspect:

- M1 diagnostic views: floor +Y green, faces toward the camera blue, a box's +X side reddish; the
  `facing` view uniformly green; one flat colour per instance in `ids`.
- `t08_box` reference: soft shadows under and beside both blocks, red and green colour bleeding on
  the floor and the block sides near the coloured walls, no fireflies, no black seams at wall
  joints, the emitter panel white.
- `mirror_box`: the orange box and the two lamps visible only inside the mirror, correct left-right
  (the box that stands to the camera's right appears on the right side of the mirror image), a
  sharp mirror edge.
- `t04_open`: light enters only through the doorway; the open leaf casts a shadow; the region seen
  through the doorway above the panel is black (nothing exists outside).

Approved image baselines are not yet stored; radiance is currently checked numerically as above.

- `metals_room`: sharp reflections of the panel in the smoothest steel box, progressively blurrier
  in the rougher ones, a copper-tinted highlight on the slab, no fireflies.

- `two_room` at tick 740 (exposure 6): Room B dark; the mirror on the left shows the warm-lit hall
  through the doorway with the threat's silhouette; at tick 300 Room A's light falls through the
  open doorway onto the hall floor with the open leaf beside it; at tick 800 the lamp on the shelf
  lights the floor of the dark inspection room.

- Denoised two_room (M4): tick 740 at exposure 6 (the mirror shows the threat with clean shading
  and no residual noise), the `history`, `motion`, `normals`, `viewz`, `raw`, and `validation`
  overlays of the same frame, tick 300 (open door) and tick 800 (shelf lamp) denoised, and the
  three cropped sequences with their event logs (`artifacts/m4/seq_*`).

- Interface (M5, `artifacts/m5/ui`): screen captures of the live window (`--play --mode denoised`)
  driven by posted key and mouse messages: the controls card with the objective line and the
  door prompt at the start; the prompt alone after the first step; the pause menu (Escape) with
  the settings; the menu with the F1 panel; play with the panel; "Close the door" after E opened
  the door with the threat visible through the doorway; the start pose again after Restart from
  the menu (mouse click); the log shows pause, resume, restart, and quit from the menu and no
  D3D12 messages. Captures through `--capture` never contain the interface (it is drawn into the
  back buffer after the copy).

- Sound (M5, `artifacts/m5/ui/08_*`, `09_*`, `audio_check.log`): the same scripted run with the
  panel open shows the device line (`4-5 voice(s) on '<output device>' at 48000 Hz, 0 dropped`:
  the room tone, the three lit hums, the machine while it moves) and the log shows the cues in
  the order of the events (door creak at the door's state change, the lamp click at the toggle,
  the machine nearby); the cue text appears under the objective line. The lamp's sound position
  is the fixture transform (spec §14, T13's sound part): `AudioSnapshotOf` reads the lamp pose
  the renderer also uses. Hearing the output is NOT RUN by a person in this record.

- Catch and restart (M5, `artifacts/m5/ui/10_*`–`12_*`): the catch replay in a window with the
  panel: the chase in the hall, the panel after the restart (catches 1, restarts 1, history resets
  2, the phase kept), and the end card's phase "escaped" with one catch. Screen captures of this
  window carry transparent holes that the presented back buffer does not have (KI-028, checked
  with `--capture-backbuffer 340`: 0 pixels with alpha below 255).

## Not yet implemented

T11 systematic offset sweeps (partly covered by the T03/T04 seals and the 400 m furnace floor);
T17's live-object report and device-removal recovery; T18 on a clean machine (KI-036).
The §4 fourth sequence step (the threat's shadow moving across a wall before direct contact) is
not staged as a check: the fan's moving shadow and the machine seen in the mirror are the moving
evidence in the demo. T12 human confirmation at normal playback speed and the M5/M6 play-through by
a person are recorded in STATUS.md as NOT RUN; the automated metrics, the frame sequences, and the
replays are the evidence.
