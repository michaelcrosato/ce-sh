# Last Circuit — Repository Instructions

The project specification is `LAST_CIRCUIT_STARTER.md` in this directory.
Read that file completely before the first implementation session.
On later sessions, read its hard rules, the relevant technical sections,
and `docs/STATUS.md` before making changes.

## Objective

Build a small custom Windows engine and a first-person escape-horror demo.
Use hardware path tracing for camera visibility and all world lighting.
Prove two rooms, a movable lamp, a door, and an off-screen mirror reflection
before expanding to the six-room game.

## Fixed rules

- Use C++20, HLSL, Direct3D 12, and hardware DXR. Start with inline ray queries.
- Do not use a complete game engine or a production raster-lighting fallback.
- Use one scene for camera, reflection, indirect, and source-visibility rays.
- Do not add baked lighting, hidden fill sources, fake reflections, or ambient brightness.
- The game runs offline. It does not require an AI service or API key.
- Existing bounded libraries are permitted. Pin versions and preserve license notices.
- Preserve user work. Do not publish, purchase, upload, or make destructive changes without permission.

## Work method

Inspect the actual workspace and tools. Begin with the earliest incomplete
milestone in the specification. Implement one testable increment at a time.
Do not stop at a proposal when implementation tools are available.

Read actual dependency headers and current official integration guides.
Do not invent APIs, files, build results, screenshots, or GPU measurements.

Separate WRITTEN, COMPILED, CPU TESTED, GPU EXECUTED, IMAGE CHECKED,
PERFORMANCE CHECKED, PASSED, FAILED, and NOT RUN in status reports.
A missing Windows or RTX environment must not be reported as a passed GPU test.
Continue useful portable work and record the exact blocked test.

Run the relevant checks after each substantive change. Keep a reproducible
build and an accurate known-issues list. Do not weaken tests to hide failures.

Update `docs/STATUS.md` with actual results and one precise next task.
Record architectural changes in `docs/DECISIONS.md`.

Use the detailed milestone gates and acceptance tests in the specification.
Do not expand the level while the moving mirror, lighting, or performance gate fails.

## Where things are

Build and run: `docs/BUILD.md` (use `. .\tools\env.ps1` to put the bundled CMake on PATH).
Tests and criteria: `docs/TESTS.md`. Current milestone, evidence, and the next task: `docs/STATUS.md`.

## First task in an empty repository (completed 2026-09-06)

Complete M0 and M1: environment audit, project setup, tests, device checks,
and a real hardware-ray-traced geometry scene. Then proceed toward the
raw two-room lighting proof. Use generated assets rather than waiting for art.
