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

## D-013 (2026-09-06) Repository workflow for this session

- Decision: work on branch `m0-m1-bootstrap` with small commits; nothing is pushed; `build/` and `artifacts/` are ignored. The owner decides on merging.
