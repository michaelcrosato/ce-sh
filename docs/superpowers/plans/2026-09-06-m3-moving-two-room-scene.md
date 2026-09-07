# Last Circuit M3 Moving Two-Room Scene Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the world move under deterministic control: a fixed-step simulation with recorded/replayed input, a first-person camera, a hinged door with explicit states, a portable lamp fixture that can be carried, placed on a shelf, and switched, and a rigid threat test object on a fixed hall path; build the spec §4 two-room scene from the generated kit; prove through replays that the real object is visible in the mirror outside direct view and that door and source changes are correct in raw and reference modes (spec M3 gate, T05, T06).

**Architecture:** A `game` library (no D3D12 types) owns the `World`: player pose, door state machine, lamp fixture state, threat path, and a fixed 60 Hz `Simulation` driven by per-tick `InputFrame`s. Each rendered frame the world writes interpolated poses into the `Scene` (which keeps the previously rendered transform), so the renderer's TLAS rebuild-on-change and motion history are exercised by real motion. Input comes from Raw Input and keyboard messages, or from a replay file; a recorder writes the same file. Replays carry checks (hit identities and radiance patches at given ticks) that `--validate` evaluates by stopping the simulation at a tick and rendering in raw or reference mode. A small strict JSON reader in `lc_core` serves the replay format now and the scene files later.

**Tech Stack:** unchanged. New `lc_game` static library; `src/core/json_reader.*`.

**Spec:** `LAST_CIRCUIT_STARTER.md` §4 (first proof), §9 (frame order, motion history), §14 (movement, interaction, doors; collision deferred to M5), §15 (threat test object on a deterministic path), §16 (versioned JSON: replay first, scenes in the follow-up plan), §19 T05/T06, §20 M3 gate, §21 (`--replay`).

## Global Constraints (in addition to the M1 and M2 plans)

- Fixed simulation step, initially 60 updates per second (§3); previous rendered transforms refer to the previous image, not the previous tick (§9).
- Interpolated current and previous render data; camera cuts, teleports, scene reloads, and resolution changes invalidate history (§9).
- Doors have explicit open, closed, and moving states; collision, ray-tracing transform, and sound follow the same state (§14).
- The lamp is one fixture: an emitting face plus an opaque housing under one transform; the visible lamp, interaction bounds, source, and audio position share that transform (§7, §14). The housing may block the source; the source is not excluded from visibility rays.
- The threat is a rigid low-polygon test object on a deterministic path that occupies the real hall outside the camera view while remaining visible in the mirror (§4, §15). No duplicate threat for the mirror (H04).
- Deterministic replay for testing; the mirror event must remain understandable during motion (§4).
- Controls: WASD, mouse look, Shift sprint, E interact, F lamp switch, Escape pause/release (§14); pause safely on focus loss; no head bob.
- Unknown options fail; `--replay <file>` is a required command (§21).

## Definitions

- **Tick**: one 1/60 s simulation step. `InputFrame { float moveX, moveZ; float lookDx, lookDy (radians); bool sprint, interactPressed, lampPressed; }`.
- **Pose**: `{ Vec3 position; float yaw; float pitch; }` for the player; `{ Vec3 position; float yaw; }` for rigid objects; the door has `angle`. Render transforms are built from interpolated pose parameters, never by interpolating matrices.
- **Replay file** (`version: 1`): scene name, tick rate, seed, a list of input segments `{ fromTick, toTick, input }`, and a list of checks `{ tick, kind, ... }`. Recording writes segments by run-length encoding identical consecutive inputs.

## File Structure

