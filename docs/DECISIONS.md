# Decisions

Format: date, issue, evidence, decision, consequence, rollback.

## D-001 (2026-09-06) Build generator: Visual Studio 18 2026 via presets

- Issue: the spec requires committed CMake presets that work from a plain shell.
- Evidence: CMake 4.3.1 and Ninja 1.13.2 are bundled with Visual Studio 2026 but not on `PATH`; a Ninja preset would need the `vcvars` environment first.
- Decision: presets use the "Visual Studio 18 2026" x64 generator with one binary directory per preset and a single configuration each; `tools/env.ps1` puts the bundled CMake on the path.
- Consequence: configure takes a few seconds longer than Ninja; shader compilation runs as MSBuild custom build steps.
- Rollback: add `windows-ninja-*` presets that assume a developer shell.

## D-002 (2026-09-06) In-box Windows SDK headers and DXC; no Agility SDK yet

- Issue: which D3D12 headers and shader compiler to pin.
- Evidence: SDK 10.0.26100.0 contains DXR Tier 1.1, DRED 2, SM 6.9 enums, and DXC 1.8.2502.11 with `dxil.dll`; the driver reports DXR Tier 1.2 and SM 6.8 on this machine.
- Decision: use the SDK's headers and its DXC (`cmake/LcFindDxc.cmake` locates them and prints the version). Do not add the Agility SDK until a feature beyond the in-box runtime is needed.
- Consequence: no NuGet or download step; shader compiler version is pinned by the recorded SDK version.
- Rollback: add the Agility SDK NuGet package via a pinned URL and hash.

## D-003 (2026-09-06) Own math layer with column vectors and row-major storage

- Issue: spec §9 wants column-vector notation and one explicit HLSL matrix layout; DirectXMath uses row vectors.
- Decision: `lc::math::Mat4` stores `m[row][col]`, math uses column vectors, shaders use `#pragma pack_matrix(row_major)` and `mul(M, v)`; structured buffers store matrices as explicit `float4` rows.
- Evidence: CPU tests for rotation direction, product order, inverse, look-at, projection; GPU layout probe matches 30/30 fields.
- Consequence: no DirectXMath dependency; explicit conversion needed if DirectXMath data ever enters.
- Rollback: none needed.

## D-004 (2026-09-06) DXR facing rule: no instance winding flag

- Issue: which winding DXR treats as front-facing for right-handed, counter-clockwise geometry.
- Evidence: the first GPU run set `D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE` and every one of 311,329 hit pixels reported back-facing while the geometric normal faced the ray. Removing the flag gave 0 mismatches.
- Decision: DXR's numeric rule (`dot(cross(p1 - p0, p2 - p0), dir) < 0` is front) already equals "counter-clockwise seen from outside" in this frame; instances use `FLAG_NONE`. The `facing` view and validation keep checking consistency on every pixel.
- Consequence: any imported asset with clockwise winding must be fixed at import, not by a flag.
- Rollback: not applicable; the test defines the truth.

## D-005 (2026-09-06) Error handling and exit codes

- Decision: initialization failures throw `lc::Error` (with `UnsupportedHardware` and `UsageError` subclasses) carrying the failing call, HRESULT text, and file:line; per-frame paths log and return. Exit codes: 0 ok, 1 failure, 2 usage, 3 unsupported hardware; CTest treats 3 as skipped.
- Consequence: no silent fallbacks; unsupported machines get a clear message listing the adapters found.

## D-006 (2026-09-06) In-house test harness

- Decision: `tests/cpu/lc_test.h` (registration macro, checks, one executable) instead of a third-party framework; CTest drives it and the GPU tests.
- Consequence: no dependency to pin; per-case CTest granularity is not available (the executable prints per-case results).
- Rollback: swap in doctest or Catch2 with a pinned version if richer reporting is needed.

## D-007 (2026-09-06) No third-party runtime code in M0/M1

- Decision: PNG (stored deflate), PFM, and JSON writers are written in `lc_core` with tests; ImGui, a JSON parser, NRD, Streamline, and audio are deferred to the milestones that need them.
- Consequence: captures are uncompressed (about 3.5 MB per 1280x720 PNG).

## D-008 (2026-09-06) Frame pacing and synchronization

- Decision: one direct queue and fence; two frames in flight, each with its own allocator and 4 MiB upload arena; a three-buffer flip-discard swap chain with optional tearing; full GPU waits only for setup, resize, readback, and the layout probe (all logged).
- Consequence: CPU can run one frame ahead; captures and resizes stall by design.

