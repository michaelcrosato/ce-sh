# Last Circuit

A small custom Windows engine and first-person escape-horror demo in which hardware path tracing
(Direct3D 12, DXR inline ray queries) provides camera visibility and all world lighting. No
complete game engine, no raster-lighting fallback, no online services.

The product specification is [`LAST_CIRCUIT_STARTER.md`](LAST_CIRCUIT_STARTER.md); repository
instructions for coding agents are in [`AGENTS.md`](AGENTS.md).

## Status

Milestones M0 (environment and build), M1 (hardware geometry), M2 (raw light transport), M3
(moving two-room scene), and M4 (stable real-time image at native resolution) are complete on the
recorded machine: an RTX 4070 SUPER runs a small path tracer with next-event estimation, multiple
importance sampling, one-sided area emitters, ideal mirrors, and GGX conductors, checked against
closed-form references and sealed-room zero tests; a fixed-step simulation with deterministic
replays drives a first-person camera, a hinged door, a portable lamp fixture, and a threat that is
visible only in the mirror; the denoised mode reconstructs one sample per pixel with NVIDIA NRD
REBLUR, describing the mirror's reflected surfaces to the denoiser (primary surface replacement),
with measured motion vectors, an explicit history policy, a zero-lag trail test on the mirrored
threat, frame-sequence captures, and a benchmark report. Scene files (M3.5) and the playable proof
(M5) are next. Details, evidence labels, and the next task: [`docs/STATUS.md`](docs/STATUS.md).

## Quick start

```powershell
git submodule update --init --recursive   # NRD and its build helpers (pinned; nothing is downloaded later)
. .\tools\env.ps1                     # bundled CMake on PATH (or: winget install Kitware.CMake)
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
.\build\windows-debug\bin\LastCircuit.exe --scene two_room --play --exposure 4 # walk the two-room proof (WASD, mouse, E, F, Escape)
.\build\windows-debug\bin\LastCircuit.exe --scene two_room --play --mode denoised --exposure 4   # the same, reconstructed (NRD)
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

Not yet chosen by the project owner. The code in `src/`, `shaders/`, `tests/`, and `tools/` is
original to this repository. `external/` holds git submodules: NVIDIA NRD under NVIDIA's RTX SDK
license (attribution required; it must not be relicensed as open source), and ShaderMake and
MathLib under MIT. See [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md) before choosing a license.