```
src/core/json_reader.h/.cpp           Strict JSON parser to a small DOM (object, array, string, number, bool, null) with line/column errors.
src/game/CMakeLists.txt               lc_game (depends on lc_scene, lc_core).
src/game/input.h                      InputFrame, key bindings enum.
src/game/replay.h/.cpp                Replay { segments, checks }; Load/Save; InputAt(tick).
src/game/simulation.h/.cpp            Fixed-step clock: Advance(realSeconds, inputProvider) -> ticks run, interpolation alpha.
src/game/player.h/.cpp                First-person controller: walk 1.5 m/s, sprint 2.6 m/s, mouse look with clamped pitch, eye height 1.6 m; no collision (M5).
src/game/door.h/.cpp                  Door { Closed, Opening, Open, Closing }, 90 degrees in 1.0 s, hinge transform from the room kit.
src/game/lamp.h/.cpp                  Lamp fixture: housing box + emitting face instances, held/placed states, socket poses, on/off through Scene::SetEmitterOn.
src/game/threat.h/.cpp                Waypoint path at constant speed with yaw facing the travel direction; loops or ping-pongs.
src/game/world.h/.cpp                 World: owns the entities, interaction query (2 m reach, facing), Tick(InputFrame), WriteRenderScene(Scene&, alpha).
src/scene/two_room.h/.cpp             BuildTwoRoomScene(): the §4 layout on the generated kit, returning entity handles (door, lamp, threat, sockets, mirror).
src/platform/window.h/.cpp            Raw Input registration, WM_INPUT mouse deltas, key state, cursor capture/release, focus events.
src/app/options.*, application.cpp    --record, --replay, --stop-at-tick, --play; windowed loop with simulation; replay checks in --validate.
tests/cpu/test_json_reader.cpp, test_replay.cpp, test_simulation.cpp, test_world.cpp, test_two_room.cpp
tests/replay/*.json                   Committed replays for T05 and T06.
tests/gpu/CMakeLists.txt              T05/T06 replay tests in raw and reference modes.
docs/ARCHITECTURE.md, RENDERING.md, TESTS.md, STATUS.md, DECISIONS.md, KNOWN_ISSUES.md
```

## Two-room layout (§4), metres, +Y up

```text
z
12.15  +----------------+                 Room B (inspection) inner x 4.0..8.0, z 8.15..12.15
       |  mirror (45°)  |                 sink block near the +X wall, lamp shelf on the -X wall
       |                |                 open doorway in the z = 8.15 wall centred at x = 6.8
 8.15  +------+   +-----+
              | H |                       Hall segment 2 inner x 5.6..8.0, z 2.4..8.15
              | a |
              | l |
 2.40  +------+ l +-----+                 Hall segment 1 inner x 4.15..8.0, z 0..2.4 (right-angle turn at x 5.6..8.0)
       |  Room A   door |
       |  (equipment)  ||                 Room A inner x 0..4, z 0..4; door in the x = 4 wall centred at z = 1.2
 0.00  +---------------+-----+
       0               4   5.6  8.0    x
```

The threat walks hall segment 2 from (6.8, 0, 3.0) to (6.8, 0, 7.5) and back. The camera in Room B at (5.0, 1.6, 10.5) looking at the mirror on the z = 12.15 wall cannot see the doorway directly; the mirror, rotated so its normal points toward the doorway/hall, reflects the hall through the open doorway. Exact mirror angle and the pixel used for the T05 check are computed with `Camera::ProjectToImage` and verified by the CPU ray caster in `test_two_room.cpp` before the GPU test exists.

Walls 0.15 m, ceiling 2.8 m, door 0.9 x 2.1 x 0.04 m hinged on the z = 0.75 edge swinging into the hall. Fixtures: Room A ceiling panel (circuit "a"), hall emergency fixture (independent supply, small, dim), Room B ceiling panel (circuit "b"), the portable lamp (own supply). At most 4 active emitters.

---

### Task 1: JSON reader
- `lc::json::Value` variant DOM; `Parse(std::string_view, std::string& error) -> std::optional<Value>`; accessors `Get(key)`, `At(index)`, `AsNumber()`, `AsString()`, `AsBool()`, `IsNull()`; depth limit 64; UTF-8 strings with `\uXXXX` escapes (BMP), no comments, no trailing commas.
- Tests: round trip of `JsonWriter` output; rejects trailing commas, bare words, unterminated strings; numbers with exponents; nested depth; error message contains line and column.
- Commit `feat(core): strict JSON reader`.