## D-009 (2026-09-06) Shaders compiled at build time

- Decision: `cmake/LcShaders.cmake` runs `dxc.exe` per shader (`-HV 2021 -WX -Zi`, `-Od` in Debug and `-O3` in Release) into `bin/shaders/*.cso` with PDBs; the executable loads the signed DXIL. No runtime compiler dependency; hot reload can be added as a development-only path later.
- Consequence: a shader error fails the build with the shader, entry, profile, and DXC diagnostics.

## D-010 (2026-09-06) Console subsystem for development builds

- Decision: `wmain` with the console subsystem so logs reach the terminal and CTest; the M7 package will decide between a windowed subsystem with `AttachConsole` or keeping the console.

## D-011 (2026-09-06) Resource state policy

- Decision: default-heap buffers are created in `COMMON` (the runtime ignores other initial states for buffers) and rely on promotion and decay; textures track their state explicitly; acceleration structures are created in `RAYTRACING_ACCELERATION_STRUCTURE`. TLAS rebuilds are bracketed by UAV barriers on the single in-order queue.
- Evidence: zero debug-layer errors or warnings across all validation runs.

## D-012 (2026-09-06) Internal resolution equals output resolution in M1

- Decision: `--width/--height` set both the window client size and the trace resolution; the spec's 1280x720 internal / 1920x1080 output split arrives with the scaled presentation path in M4.
- Consequence: `CopyResource` requires identical sizes; a mismatch skips the present copy with a warning until the next resize.

## D-014 (2026-09-06) Explicit material types

- Decision: `Diffuse`, `Mirror`, `Emitter` as explicit records with validated ranges (spec §7); emitters carry their own diffuse reflectance so an off source stays a surface; no material graph, no normal maps, no metalness model.
- Rollback: add `RoughConductor` as a fourth explicit type (planned), never a generic graph.

## D-015 (2026-09-06) Closed-form irradiance as the M2 reference

- Issue: T08/T09 need an independent reference for the estimator.
- Decision: the configuration-factor formula for a Lambertian rectangle (`RectangleIrradianceAtPoint`) is checked against numerical quadrature on the CPU and then used as the truth for the GPU estimators on an open floor. Enclosed scenes are checked by estimator cross-agreement and higher-sample agreement instead.
- Consequence: an integrator error in NEE, BSDF sampling, or MIS weights shows up as a relative error against a known number, not as a "looks plausible" judgement.

## D-016 (2026-09-06) Hash-based deterministic sampling

- Decision: `Hash4(pixel, sample, dimension, seed)` with the PCG permutation; fixed dimension table per bounce; no clock input; raw mode varies the seed per frame.
- Consequence: `1/sqrt(N)` convergence; a low-discrepancy sequence can replace it later behind the same `Rand(key, dimension)` interface.

## D-017 (2026-09-06) Same path family when comparing estimators

- Evidence: `bsdf` at 4 hits was 5–9 % below `mis`/`light` at 4 hits in the closed box while all three matched the analytic open-floor value.
- Decision: NEE-based estimators with `N` hits reach direct light at the `N`-th surface; BSDF-only needs `N + 1` hits for the same family. The comparison test uses `bsdf` with 5 hits; the production integrator keeps 4 hits with NEE at the last vertex (weight 1).
- Consequence: documented in RENDERING.md and TESTS.md; the tolerance (3 %) was not widened.

## D-018 (2026-09-06) Raw mode shows a single sample per frame

- Decision: raw mode resets accumulation every frame and changes the seed with the frame index, so the image is the current sample only (spec §13 "show current samples without temporal filtering"). Temporal reconstruction is a separate mode (M4).

## D-019 (2026-09-06) Built-in generated scenes until M3

- Decision: test scenes are C++ builders on the generated kit (rooms with 0.15 m slabs, doorway frames, hinged 0.04 m doors, rectangle emitters). Versioned JSON scene files and the glTF subset arrive with M3, when the two-room proof needs sockets, circuits, and waypoints.

## D-020 (2026-09-06) Scene redesign instead of tolerance change (T07)

- Evidence: the first T07 layout (red and white boxes) gave a redness ratio of 1.18 against a pre-chosen 1.2 factor.
- Decision: keep the factor; make the scene physically stronger (a full red wall against a white wall, patches 0.15 m from each). Observed ratio afterwards clears the factor with margin.

