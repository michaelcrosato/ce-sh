# Tests

## Commands

```powershell
. .\tools\env.ps1
ctest --preset windows-debug --output-on-failure      # everything (about 30 s on the reference machine)
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

## CPU tests (`cpu_tests`, 50 cases in `tests/cpu`)

| File | Checks |
|---|---|
| `test_log.cpp` | Error counting through level filtering; clock monotonic; timestamp shapes; `LC_THROW` message carries file and line; strong ids |
| `test_cli.cpp` | Unknown/repeated/valueless options are errors; `--opt value` and `--opt=value`; integer and float validation; `AppOptions` mapping; view-name errors list valid names; conflicting flags |
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
| `test_scene_file.cpp` | The golden `two_room.json` reproduces the proof level (material and instance counts, circuit states, door hinge, sockets, path, markers, the derived mirror aim within 1e-5, collider flags, deterministic reload hash); every validation rule rejects a patched document with a message naming the list, identifier, or field (schema, JSON syntax, duplicate id, unknown material, reflectance range, unknown circuit, box without centre, negative extent, opening leaving no wall, non-axis facing, unknown marker, objectives not implemented, zero speed); limits (object count, file size); asset-root containment (parent traversal and absolute paths refused, missing file reported) |

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

## Not yet implemented

T11 systematic offset sweeps (partly covered by the T03/T04 seals and the 400 m furnace floor),
T13–T18 (T16's fixed 180-second full-encounter replay needs the M5/M6 content; the benchmark
command exists). The §4 fourth sequence step (the threat's shadow moving across a wall before
direct contact) is staged in M5. T12 human confirmation at normal playback speed is recorded in
STATUS.md as NOT RUN with a person; the automated lag metric and the frame sequences are the
evidence.
