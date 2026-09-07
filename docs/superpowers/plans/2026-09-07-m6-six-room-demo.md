# Last Circuit M6 Six-Room Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The complete encounter of spec §5 in one scene file built from the shared kit: six rooms and two halls on one floor, the outward route (controls, lamp, mirror, fan, fuse), the fuse that darkens the return route, the exit that opens onto a lit space, the machine patrolling and hunting through both halls, and the fixed 180-second replay that measures it (T16), with every test-matrix row that applies (spec M6 gate: "the complete encounter meets the test matrix; no new asset bypasses the rendering or material rules").

**Architecture:** The scene file schema moves to version 2 with lists instead of the proof's singular entities: `doors[]`, `items[]` (the lamp with its light and the fuse without one), `sockets[]` that state which items they accept, `circuits[]` that can be powered by an item sitting in a socket, a `fan` object kind (hub and blades rotated by the world while its circuit is on, a real emitter behind them), and `objectives[]` as an ordered list of steps (`take`, `place`, `reach`) whose ids are the phase names; the two-room proof is migrated to the same schema so its replays and hashes stay the reference. The world generalises the same way (a vector of doors, items with sockets, circuits evaluated from item placement every tick, the fan's angular state with a spin-down, the step list driving phases and checkpoints); the machine gets a hall polyline for its off-path moves so corners no longer stall it. Rendering, denoising, sound, and interface code are unchanged except for the fan loop and the prompts' item names. Evidence follows the established discipline: CPU tests for the data and rules, CPU-only replay runs with the state hash, GPU runs of the same replays in several modes, image checks at the key ticks, the §17 benchmark on the 180-second replay.

**Tech Stack:** unchanged (C++20, D3D12/DXR inline ray queries, NRD, Dear ImGui, miniaudio; no new dependencies).

**Spec:** §4 (sequence), §5 (layout, power, presentation), §6 (budgets), §14–§16, §17 (T16 protocol), §19 T13–T17, §20 M6 gate, §21, §22.