## D-021 (2026-09-06) Rough conductor: GGX with visible-normal sampling, single scattering

- Decision: Trowbridge-Reitz NDF, height-correlated Smith G2, Schlick Fresnel with F0 = reflectance, perceptual roughness in [0.02, 1] with alpha = roughness^2, Heitz 2018 VNDF sampling; no multiple-scattering compensation (recorded loss).
- Evidence: T10 furnace cases match the CPU-integrated directional albedo within 2 % at roughness 0.05/0.35/0.70 and the closed form 1 - ln 2 at alpha 1; MIS and BSDF-only estimators agree to four digits.

## D-022 (2026-09-06) Ray offsets scale with the triangle's coordinate magnitude

- Issue: the RTG offset bounds the point's error only; a point near the origin on a 400 m triangle self-hit from below and lost 3.4 % of the furnace energy.
- Decision: `OffsetRayTri` adds 256 ULP of the largest vertex coordinate of the hit (or sampled) triangle along the normal. Sealed-room tests confirm no leaks with the larger offsets on 4 m slabs.
- Rollback: none needed; a future per-triangle error bound from the intersection algorithm could replace the heuristic.

## D-023 (2026-09-06) In-house strict JSON reader

- Decision: a small RFC 8259 parser in `lc_core` (no comments, no trailing commas, depth 64, line/column errors) serves replay files now and scene files next, keeping the dependency set empty.
- Rollback: pin nlohmann/json if the scene format outgrows it.

## D-024 (2026-09-06) Poses interpolated by parameters; history from rendered frames

- Decision: entities keep previous and current tick poses (position, yaw, pitch, door angle) and the render transform is built from the interpolated parameters; `Scene::CommitRenderedFrame` records the rendered transform as the previous one (spec §9). Placement events reset interpolation.

## D-025 (2026-09-06) Replay-driven tests with per-tick checks

- Decision: T05/T06 are replays with checks evaluated at a stop tick in a fresh process per check (reference mode for radiance, raw for identities). The committed replays are also run on the CPU by the unit tests, which verify poses and mirror facts before the GPU is involved.
- Consequence: a failing GPU replay test isolates the renderer, because the CPU test already proved the world state.

## D-026 (2026-09-06) Collision deferred to M5; interaction by view alignment

- Decision: the M3 controller has no collision (spec places collision in M5); interaction picks the candidate within 2 m that is closest to the view direction (cosine >= 0.6), so looking down at the lamp chooses it over the door.

## D-027 (2026-09-06) Light levels of the proof scene

