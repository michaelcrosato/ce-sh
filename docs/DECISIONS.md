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

## D-013 (2026-09-06) Repository workflow for this session

- Decision: work on branch `m0-m1-bootstrap` with small commits; nothing is pushed; `build/` and `artifacts/` are ignored. The owner decides on merging.
