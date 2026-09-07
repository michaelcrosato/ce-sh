# Last Circuit M7 External Test Package Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A package that a tester runs by double-clicking, offline, without a developer SDK, asset download, or API credential (spec §20 M7, §19 T18, §21 "a packaging command and a smoke-test command", §24 "the package runs offline and includes required notices; settings explain unsupported features without presenting nonfunctional switches"): settings that persist, rebindable actions, accessible cues, build identification, license notices, clean startup errors, a smoke test and a benchmark the tester can run, and the owner's deliverables (gameplay capture, benchmark report, known-issues list).

**Architecture:** The executable stays one console-subsystem binary (development and tests unchanged) that closes the console it was started with when it owns it alone (a double-click), writes its log to the user's data directory by default in play, and reports startup failures in a message box with the log path. Both presets link the static CRT so the package needs no redistributable. Settings (look, exposure, volumes, cues, UI scale, bindings) live in one JSON file in `%LOCALAPPDATA%\LastCircuit`, loaded only by play (replays, benchmarks, and tests never read it); the pause menu edits them, including a press-a-key rebinding of the seven game actions, and states the fixed keys and the unsupported features in words. A PowerShell packaging script assembles the package directory from the Release build (executable, shaders, assets, replays, generated notices, a README, the smoke-test and benchmark commands, the known-issues list), checks the executable's imports against Windows' in-box libraries, and zips it with a checksum; a ctest runs the package's smoke test from a directory outside the build tree.

**Tech Stack:** unchanged (no new dependencies).

**Spec:** §3 (license notices), §14 (rebindable actions, settings conventions), §15 (cues), §17 (benchmark protocol, release-equivalent build), §19 T01/T16/T18, §20 M7 gate, §21, §23 (records), §24.

**Status (2026-09-07):** Tasks 1–3 implemented, tested, and committed on `m0-m1-bootstrap`; the
records follow in the next commit. Deviations from the text below, all recorded in
`docs/DECISIONS.md` and `docs/KNOWN_ISSUES.md`: the LNK4098 warning came from `miniaudio.c`
compiled without CMake's runtime flags (C was not an enabled language), not from a static-CRT
object elsewhere; the settings struct lives in `src/game/settings.*` (the interface library aliases
it) so the CPU tests reach it without graphics; the freed console needed the standard streams
redirected to NUL (handle reuse doubled the log); the frame's input clear moved after the
interface so a rebinding key press is seen; windowed replays now draw the interface so the
gameplay capture shows what a player sees; the end card's capture frame is five frames after the
completion (ImGui sizes a new window on its first frame); `package_smoke` computes hashes and
zips through .NET because CTest's PowerShell environment does not load the utility modules, and
the package commands call the executable by its full path because a PowerShell-started `cmd` does
not run programs from the current directory. The clean-machine test (T18) is NOT RUN (KI-036).

## Global Constraints

- "Preserve their license notices. Do not describe third-party code as original work." (§3)
- "Initial controls are WASD movement, mouse look, Shift sprint, E interact, F lamp switch, and Escape pause. Make the actions rebindable before external release." (§14)
- "An accessibility brightness control may adjust global display exposure within a documented range. It must not change gameplay detection." (§12)
- "Provide a packaging command and a smoke-test command before M7. Do not require users to launch the game from a developer terminal." (§21)
- "Settings explain unsupported features without presenting nonfunctional switches." (§24)
- "Do not invent test results ... or device support." A clean machine is not available in this session: T18 is recorded as NOT RUN with the substitute evidence named.
- NRD attribution: "This software contains source code provided by NVIDIA Corporation." on the credit screen and in the end-user documentation (docs/DEPENDENCIES.md).

## Tasks

### Task 1: Identification, runtime, aliases, clean startup
- `CMakeLists.txt`: `CMAKE_MSVC_RUNTIME_LIBRARY` static for both presets (no Visual C++ redistributable; resolves KI-034 if the LIBCMT conflict came from a static-CRT object); full suites green in both presets.
- `--version` prints `Last Circuit <version> build <commit>[-dirty] (<config>)` and exits 0; the same line stays at the top of every log.
- `--scene last_circuit` (the demo) and `--scene mirror_lab` (the proof) as the §21 names beside `six_room` and `two_room`.
- No arguments at all: the game (`--scene last_circuit --play --mode denoised`, exposure from the settings); any argument keeps the developer interface.
- `main.cpp`: free a console that only this process uses (a double-click); default log file `%LOCALAPPDATA%\LastCircuit\logs\LastCircuit-<timestamp>.log` for play runs without `--log`; a message box for startup failures (unsupported hardware in plain words with the requirement, usage errors, fatal errors) with the log path, unless `--headless`, `--validate`, `--list-adapters`, `--simulate-only`, or `--no-dialog`.
- CPU tests: `--version`, the aliases, the launch arguments, `--no-dialog`.

### Task 2: Settings file, rebinding, the menu
- `src/app/settings_file.*`: `ui::Settings` + `Bindings` (forward, back, left, right, sprint, interact, lamp as virtual keys; Escape and F1 fixed) to and from JSON with validation (ranges clamped, unknown keys logged, a broken file gives defaults); `KeyName(vk)`; `--settings <file>`; play loads at start and saves on every change and at exit.
- `InputFromRaw(raw, bindings, ...)` in a header the tests can reach; the app uses it.
- The menu: a Controls section (press a key to rebind, conflicts shown, reset to defaults), UI scale (0.8–1.6), the brightness range stated (0.25–8x, display only), the reconstruction line ("NRD REBLUR at the internal resolution; DLSS and frame generation are not available in this build"), an About section (version, build, the NVIDIA attribution, the third-party notices, the notices file), the controls card and the prompts show the bound keys.
- CPU tests: round trip, clamping, defaults on a broken file, key names, the input mapping through bindings.

### Task 3: Packaging, notices, the smoke test, the package test, the capture, records
- `tools/package.ps1`: assemble `dist/LastCircuit-<version>-<commit>-win64/` (executable, `shaders/`, `assets/`, `replays/`, `NOTICES.txt` generated from the submodules' license files with the NVIDIA attribution, `README.txt`, `KNOWN_ISSUES.md`, `smoke_test.cmd`, `benchmark.cmd`), verify the executable's imports against the in-box list, zip with a SHA-256 file.
- `smoke_test.cmd`: adapters and the environment report, the T01 diagnostic validate, a 600-frame denoised validate of the demo's encounter, a 10-second benchmark report; PASS/FAIL per step; `/quiet` for scripts.
- `benchmark.cmd`: the §17 run (180 s, 1920x1080 from 1280x720, denoised, vsync off) into `benchmark\`.
- ctest `package_smoke` (label `package`): package without building, run the smoke test from a directory outside the build tree.
- `--capture-backbuffer` takes a list of frames; `tools/capture_gameplay.ps1` writes the presented frames (interface included) of the encounter at its key ticks into `artifacts/m7/gameplay` (the owner's gameplay capture: the fixed replay as presented, described as such).
- Records: BUILD (packaging, the package's commands), DEPENDENCIES (static CRT, notices), DECISIONS, KNOWN_ISSUES (T18 NOT RUN on a clean machine, the console flash, rebinding limits), TESTS, STATUS (M7 evidence and the gate), README.
