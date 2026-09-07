# Last Circuit M5 Playable Proof Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A person can play the two-room proof without console commands or explanations: collision keeps the camera and the carried lamp out of walls, the player has a placeholder body that shows in the mirror, prompts and a pause menu exist on screen, sounds follow the world state, the threat runs a small state machine with a catch and a checkpoint restart, and a short objective can be completed, failed, and restarted (spec M5 gate; T13, T14, T15). The image and performance gates of M4 stay passed.

**Architecture:** Collision is an in-house capsule-versus-box solver over the scene file's collider instances (world-space oriented boxes from the kit parts, the door leaf following its state); the visible shell and the collision solid are the same box, so the agreement tolerance is the leaf's overlap/inset. The lamp placement volume is swept toward the player when held. A rigid placeholder body (torso box, two hand boxes) follows the player pose below and beside the camera; it is ordinary geometry (visible in the mirror, casting shadows), never camera-excluded. UI is Dear ImGui (pinned submodule, MIT) rendered into the swap-chain image after the present copy; it draws prompts, the pause menu, settings (sensitivity, invert, FOV, volumes, exposure), the reload result, and the diagnostic panel of §16. Sound is miniaudio (pinned submodule, MIT-0/public domain) playing procedurally generated clips (provenance: generated in code) with distance attenuation, stereo panning, and a documented wall-occlusion factor from a visibility ray. The threat gets wait/patrol/investigate/chase states driven by gameplay data only (lamp on, distance, facing, line of sight through a ray query on the CPU ray caster or a GPU query result cached per tick — the CPU caster over the collider boxes keeps the rule independent of rendering). The objective state machine for the proof: introduction -> lamp acquired -> lamp placed (inspection) -> lamp retrieved -> escaped (reach the exit marker with the lamp); a catch restarts from the last checkpoint (objective state + player start of that phase). All of it is exercised by replays with checks (T13 collision and reflection identity, T14 rule invariance under exposure/resolution/mode, T15 restart and completion) and by a person at the controls (recorded honestly in STATUS.md).

**Tech Stack:** unchanged plus Dear ImGui and miniaudio as pinned submodules (licenses recorded before use).

**Spec:** §4 (sequence, player abilities), §14 (movement, placement, body, interaction, doors), §15 (threat, objective, sound), §16 (scene file: collision shapes, objective events), §19 T13–T15, §20 M5 gate, §21 (restart), §22.

**Status (2026-09-07):** Tasks 1–5 implemented, tested, and committed on `m0-m1-bootstrap`
(collision and body; interface; sound; rules with T14/T15). Deviations from the text below, all
recorded in `docs/DECISIONS.md` and `docs/KNOWN_ISSUES.md`: the T13 collision proof is the CPU
test suite plus every replay running against the solids (no `t13_walls.json`); the body sits
behind the eye axis, not 0.15 m in front (D-039); the panel shows the reload result but has no
buttons (the blackout hook stays a command-line option); occlusion is a level factor without
filtering; the T14 invariance check is a world-state hash reproduced by a CPU-only run rather
than a log comparison. Task 6 (performance with the M5 content, the human play-through, the gate
decision) is in progress.

## Global Constraints

- "The camera and carried lamp must not enter a wall. Sweep the lamp's placement volume and move it back toward the player when it would intersect geometry." "Do not render the lamp through walls. Do not disable its shadows while it is held." (§14)
- "The collision and visible shell must agree within a documented tolerance. Do not build collision from a denoised image." (§14)
- "Keep the geometry clear of the camera through pose and placement, not camera-ray exclusion." "Do not silently omit the player from the mirror." (§14)
- "Detection uses gameplay data: lamp state, distance, facing, and line of sight. Do not read display brightness or denoised pixels. Exposure and reconstruction must not change the game rules." (§15)
- "A catch restarts from a checkpoint. It must not delete progress outside this demo or require a full process restart." "Provide a restart command. Avoid irreversible soft locks." (§15)
- "Use original, generated, or licensed assets with provenance. Silence is better than an unlicensed asset." "Sound events follow actual world state." "Important sounds need an optional text or visual cue." (§15)
- "Doors have explicit open, closed, and moving states. Their collision, ray-tracing transform, and sound follow the same state. A closing door must not trap the player inside its solid volume." (§14)
- Controls WASD, mouse, Shift, E, F, Escape; configurable sensitivity, inverted look, FOV (state the convention: horizontal at 16:9); no head bob; pause on focus loss (§14).

## Tasks