- Decision: Room B's fixture is on an off circuit so the portable lamp is the dominant change there; the hall emergency fixture carries about a third of Room A's fixture power so the mirror image stays readable; the lamp face is small and bright (about a tenth of Room A's fixture power). Evidence captures of the dark rooms use `--exposure` 2..6 (display only; radiance checks read the linear image).

## D-028 (2026-09-06) NRD, ShaderMake, and MathLib as pinned git submodules built from source

- Issue: the spec (§3, §13, M4) directs an NRD integration; NRD's own CMake downloads ShaderMake, MathLib, and (through ShaderMake) a DXC release at configure time.
- Decision: the three repositories are git submodules at the commits NRD 4.17.3 pins (`external/`); `cmake/LcNrd.cmake` switches every fetch off, hands ShaderMake the SDK's `dxc.exe`, builds NRD as a static library with DXIL shaders only, and pins the normal/roughness encodings the shaders expect.
- Evidence: configure and build offline-equivalent (no download step in the logs); 31 DXIL blobs compiled by the SDK DXC; `NrdDenoiser` verifies the encodings at start-up.
- Consequence: NRD's proprietary RTX SDK license enters the dependency record with its attribution and redistribution terms (DEPENDENCIES.md) for the owner's review.

## D-029 (2026-09-06) REBLUR diffuse+specular as the single NRD method

- Decision: one denoiser (REBLUR_DIFFUSE_SPECULAR) suited to the signals we have (1 spp, one lobe per pixel, no probabilistic lobe split, no checkerboard); no SH variants, no RELAX, no SIGMA.
- Evidence: 7 dispatches per frame, about 1 ms in the Debug shader build at 1280x720; the analytic and box scenes within the documented tolerances.

## D-030 (2026-09-06) Primary Surface Replacement with a static mirror plane

- Decision: the guided trace describes the first non-mirror surface to the denoiser (virtual position on the primary ray, normal and motion reflected through the mirror planes in reverse order, mirror reflectance folded into the material factors). Mirror planes are taken as static between frames.
- Evidence: the mirrored threat's guide motion equals its reflected translation within 6.5e-5 m; the mirror image tracks the moving threat with zero measured lag.
- Rollback: a moving mirror needs the previous plane (NRD's `worldPrevToWorldMatrix` or a per-pixel previous-plane reflection).

## D-031 (2026-09-06) One global sub-pixel jitter per frame in denoised mode

- Decision: raw and reference modes keep per-pixel random jitter; the denoised mode applies one Halton(2,3) offset to every pixel and passes it to NRD. A temporal filter cannot be told per-pixel random offsets.

## D-032 (2026-09-06) World-space motion from the instance transforms

- Decision: motion vectors are `prevObjectToWorld * p - objectToWorld * p` of the hit's object-space point (exact for rigid motion, zero for static objects, camera motion excluded); NRD reprojects with the previous camera matrices. No 2D or 2.5D motion.
- Evidence: static pixels report exactly zero; the carried lamp reports its translation while the camera moves.

## D-033 (2026-09-06) Material factors stored, not recomputed

- Decision: the guided trace stores the demodulation factors it used; compose multiplies the denoised signals by the stored values. Divide and multiply always agree, whatever NRD's factor formula does.

## D-034 (2026-09-06) History bound and blur settings

- Decision: 30 frames of main history (0.5 s at 60 Hz), 6 fast; no pre-accumulation blur (it biased lighting gradients by 7–9 % permanently); maximum blur radius 30 px (shrinks with accumulation); the diffuse hit distance mixes in the light-sample distance where direct light dominates and uses it alone when the continuation leaves the scene.
- Evidence: the sharpest analytic case moved from -7 / -9 % to -3.6 / -4.0 %, inside the 5 % tolerance chosen beforehand; the closed box is within 1.5 %.
- Rollback: `--prepass-radius`, `--blur-radius`, `--history-frames` expose the values for the T12 review with a person.

## D-035 (2026-09-06) Trail lag as the automated temporal metric

- Decision: per-frame patch luminance and entity-pixel counts, a departure frame, a settled value 20–32 frames after departure, and the lag to reach 80 % of the step; limit 6 frames (100 ms). Invalid series (never leaves, too short, re-entry, no contrast) fail with a reason instead of passing.
- Consequence: the metric measures the reflected threat's silhouette specifically (spec §19: no global averages).

## D-036 (2026-09-06) Scene files describe kit parts and entities, not triangles

- Decision: schema 1 lists materials, circuits, objects as room-kit parts (slab, wall with opening, door leaf, emitter rectangle, box, quad), sockets, paths, markers, and the proof's entities; the loader builds the same kit calls the C++ builder used, so wall thickness, door overlap, and winding rules stay in code. Mesh files (the GLB subset) come later as another object kind.
- Consequence: the file cannot express baked lighting; validation is complete before any building; every error names its list, id, or field, and all errors are reported together.

## D-037 (2026-09-06) Reload never replaces a working scene with a broken one

- Decision: parse and validate first; the swap happens at the frame boundary after a full GPU wait (new world, upload, history reset, simulation restart). The asset root is `<executable dir>/assets`; scene paths that leave it are refused. The diagnostic panel (Dear ImGui) is deferred to M5 with the settings UI; reload results go to the log and the title.

## D-038 (2026-09-06) Dear ImGui drawn into the swap-chain image after the present copy

- Decision: the interface (prompts, objective line, controls card, pause menu with settings, diagnostic panel) is Dear ImGui v1.92.9b with its Win32 and D3D12 backends, compiled from the pinned submodule as `lc_imgui` (third-party code at `/W3`, not `/WX`); `lc_ui` wraps it behind `ui::Ui`. The window forwards every message to the backend first; the renderer exposes `RecordOverlay`, which records into the frame after the copy to the back buffer (PRESENT → RENDER_TARGET → PRESENT), so the linear output, captures, and validation never contain interface pixels. The backend compiles its two shaders with `D3DCompile` once at start-up (system `d3dcompiler_47.dll`); no other runtime shader compilation exists.
- Consequence: `--no-ui` removes the interface and the benchmark never creates it (its `overlay` timer would otherwise appear in the pass table); the menu releases the cursor and stops the simulation; settings apply the same frame (sensitivity, inverted look, field of view, exposure); the volumes and text cues are stored for the sound system.

## D-039 (2026-09-06) The placeholder body stays clear of the camera by placement

- Decision: spec §14 forbids camera-ray exclusion, so the torso box is centred 1.0 m up and 6 cm behind the eye axis (its top 0.32 m below the eye, its front face under the eye) and the hands rest in front of the waist (1.05 m up, 16 cm forward, 20 cm to the side). The floor stays visible down to 0.3 m from the feet; the chest enters the view only when the player looks almost straight down; the hands at pitches steeper than about 52 degrees.
- Consequence: the first placement (torso 5 cm in front of the eye, top at 1.38 m) hid the T06 floor patch one metre ahead and turned the check 4x darker; the fix moved the body, not the threshold. T13's reflected-torso identity check holds with either placement.

## D-040 (2026-09-06) Collision: yaw-only boxes, a capsule as a circle, substeps

- Decision: every kit box with `collider` on becomes a yaw-only oriented box from its mesh bounds (walls, slabs, furniture, the door leaf, which is updated per tick); the player is a vertical capsule of radius 0.3 m, height 1.7 m, and a 5 cm skin, resolved on the ground plane as a circle pushed out along the axis it entered from (the previous position decides the side, so a centre exactly on a face never crosses to the far side), in substeps of at most a quarter radius with four resolve iterations each; the carried lamp's housing sphere (radius 0.125 m) is swept from the eye toward its held pose and stops where it is free; a closing door that overlaps the player swings back open (`Door::Block`). Line-of-sight queries use segment tests against the same boxes.
- Consequence: no physics library; no vertical motion (the proof has one floor level, spec §14); entity-placed objects (body, lamp, threat) default to non-colliding so they never block their owner. The mirror replay route grazed the sink block by 10 cm, so the sink moved 20 cm (`two_room.json`), keeping the tolerances.

## D-041 (2026-09-07) Sound: generated clips, our own rules, miniaudio as the device layer

- Decision: every clip is synthesised in code from recorded parameters (`src/audio/clips.cpp`: tones with harmonics and wobble for the hums, low-passed noise bursts for footsteps and thuds, a grainy sweep for the creak, a motor tone with ticks for the machine, a crossfaded noise bed, chime and sting), so provenance is the repository itself (spec §15: "original, generated, or licensed assets with provenance"). The rules live in `audio::Director` and `ComputeMix` without a device: inverse-square attenuation clamped inside 1 m and silent past 30 m, pan from the listener's right axis, a documented occlusion factor of 0.3 when the collision solids (the door leaf included) lie between the eye and the source, hums that start and stop with the emitter state and move with the fixture transform (the lamp), footsteps every 0.62 m walked, door creak and thud on state edges, the machine's loop only while it moves (with a 0.25 s hold across tick-less frames), text cues for the important events. miniaudio only owns the device: a 24-voice pool of non-spatialised, pitch-free mono voices whose volume and pan the rules set every frame, two groups for the effects and ambience volumes, the engine volume for master and pause. No output device is not an error: the cues still flow.
- Consequence: the rules are unit-tested (`test_audio.cpp`); the library never decides a level or a side; occlusion is a level change without filtering (KI-024); the proof has no fan, so the fan clip is generated but unused (KI-025); `--no-audio` opens no device and headless runs never do.

## D-042 (2026-09-07) Input edges live until a tick reads them

- Decision: raw mouse deltas and key press edges stay in the window's record until a simulation tick consumed them; frame-level commands (F1, R) consume their own edges. Before this the record was cleared every frame, and at 144 frames per second more than half the frames run no 60 Hz tick, so key presses and mouse motion in those frames were lost (found when a posted F press did nothing in the sound check).
- Consequence: presses are never dropped at any refresh rate; the paused or unfocused game still clears the record each frame, so no burst of motion follows a resume.

## D-043 (2026-09-07) The machine's state machine runs on gameplay data; the visual tests keep the path

- Decision: `Threat` has two behaviours. `patrol` is the deterministic ping-pong path the M3/M4 replays and their tolerances were built on (spec §15: "the first visual test uses a deterministic path"). `hunt` adds chase, investigate, wait, and return: detection is `distance < range` (8 m when the player carries the lit lamp, 2.5 m otherwise), the player inside a 60-degree cone of the machine's facing, and `CollisionWorld::SegmentClear` from the machine's head to the player's eye (walls and the closed door block it; nothing reads an image). Chase moves straight at the player at 1.8 m/s sliding along solids; losing sight goes to investigate (the last seen position, 1.4 m/s, 4 s timeout, a stuck timer), then wait (2 s), then return to the closest point of the path (waypoints in turn when the straight line is blocked). Contact within 0.6 m in any state is a catch. Replays carry `"threat": "hunt"`; play defaults to hunt with `--threat patrol` as the override.
- Consequence: no navigation mesh (spec §15: "simple room and hall waypoints"); the straight-line moves can stall against corners (KI-026); the existing replays and their GPU tolerances are untouched; T14/T15 use hunt replays.

## D-044 (2026-09-07) Objective phases, checkpoints, and the restart

- Decision: the proof's objective is introduction → lamp acquired → lamp placed (the shelf socket only) → lamp retrieved → escaped (the `exit` marker of the scene file reached with the lamp held, `objectives: [{"kind": "exit", ...}]`, schema 1 extended). Each transition saves a checkpoint (phase, player pose, door state and angle, lamp state, switch, socket, pose); a catch restores it within the same tick, the machine goes back to the start of its path (never staged into view), the temporal history is reset (a camera cut), and the sting plays with its text cue. The pause menu's Restart restores the same checkpoint, or the whole route after "escaped".
- Consequence: no progress outside the demo is touched and no process restart is needed (spec §15); the introduction checkpoint is the initial state; the objective line, chime, and end card come from the phase.

## D-045 (2026-09-07) T14 by a CPU-only run and a world-state hash

- Decision: `--simulate-only` runs a replay through the same `World`, `Simulation`, and state checks with no device, window, or renderer, and prints an FNV-1a hash over every tick's objective phase, threat state and pose, player pose, lamp, door angle and state, and catches. Rendered runs take `--expect-state-hash` and fail when their hash differs. The T14 tests run the route replay in raw and denoised modes, at exposure 0.25, and at a 960x540 internal size against the CPU-only constant, so exposure, resolution, and reconstruction provably leave the rules alone.
- Consequence: the constants are goldens regenerated on purpose only; the game layer's independence from rendering is a tested property, not a claim.

## D-046 (2026-09-07) Scene file schema 2: lists of doors, items, sockets, powered circuits, the fan, objective steps

- Decision: schema 2 replaces the proof's singular entities with lists. `entities.doors[]` (id, object, `locked`, `opensWithCircuit`), `entities.items[]` (id, text, a plain `object` or a lamp's `housing` + `face` + `light`, `startSocket`, `hidesWhenCarried`), `sockets[]` with `accepts` (item ids) and a prompt `text`, `circuits[]` with an optional `poweredBy {item, socket}` (the initial `on` must agree with the item's start socket), the object kind `fan` (centre, axis, radius, blade count, width, thickness, hub radius, rpm, circuit; the loader generates the hub and the blades as separate instances `<id>_hub`, `<id>_blade<n>`), `objectives[]` as ordered steps of kind `take` / `place` / `reach` (item, socket, marker and radius, `requires`, text) whose ids are the phase names, and `objectiveComplete`. Every id is cross-checked (a door leaf without a door entity, a socket that accepts an unknown item, a `poweredBy` that names a socket which does not accept the item, are errors that name their list and field). `assets/scenes/two_room.json` moved to schema 2 with the same geometry and ids.
- Consequence: the level and the world hold vectors; the proof's replays behave exactly as before (the M5 hashes were regenerated once because the hash now covers every door, item, fan, and circuit, and the events kept their ticks); a new level needs no code for its doors, items, sockets, circuits, or steps.

## D-047 (2026-09-07) Circuits follow item placement; fixtures, the fan, and doors follow their circuit

- Decision: `World::EvaluateCircuits` runs every tick after the items: a circuit with `poweredBy` is on exactly when its item sits in that socket. A change switches the circuit's emitter materials (`Scene::SetEmitterOn`, so sampling and sound follow the same state), sets the fan's target speed, and opens a door with `opensWithCircuit` when the circuit turns on. A `locked` door is never offered by the prompt and ignores E; it opens only through its circuit and stays open when the circuit dies. The same evaluation runs at construction and after a checkpoint restore.
- Consequence: spec §15's "removing the fuse changes circuit state ... the same state drives fixture emission, source data, fixture sound, and relevant animations" is one code path with no scripted light; the checkpoint captures item placement and the circuits derive from it.

## D-048 (2026-09-07) The fan: boxes rotated by the world with a linear spin-up and spin-down

- Decision: the fan's hub and blades are ordinary boxes (`fan_metal`, a rough conductor) whose transforms the world rewrites every tick while the angle changes: `Translation(centre) * RotationAxis(axis, angle) * local`. The angular speed follows the circuit with a linear 3 s ramp (`Fan::kSpinSeconds`) both ways, so the angle after the ramp is the integral of the ramp (a check at tick N of the running fan expects `pi * (N - 89.5) / 60` for 30 rpm; the rest angle after a pull at tick P is `pi * P / 60` because the two integrals are symmetric). The fan loop's level is the speed fraction; a stopped fan is silent (spec §15).
- Consequence: the blades cast moving shadows from the real fixture behind them (spec §5) with no special rendering; the angle is part of the state hash and the checkpoint; a stopped fan costs no transform writes.

## D-049 (2026-09-07) Hall routing along the patrol polyline instead of a navigation mesh

- Decision: when an off-path move (chase lost, investigate, return) is blocked at body height (`CollisionWorld::SegmentClear` 0.6 m up), `Threat::PlanRoute` walks the patrol polyline between the closest points of the start and the target and follows the intermediate vertices (`FollowRoute`; a leg that stays blocked for 1.5 s is skipped). A straight clear line is still taken directly. `Threat::Teleport` and `BeginReturn` exist for tests that stage the machine.
- Consequence: spec §15's "simple room and hall waypoints" without a mesh; the machine returns from the Plant doorway to Hall B around the corner in 58 ticks in the CPU test; KI-026 is closed for the halls (a room interior still uses the straight line and the stuck timer).

## D-050 (2026-09-07) The static mirror check looks through an open door

- Decision: `entities.mirror.openDoor` names a door the static scene opens for the mirror aim and the `gpu_six_room_static_mirror` check; the world still starts with every door closed (the objective opens it). The six-room mirror is aimed from the inspection check pose through the inspection doorway at the hall point the machine crosses; its centre sits at 1.3 m so the reflected ray from the 1.6 m eye descends onto the machine's body.
- Consequence: the T05 evidence exists for the demo without a replay; the mirror stays a plain mirror box.

## D-051 (2026-09-07) Pocket items: an item that hides when carried never needs the hand

- Decision: one item in the hand at a time (a held item that does not hide when carried); an item with `hidesWhenCarried` goes into the pocket (parked inside the torso box) and can be taken while the hand is full; a socket's prompt and placement act on whichever held item it accepts. The first M6 rule ("one held item") refused the fuse while the lamp was carried, which contradicts spec §15's phase order (fuse carried after lamp acquired, the escape with the lamp).
- Consequence: the lamp stays in the hand through the fuse route; `Take the fuse` and `Put the fuse in the exit panel` are offered with the lamp held; the CPU test pins both rules and the pocketed item's transform is rewritten only when the player moves (an every-frame rewrite counted as motion: a TLAS rebuild each frame and a reference accumulation that never converged, found by the first frozen render after the fuse pull).

## D-052 (2026-09-07) Image checks of long replays run frozen at their tick in reference mode

- Decision: a replay's image checks are evaluated on the final image of a run, so a check at tick N runs as `--stop-at-tick N --mode reference --spp 64` (the world advances on the CPU, one converged render is checked). A run stopped early on purpose leaves the state checks beyond its stop tick unevaluated and reports them as such instead of failing; a state check inside the run that never fired is still a failure. Patch thresholds were set before the reference runs (lit floors > 0.01, the emergency wall > 0.005, the vestibule > 0.02, an emitting fixture face > 1.0, dark surfaces < 1e-3, a dark fixture face < 0.01) and are recorded with the measured values in docs/TESTS.md.
- Consequence: fourteen frozen runs of a second each replace one long noisy run: the recomposed 1 spp raw mean of a 5x5 patch in denoised mode read 0.010 on a floor whose converged value is 2e-5 (one bright sample), which would have made every dark check meaningless; per-frame image evaluation inside a single run was not built.

## D-013 (2026-09-06) Repository workflow for this session

- Decision: work on branch `m0-m1-bootstrap` with small commits; nothing is pushed; `build/` and `artifacts/` are ignored. The owner decides on merging.
