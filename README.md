# Last Circuit

A small custom Windows engine and first-person escape-horror demo in which hardware path tracing
(Direct3D 12, DXR inline ray queries) provides camera visibility and all world lighting. No
complete game engine, no raster-lighting fallback, no online services.

The product specification is [`LAST_CIRCUIT_STARTER.md`](LAST_CIRCUIT_STARTER.md); repository
instructions for coding agents are in [`AGENTS.md`](AGENTS.md).

## Status

Milestones M0 (environment and build), M1 (hardware geometry), and M2 (raw light transport) are
complete on the recorded machine: an RTX 4070 SUPER runs a small path tracer with next-event
estimation, multiple importance sampling, one-sided area emitters, and ideal mirrors, checked
against closed-form irradiance, sealed-room zero tests, estimator cross-agreement, and mirror
identity tests. The moving two-room scene (M3) is next. Details, evidence labels, and the next
task: [`docs/STATUS.md`](docs/STATUS.md).

## Quick start

```powershell
. .\tools\env.ps1                     # bundled CMake on PATH (or: winget install Kitware.CMake)
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
.\build\windows-debug\bin\LastCircuit.exe --scene t08_box --mode reference     # path-traced Cornell-style box
.\build\windows-debug\bin\LastCircuit.exe --scene rt_boxes --mode diag         # M1 diagnostic normals view
```

Requirements: Windows 11, Visual Studio 2026 with the C++ workload (Windows SDK 10.0.26100), and
a GPU with DXR Tier 1.1 (NVIDIA RTX). See [`docs/BUILD.md`](docs/BUILD.md).

## Documentation

| Document | Content |
|---|---|
| [`docs/BUILD.md`](docs/BUILD.md) | Prerequisites, commands, outputs, run options, exit codes |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Modules, frame sequence, lifetimes, error policy |
| [`docs/RENDERING.md`](docs/RENDERING.md) | Coordinate contract, camera, ray interface, diagnostic views, captures |
| [`docs/TESTS.md`](docs/TESTS.md) | Test commands, criteria, tolerances, manual inspection |
| [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md) | Exact toolchain versions and licenses |
| [`docs/STATUS.md`](docs/STATUS.md) | Milestone status with evidence labels and the next task |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | Decision log |
| [`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md) | Open issues |

## License

Not yet chosen by the project owner. All code so far is original to this repository; there are no
third-party source dependencies.