### Task 2: Simulation clock, input frames, replay files
- `Simulation { tickRate = 60; Advance(double realSeconds, std::function<InputFrame(uint64 tick)>) -> struct { uint32 ticksRun; float alpha; }; Tick() const; }` with an accumulator capped at 0.25 s (spiral-of-death guard, logged).
- `Replay { Load(path) / Save(path); InputAt(tick) (last segment covering the tick, else neutral); checks }`; recorder appends and merges identical consecutive inputs.
- Tests: fixed steps are exact (advancing 1.0 s runs 60 ticks, alpha 0); run-length recording and lookup; save/load round trip; unknown version rejected.
- Commit `feat(game): fixed-step simulation and replay files`.

### Task 3: Raw Input and keyboard in the window
- `RegisterRawInputDevices` for the mouse (usage page 1, usage 2) on the window; `WM_INPUT` accumulates `lastRelX/Y`; `WM_KEYDOWN/UP` maintain a 256-entry key state and "pressed this pump" edges; `CaptureCursor(bool)` clips and hides the cursor; focus loss releases it and zeroes deltas; `WM_MOUSEMOVE` is not used for looking.
- `Window::PollInput() -> RawInputState { mouseDx, mouseDy; bool keyDown[256]; bool keyPressed[256]; }`.
- Manual check: `--play` moves the camera; Escape releases the cursor; alt-tab pauses.
- Commit `feat(platform): raw mouse input, key state, cursor capture`.

### Task 4: World: player, door, lamp, threat
- `Player::Tick(const InputFrame&, float dt)`: yaw -= lookDx, pitch clamped to +/-85 degrees, move in the yaw plane, speeds as above; pose interpolation for rendering.
- `Door` state machine; `Interact()` toggles Opening/Closing; `Angle(alpha)` interpolated; the door's scene instance transform = `DoorTransform(handle, angle)`.
- `Lamp` fixture: `Scene` instances `lamp_housing` (box 0.10 x 0.10 x 0.20 m, diffuse dark grey) and `lamp_face` (0.06 x 0.06 m rectangle emitter, radiance (6, 5.6, 5)); states Held (pose = player eye pose with offset (0.25, -0.2, -0.4) in camera space, aimed with the camera) and Placed (pose = socket); `Toggle()` calls `Scene::SetEmitterOn`; `PickUp/Place` via the interaction query when within 2 m and facing.
- `Threat`: waypoints, speed 1.2 m/s, ping-pong; scene instance `threat_body` (box 0.5 x 1.2 x 0.4 m, diffuse (0.15, 0.15, 0.18)) plus `threat_head` (box 0.3 m) under one pose.
- `World::WriteRenderScene(Scene&, float alpha)`: sets transforms only when a pose changed (so unchanged frames keep the TLAS), then the app calls `CommitRenderedFrame` after presenting.
- Tests: door angle over time and state transitions; lamp held pose follows the camera; placing snaps to the socket and picking up restores Held; threat position at t = 0, mid, end and ping-pong; a full tick with neutral input changes nothing except moving entities; `prevObjectToWorld` of the threat equals the transform rendered in the previous frame after one commit.
- Commit `feat(game): player, door, lamp fixture, threat path, world`.

### Task 5: Two-room scene
- `BuildTwoRoomScene()` on the room kit: rooms, hall slabs, doorways, the door (Room A), mirror slab in Room B, sink block, shelf with a socket pose, crate, three fixtures plus the lamp fixture (starts on the floor of Room A at (1.0, 0.0, 3.0) as Placed on a floor socket), threat path, camera start in Room A at (2.0, 1.6, 2.0) facing the door.
- Tests (CPU ray caster): from the Room B check position the doorway is not directly visible but a mirror pixel sees the hall floor at z = 5 (through the mirror) when nothing is there; with the threat placed at (6.8, 0, 5.0) the same pixel's first non-mirror hit is `threat_body`; the closed door blocks a ray from the Room A fixture into the hall; the open door does not; total triangles below 1,000; at most 4 active emitters.
- Commit `feat(scene): two-room proof scene`.

