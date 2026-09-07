# Last Circuit M3.5 Scene Files Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the two-room level out of C++ into a versioned JSON scene file with stable identifiers (objects, sources, circuits, sockets, waypoints, markers, collision flags, objective placeholders), load and validate it with limits and an asset root, make it reloadable at a safe frame boundary without ever destroying the working scene, and record its content hash in every capture and benchmark (spec §3 "Versioned JSON with stable object identifiers", §16).

**Architecture:** `src/scene/scene_file.*` parses a document with the strict JSON reader, validates it completely (schema version, identifiers, references, finite values, counts, sizes, transforms, path containment), and only then builds a `TwoRoomLevel` through the existing room kit (the same functions the C++ builder used, so every generated part keeps its documented thickness and winding). `BuildTwoRoomLevel()` loads `assets/scenes/two_room.json` from the asset root; the assets directory is copied beside the executable by the build so the packaged layout matches development. The application gains `--scene-file <path>` (inside the asset root) and, in play mode, an R-key reload: parse and validate first, then at the frame boundary wait for the GPU, rebuild the world and upload the scene, and reset the temporal history. Failures log every problem and keep the current scene.

**Tech Stack:** unchanged (in-house JSON reader/writer). No Dear ImGui yet: the diagnostic panel is deferred to M5 with the settings UI (recorded in STATUS.md); reload results go to the log and the window title.

**Spec:** `LAST_CIRCUIT_STARTER.md` §3 (scene description), §16 (asset and scene pipeline: schema version, validation list, limits, asset root, safe reload, content hash), §17 (no asset download during play), §21 (unknown options fail).

## Global Constraints

- "A scene file describes objects, sources, circuits, interaction sockets, collision shapes, waypoints, and objective events. It must not contain baked lighting corrections." (§16)
- "Use a schema version. An invalid scene must not destroy the previous working scene during hot reload. Parse and validate first, then replace the scene at a safe frame boundary." (§16)
- "Validate indices, finite values, file bounds, object counts, texture sizes, duplicate identifiers, emitter references, and supported transforms. Apply reasonable input-size limits. Scene paths must stay within the approved asset root." (§16)
- "Record a content hash for scenes, meshes, materials, and shaders in benchmark metadata." (§16) — extended to captures.
- Generated kit parts only; the GLB subset is a later task (§16).

## Schema (version 1), `assets/scenes/two_room.json`

```json
{
  "schema": 1,
  "name": "two_room",
  "units": "metres",
  "materials": [
    {"id": "painted_concrete", "type": "diffuse", "reflectance": [0.55, 0.55, 0.52]},
    {"id": "mirror", "type": "mirror", "reflectance": [0.9, 0.9, 0.9]},
    {"id": "fixture_a", "type": "emitter", "radiance": [6.0, 5.8, 5.4], "reflectance": [0.02, 0.02, 0.02], "circuit": "a"},
    {"id": "steel", "type": "rough_conductor", "reflectance": [0.9, 0.9, 0.9], "roughness": 0.3}
  ],
  "circuits": [{"id": "a", "on": true}, {"id": "hall", "on": true}, {"id": "b", "on": false}, {"id": "lamp", "on": true}],
  "objects": [
    {"id": "floor", "kind": "slab", "min": [-0.15, -0.15, -0.15], "max": [8.15, 0.0, 12.3], "material": "rubber_floor"},
    {"id": "a_wall_pos_x", "kind": "wall_opening", "min": [4.0, 0.0, 0.0], "max": [4.15, 2.8, 4.0], "axis": "z", "opening": {"centre": 1.2, "width": 0.9, "height": 2.1}, "material": "painted_concrete"},
    {"id": "door", "kind": "door_leaf", "hinge": [4.0, 0.0, 0.75], "widthDir": [0, 0, 1], "thicknessDir": [1, 0, 0], "width": 0.9, "height": 2.1, "material": "painted_metal_door"},
    {"id": "fixture_a", "kind": "emitter_rect", "width": 0.8, "height": 0.8, "position": [2.0, 2.79, 2.0], "facing": [0, -1, 0], "material": "fixture_a"},
    {"id": "crate", "kind": "box", "half": [0.4, 0.4, 0.4], "centre": [0.8, 0.4, 3.0], "material": "crate"},
    {"id": "mirror", "kind": "box", "half": [0.6, 0.8, 0.01], "centre": [6.4, 1.5, 12.0], "yaw": "auto_mirror", "material": "mirror", "collider": true},
    {"id": "lamp_housing", "kind": "box", "half": [0.05, 0.05, 0.10], "material": "lamp_housing", "collider": false},
    {"id": "lamp_face", "kind": "quad", "half": [0.03, 0.03], "material": "lamp_face", "collider": false}
  ],
  "sockets": [{"id": "floor_a", "position": [3.0, 0.0, 0.9], "yaw": -1.5707963}, {"id": "shelf", "position": [4.25, 0.95, 10.25], "yaw": -1.5707963}],
  "paths": [{"id": "threat_route", "points": [[4.6, 0, 1.9], [7.7, 0, 1.9]], "speed": 1.2, "mode": "ping_pong"}],
  "markers": [{"id": "player_start", "position": [2.5, 0, 1.2], "yaw": -1.5707963}, {"id": "mirror_check_camera", "position": [5.2, 0, 10.3], "yaw": 3.1415927}, {"id": "threat_check", "position": [7.0, 0, 1.9]}],
  "entities": {
    "player": {"start": "player_start"},
    "door": {"object": "door"},
    "lamp": {"housing": "lamp_housing", "face": "lamp_face", "faceOffset": 0.101, "material": "lamp_face", "startSocket": "floor_a"},
    "threat": {"body": "threat_body", "head": "threat_head", "path": "threat_route", "parkAt": "threat_check"},
    "mirror": {"object": "mirror", "checkCamera": "mirror_check_camera", "aimAt": "threat_check"}
  },
  "objectives": []
}
```