**Status (2026-09-07):** Tasks 1–6 implemented, tested, and committed on `m0-m1-bootstrap`.
Deviations from the text below, all recorded in `docs/DECISIONS.md` and `docs/KNOWN_ISSUES.md`:
the routing lives in `Threat` (`PlanRoute` / `FollowRoute` over the patrol polyline) rather than a
`game/nav` module (D-049); the Plant room is a side room off Hall B with a ceiling fan under its
fixture, and the Switch room's fixture is on the fused circuit (KI-030); the mirror sits at 1.3 m so
its reflected ray descends onto the machine (D-050); an item that hides when carried goes into a
pocket beside the hand (D-051; the plan's single held item refused the fuse); the encounter replay
is 99 s and the caught variant 118 s (the §17 measurements are three 180-second runs of the looping
encounter plus one of the caught variant); the image checks run frozen at their ticks in reference
mode instead of inside one denoised run (D-052, KI-031/KI-033); the "fan shadow sequence" is a pair
of blade identity checks a quarter second apart plus the stopped fan at its integrated rest angle;
the 78-mesh count against the §6 reusable-mesh budget is KI-029. The human items of the M6 gate are
NOT RUN (`docs/STATUS.md`).

## Global Constraints

- "Keep the level on one floor. Aim for approximately 160–220 square metres of walkable space, including halls." (§5)
- "Build a measured layout without overlapping walls or impossible door swings." (§5)
- "Give each fixture a circuit or battery state. The fuse controls a defined circuit. The lamp has its own supply. A hall emergency fixture may remain on if its independent supply was established earlier." (§5)
- "Do not spawn an invisible light for a scare. A script may operate an existing fixture, door, fan, or threat." (§5)
- "Start with no more than eight active emitter fixtures in the complete demo, including the portable lamp." (§5)
- "Fan blades cast moving shadows from a real source." "A stopped fan must not continue to sound as though it rotates." (§5, §15)
- "Removing the fuse changes circuit state. Inserting it at the exit changes the appropriate circuit. The same state drives fixture emission, source data, fixture sound, and relevant animations." (§15)
- "Do not let the threat see through a closed opaque wall. Do not teleport it through an active mirror view." (§15)
- Budgets (§6): complete level below 100,000 triangle instances, placed objects below 500, 20–30 reusable meshes, materials only diffuse / rough conductor / mirror / emitter, no transparency, rigid motion only.
- "Use deterministic replay for testing." (§4) Tolerances chosen before the runs and never widened.

## Measured layout (metres; x east, z north, y up; ceiling 2.8; walls 0.15; halls 2.4 clear)

```text
z 17.55 ┌────────────┐
        │ 2 Equipment│ (x 4.15–8.15, z 12.55–17.55; the lamp on a floor socket, the crate)
z 16.55 ├─1 Security─┤────────────┬─3 Inspection─┐
        │ x 0–4      │            │ x 8.3–12.3   │ (Inspection: fixture off, mirror on the north wall aimed
        │ z 12.55–   │            │ z 12.55–16.55│  through its door at Hall A, the shelf, the sink block)
z 12.55 ├──door 2.0──┼──door 6.15─┼──door 10.3───┤
z 12.40 │                HALL A (x 0–16.4)        │ two hall fixtures (fused), the emergency fixture near
z 10.00 ├──door 2.0──┐                  ┌─────────┤ the exit door (independent), the machine's route
        │ 6 Exit     │                  │ HALL B  │ (x 14–16.4, z 0–10)
        │ x 0–4      │                  │         ├──door z 6.0──┐
        │ z 5.85–9.85│                  │         │  4 Plant     │ (x 16.55–22.55, z 3–9; the fan in the
z  5.85 ├──exit door─┤                  │         │              │  east wall with the fixture behind it)
        │ vestibule  │                  │         └──────────────┘
        │ x 1–3.4    │                  ├──door x 15.2──┐
z  3.30 └────────────┘                  │ 5 Switch room │ (x 14–18, z -5.15 – -0.15; the fuse box)
                                        └───────────────┘
```

Walkable area: rooms 16 + 20 + 16 + 36 + 20 + 16 = 124 m², Hall A 39 m², Hall B 24 m², vestibule 5.8 m² → about 193 m².

Circuits: `security` (Room 1 fixture, on), `equipment` (Room 2, on), `inspection` (Room 3, off), `hall` (Hall A fixture, Hall B fixture, the Plant fixture behind the fan, the fan, the Switch room fixture; powered by the fuse in the fuse box), `emergency` (Hall A, independent, on), `exit` (the vestibule fixture and the exit door; powered by the fuse in the exit panel), `lamp` (its own supply). Active emitters: 8 at the start (security, equipment, hall A, hall B, emergency, plant, switch, lamp), 5 after the fuse is removed, 6 once the exit is powered.

Objective steps (phase names): `lamp_acquired` (take the lamp) → `lamp_placed` (shelf) → `lamp_retrieved` → `fuse_available` (reach the Switch room) → `fuse_carried` (take the fuse: `hall` goes dark, the fan spins down) → `exit_powered` (insert the fuse at the exit panel: the vestibule lights, the exit door opens) → `escaped` (reach the vestibule with the lamp).

Machine: patrol polyline Hall A west → east → Hall B south (ping-pong, 1.2 m/s); hunt as in M5 with the polyline as the route for off-path moves; catch → checkpoint restart (M5 mechanism).

## Tasks

### Task 1: Scene file schema 2 (lists, items, powered circuits, fan, objective steps)
- `scene_file.cpp`: schema 2 parses `doors: [{id, object, locked?, opensWithCircuit?}]`, `items: [{id, object|housing+face(+light: material, faceOffset), startSocket, hidesWhenCarried?}]`, `sockets: [{id, position, yaw, accepts: [item ids]}]`, `circuits: [{id, on, poweredBy?: {item, socket}}]`, objects of kind `fan` (`centre, axis, radius, blades, bladeWidth, bladeThickness, hubRadius, rpm, circuit, material`: hub plus blade boxes generated as separate instances named `<id>_hub`, `<id>_blade<n>`), `objectives: [{id, kind: take|place|reach, item?, socket?, marker?, radius?, requires?, text}]`, `entities.threat` and `entities.mirror` unchanged, `entities.player` unchanged. Every id must resolve; every rule reports its list and field; limits extended (doors, items, steps).
- `Level` (rename of `TwoRoomLevel`, alias kept): `doors[]` (DoorHandle + id + locked + circuit), `items[]` (id, instances, light material, start socket, hidesWhenCarried, housing half), `sockets[]` with accepts, `circuits[]` with poweredBy, `fans[]` (hub id, blade ids, axis, centre, rpm, circuit), `steps[]`, plus the existing mirror, threat, body fields.
- Migrate `assets/scenes/two_room.json` to schema 2 (the same geometry and ids; the proof's four steps named as today) and update `test_scene_file.cpp` (golden counts, rejection cases for every new rule, the fan generation, powered circuits).
- CPU tests pass; the M5 replays still hash to the recorded constants (the world is untouched in this task).

### Task 2: World generalisation (doors, items, circuits, fan, steps) with the proof unchanged
- `World`: `std::vector<Door>` (interaction target `door:<id>`; the locked exit door ignores E and opens when its circuit turns on), `std::vector<Item>` (Held/Placed, socket name; the lamp's light as an optional part; `hidesWhenCarried` parks the instance inside the torso box), sockets accept lists, `EvaluateCircuits()` each tick (a powered circuit is on iff its item sits in its socket; fixtures of a circuit follow it through `SetEmitterOn`; the fan's target speed follows its circuit), `Fan` (angle, angular speed with a 3 s spin-up/spin-down, per-tick transforms of hub and blades), the step list driving `Phase()` (phase name = the last completed step id, `introduction` before the first), checkpoints capturing every door, item, fan, and circuit, catch restart restoring them.
- Prompts: `Take the <item>`, `Place the <item> on the <socket text>`, `Insert the fuse`, `Open/Close the door`; the objective line from the step text.
- Audio: `FanLoop` while the fan turns (level scaled by angular speed), fixture hums keep following `emitterOn`.
- The state hash covers all doors, items, fans, and circuits; regenerate `LC_T15_ROUTE_HASH` and `LC_T15_CATCH_HASH` with `--simulate-only` once and record why.
- Tests: `test_world.cpp` and `test_objective.cpp` for the generalised proof (identical outcomes: the t15 replays still complete and are caught at the same ticks), new cases for a powered circuit, the locked door, the fan spin-down, an item that hides when carried.

### Task 3: The six-room scene file
- `assets/scenes/six_room.json` following the measured layout above (slabs for floors, ceilings, and walls, `wall_opening` pieces for the doorways, six door leaves plus the exit door, ten fixtures on their circuits, the fan with its emitter behind it, the crate, the sink block, the shelf, the fuse box and the exit panel as boxes with their sockets, the lamp and the fuse items, markers: player start in Security, switch room, exit vestibule, mirror check camera, mirror aim point in Hall A; the machine's polyline path).
- CPU tests (`test_six_room.cpp`): every room is enclosed (ray sweeps as in the two-room test), every door swings free of walls and furniture, the walkable area lies within 160–220 m², at most eight active emitters at any step of the objective, triangle instances and placed objects within the §6 budgets, the mirror's aim shows the hall point from the check camera, the machine's path stays inside the halls with 0.3 m clearance, every socket and marker lies inside a room, the sealed-room rule holds for the inspection room with its door closed (the light of Hall A does not reach a floor patch).
- `--scene six_room` as a named scene beside `two_room` (both from files).

### Task 4: Hall routing for the machine
- `game/nav.h/.cpp`: a polyline route (the patrol path's points) with `ClosestPoint`, `PathTo(from, to)` that walks the polyline between the nearest points when the straight segment is blocked (`CollisionWorld::SegmentClear` at the machine's body height); `Threat` uses it for chase-lost, investigate, and return moves; the stuck timers stay as the last resort.
- Tests: the machine returns from the Plant room's door to its route around the Hall B corner without stalling; a chase around the Hall A/B corner keeps closing in.

### Task 5: The encounter replay, T14/T15/T16 on the demo, image checks
- `tests/replay/t16_encounter.json` (about 180 s): the full route with state checks at each phase, `caught_count` 0, a mirror `hit` check in Inspection (the machine seen through the mirror while not directly visible), `patch_positive`/`patch_dark` pairs for the fan's moving shadow (a frame sequence over one blade pass), the hall floor before and after the fuse (`patch_positive` then `patch_dark` on the fused fixture's floor patch, the emergency patch unchanged), the vestibule floor lit after the exit is powered, the exit door open (`hit`).
- `tests/replay/t16_caught.json`: a catch in Hall B in the dark return and the checkpoint restart, then completion.
- ctest: CPU-only runs with hashes, T14 variants (raw, denoised, exposure, internal size) on the encounter, T15 on the caught replay, the mirror and fan image checks in reference mode at their ticks.
- Benchmark: `tests/scripts/bench_m6.ps1` (three 60-second runs are not enough: the protocol wants the fixed replay; run the whole 180 s replay once and twice looped for 60 s windows) → the §17 criterion (p95 ≤ 16.67 ms, p99 ≤ 22 ms) judged on this content; memory trend over the run.

### Task 6: Records and the M6 gate
- STATUS (M6 evidence, the test matrix row by row with labels, the gate decision: PASSED only if every applicable row passed, the human items NOT RUN stated), TESTS (six-room tests, T16 protocol), BUILD (`--scene six_room`, the encounter commands), ARCHITECTURE (nav, items, circuits), DECISIONS (schema 2, circuits by item placement, fan, routing), KNOWN_ISSUES (what the layout compromises), README, DEPENDENCIES unchanged.