### Task 6: Application integration and replay checks
- Options: `--play` (windowed, live input, cursor captured), `--record <file>` (with `--play`), `--replay <file>`, `--stop-at-tick N` (advance the simulation exactly N ticks with replay input, then render; needed for reference mode and validation), `--scene two_room`.
- Frame loop (spec §9 order): pump input -> simulation advance (real time in `--play`, exactly one tick per frame in `--replay` unless stopped) -> `WriteRenderScene(alpha)` -> renderer (raw/reference/diag) -> present -> commit history.
- Replay checks evaluated in `--validate` at the stop tick: `hit` (pixel from a world point or `[u, v]`, expected entity name -> stable id, "first non-mirror hit"), `patch_positive`, `patch_zero`, `patch_ratio` (two ticks recorded in one run are not needed: each check runs in its own process with `--stop-at-tick`).
- Log line per frame group: number of TLAS rebuilds; `--validate` asserts rebuild count == number of frames in which something moved (motion frames) when stopping.
- Commit `feat(app): play, record, replay, stop-at-tick, replay checks`.

### Task 7: T05/T06 replays and GPU tests
- `tests/replay/t05_mirror_threat.json`: camera walks from Room A through the door and the hall into Room B (scripted input segments), turns to the mirror; checks at tick A (threat at z = 3.0, outside the mirror's view of the doorway: mirror pixel sees the hall wall) and tick B (threat crossing z = 5.0: mirror pixel's first non-mirror hit is `threat_body`, and the threat is *not* visible directly: no pixel outside the mirror reports it).
- `tests/replay/t06_door_light.json`: camera stays in Room B; the door (Room A) is opened by a replay interaction at tick 30 and closed at tick 400; checks: a hall floor patch visible in the mirror is Positive at tick 200 and Zero (below 1e-6... but the hall emergency fixture is on; use a patch lit only by Room A's fixture through the door) at tick 600; the lamp switched off at tick 700: the shelf patch changes.
- CTest: each check runs as `LastCircuit --scene two_room --replay <file> --stop-at-tick N --mode reference --spp 64 --validate --headless` and once in `--mode raw` (identity checks only); the TLAS-rebuild assertion runs in the raw variant with `--frames`.
- Commit `test(gpu): T05 mirror identity in motion and T06 door occlusion replays`.

### Task 8: Docs and status
- ARCHITECTURE.md: game layer, frame order with simulation, interpolation and history; RENDERING.md: motion history data (prev transforms) and reset rules; TESTS.md: replay format and the new tests; STATUS.md; DECISIONS.md (D-023 JSON reader in-house, D-024 pose interpolation by parameters, D-025 replay-driven tests, D-026 collision deferred to M5); KNOWN_ISSUES (no collision yet, no interpolation of the camera cut).
- Commit `docs: M3 records`.

## Follow-up plan (M3.5): versioned JSON scene files and reload
Objects on the generated kit (`box`, `quad`, `room`, `doorway`, `door`, `rectangle_emitter`), materials, instances with stable ids, sockets, circuits (emitter groups), waypoints; `--scene file.json`; validation rules of §16 (bounds, counts, unsupported features); reload command in the diagnostic panel (ImGui evaluation).

## Self-review
- Spec coverage: §4 sequence steps 1-3 (lamp on shelf changes light: Task 4/7 T06 lamp check; object crosses the hall and appears in the mirror: T05; closing the door blocks light and reflected view: T06); step 4 (shadow across a wall before contact) is M5 staging. §9 frame order and history: Tasks 2, 4, 6. §14 controls, pause on focus loss: Tasks 3, 6; collision: deferred (M5, recorded). §15 deterministic path: Task 4. §16 replay JSON: Tasks 1, 2; scene JSON: follow-up. §21 `--replay`: Task 6.
- Placeholders: none. Types named consistently: `InputFrame`, `Simulation`, `Replay`, `Player`, `Door`, `Lamp`, `Threat`, `World`, `BuildTwoRoomScene`.