### Task 1: Collision and placement (T13 part 2)
- `src/game/collision.h/.cpp`: `CollisionWorld` built from `TwoRoomLevel::colliders` (oriented boxes with transforms; the door box updated from its state every tick); `MoveCapsule(position, radius 0.3, height 1.7, delta) -> resolved position` (axis-separated slide, at most 3 iterations); `SweepBox(halfExtents, from, to) -> first free position along the segment`; `RayHitsAny(origin, dir, tMax)`.
- Player: capsule at the feet position; movement resolved per tick; eye stays 1.6 m up; documented tolerance: the collision box equals the visible box (0 m) except the door leaf's overlap 0.01 m.
- Lamp: the held pose is swept from the eye toward the target offset; when the housing volume intersects geometry it moves back toward the player (never inside a wall); placed lamps are static.
- Door closing onto the player: the leaf stops (state Closing -> Open again) when its swept volume meets the capsule.
- CPU tests: capsule stops at walls, slides along them, cannot cross the closed door, passes the open doorway; lamp pushed back at a wall; door blocked by the player. Replay `t13_walls.json`: walk into every wall of Room A and the hall, check the player stays inside (`patch`-free checks: `player_inside` kind evaluated from the world state).

### Task 2: Player body in reflections (T13 part 1)
- Two more objects in `two_room.json` (`player_torso` box 0.18x0.3x0.12 half, `player_hand_l/r` 0.05 half) with `"collider": false`, placed by the player entity 0.35 m below and 0.15 m in front of the eye; they follow the player pose with the same interpolation.
- Replay `t13_reflection.json`: the player faces the mirror at the check pose; `hit` checks that the mirror pixel at the torso's projected reflection reports `player_torso` after one mirror bounce, and a `patch_positive` with the lamp on shows the body's shadow region darker than its neighbour (`patch_ratio`).

### Task 3: Dear ImGui prompts, pause menu, settings, diagnostic panel
- Submodule `external/imgui` (pinned release), Win32 + D3D12 backends rendered into the back buffer after the present copy (RTV heap, SRV heap for the font); the render targets are the swap-chain buffers only, never the path-tracer textures.
- Prompt text from `World::CurrentInteraction` ("E: open door", "E: take lamp", "E: place lamp on shelf", "F: lamp on/off"); a controls card during the introduction; pause menu (Escape: resume, restart from checkpoint, settings, quit) with the cursor released; settings: sensitivity, invert Y, horizontal FOV, exposure, master/effects/ambience volume, subtitles/cues on; diagnostic panel: mode, frame times, history resets, reload button and result, blackout/reset buttons for the temporal tests.
- `--no-ui` keeps the benchmark's main pass free of the overlay (spec §17).

### Task 4: Sound (miniaudio)
- Submodule `external/miniaudio` (pinned release); generated clips (`src/audio/clips.cpp`: footsteps as filtered noise bursts, lamp click, door creak sweep, fixture hum tone with harmonics, fan whoosh loop, threat servo pattern, exit chime) with the generator parameters recorded as provenance.
- `AudioSystem`: listener at the eye pose; sources at entity transforms (the lamp's source uses the fixture transform, spec §14); distance attenuation 1/d² clamped; stereo pan from the listener's right vector; occlusion factor 0.3 when the CPU ray caster finds a collider between source and listener (documented approximation); fixture hum follows the circuit state; door sounds follow the door state; threat movement sound follows its motion; volumes from settings; every important sound has a text cue when cues are enabled.
- Test: `test_audio.cpp` for attenuation, panning, occlusion, and the state-following rules (no device needed: the mixer is driven manually).

### Task 5: Threat state machine, catch, checkpoint restart, objective
- Threat states wait -> patrol (the path) -> investigate (moves toward the last stimulus) -> chase (toward the player while seen) with detection from the lamp state, distance (< 8 m), facing (cos > 0.5), and a line-of-sight ray over colliders (never through the closed door or walls); a catch within 0.6 m triggers the restart from the checkpoint (objective phase + phase start pose), never a process restart.
- Objective: introduction (controls card) -> lamp acquired -> lamp placed on the shelf -> lamp retrieved -> escaped (exit marker in Room A reached with the lamp held and the door open); checkpoints at each phase; `--restart` command and the pause-menu action; the state drives the emitter circuits exactly as today (the lamp's own supply).
- Replays: `t15_route.json` (complete the objective), `t15_catch_restart.json` (get caught, restart, complete), with `objective_state` and `threat_state` check kinds; T14: the same replay under `--mode raw`, `--mode denoised`, `--exposure 0.25/4`, `--internal 960x540` must produce identical objective and threat state logs (a CPU-side hash of the per-tick state).

### Task 6: Records and the human play-through
- STATUS.md: M5 evidence with labels; the play-through by a person recorded as PASSED only when it happened (otherwise NOT RUN with the replays as evidence); performance re-measured with the body, UI off, and sound on.
- DEPENDENCIES.md (ImGui, miniaudio), TESTS.md (T13–T15), BUILD.md (controls, settings, restart), DECISIONS.md, KNOWN_ISSUES.md, README.md.