Rules: identifiers are unique within their list and match `[a-z0-9_]+`; every reference resolves; numbers are finite; a material's `circuit` is required for emitters and forbidden otherwise; `on` of a circuit sets the initial state of its emitters (the `SetEmitterOn` calls of today); the mirror's `"yaw": "auto_mirror"` keeps the derived aim (from `checkCamera` eye and `aimAt`) that the C++ builder computes, an explicit number is also allowed; `collider` defaults to true for slabs, walls, doors, and boxes and to false for quads and emitter rectangles (M5 reads it); objects that entities move (`lamp_housing`, `lamp_face`, `threat_body`, `threat_head`, `door`) have no `centre`/pose of their own beyond their kit parameters: the entity places them.

Limits (spec §16 "reasonable input-size limits"): file <= 4 MiB, <= 4096 objects, <= 256 materials, <= 64 circuits, <= 64 sockets, <= 32 paths with <= 256 points each, <= 128 markers; every dimension within [1e-4, 1000] m; radiance within [0, 1e6]; total triangles under the existing scene validation (`kMaxMeshTriangles` per mesh, instance ids < 2^24).

Path containment: `--scene-file` is resolved against the asset root (`<exe dir>/assets`, or `LC_ASSET_ROOT` for the CPU tests) and rejected when the canonical path leaves it.

## File Structure

```
assets/scenes/two_room.json               the level (the only scene file for now)
src/scene/scene_file.h/.cpp               SceneFileLimits, SceneFileResult { std::optional<TwoRoomLevel> level; std::vector<std::string> errors; std::uint64_t contentHash; }, LoadSceneFile(path, assetRoot), ParseSceneFile(text, sourceName), AssetRoot()
src/scene/two_room_level.cpp              BuildTwoRoomLevel() = LoadSceneFile(AssetRoot()/"scenes/two_room.json") with the test expectations derived from markers (the kit calls move into scene_file.cpp)
src/scene/CMakeLists.txt                  copies assets/ beside the executable (custom command), LC_ASSET_ROOT for tests
src/app/options.*, application.cpp        --scene-file; play-mode R = reload (parse/validate, then swap at the frame boundary with ResetHistory; keeps the old scene on failure); content hash in capture metadata and logs
tests/cpu/test_scene_file.cpp             golden load of two_room.json (ids, counts, circuits, sockets, markers, mirror aim); each validation rule rejected with a message naming the id/field; limits; path containment; a reload-style double load produces equal content hashes
tests/gpu/CMakeLists.txt                  the M3/M4 two_room tests now exercise the JSON level; gpu_scene_reload: --play is windowed, so a headless `--reload-test` runs load -> swap -> render -> validate twice
docs/ARCHITECTURE.md, BUILD.md, TESTS.md, DECISIONS.md, STATUS.md, KNOWN_ISSUES.md, README.md
```

### Task 1: Loader and validation (CPU only)

- [ ] `scene_file.h/.cpp`: parse with `json::Parse`; collect *every* problem into `errors` (do not stop at the first); build only when there are none. Kit calls exactly as in `two_room_level.cpp` today (`AddSlab`, `AddWallWithOpening`, `AddDoorLeaf`, `AddRectangleEmitter`, `AddBox`, `MakeQuadXZ`). Content hash = FNV-1a of the file bytes combined with `Scene::ContentHash()` of the built scene.
- [ ] `assets/scenes/two_room.json` authored from the C++ builder (numbers copied, not re-derived).
- [ ] `test_scene_file.cpp`: the golden load reproduces today's level (instance names and count, material names and types, circuits' emitter states, sockets, path, markers, `mirrorNormal` within 1e-6 of the C++ derivation kept as a test oracle until the builder is deleted); every rule; limits; containment.
- [ ] Commit: `feat(scene): versioned JSON scene files with full validation; two_room.json`.

### Task 2: The level comes from the file

- [ ] `BuildTwoRoomLevel()` loads the file; the builtin scene registry and the world use it; the CPU tests get `LC_ASSET_ROOT`; the build copies `assets/` to `bin/assets/`.
- [ ] All 44 tests pass in both presets (they now run on the JSON level).
- [ ] Commit: `refactor(scene): two_room level loaded from assets/scenes/two_room.json`.

### Task 3: Reload command and scene-file option

- [ ] `--scene-file <path>` (validated against the asset root); `--reload-test` (headless: render, reload, render, validate both; exit 1 when the reload fails or changes the content hash of an unchanged file).
- [ ] Play mode: R reloads. Order: parse + validate (log every error on failure and keep going with the old scene) -> `queue.WaitIdle()` -> new `World` -> `renderer.SetScene` -> `ResetHistory` -> title shows the file's hash and the reload count.
- [ ] Capture metadata: `sceneContentHash`, `sceneFile`.
- [ ] GPU test `gpu_scene_reload_test`; docs; STATUS with the deferred diagnostic panel recorded.
- [ ] Commit: `feat(app): scene reload at the frame boundary; --scene-file; content hashes in captures`.
