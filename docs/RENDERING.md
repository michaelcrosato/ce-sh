# Rendering

State at milestone M2: hardware camera rays with diagnostic views (M1) and a small RGB path tracer
with raw and progressive-reference modes (M2). Everything below exists in the code and is exercised
by the tests in `docs/TESTS.md`. Sections marked **not implemented** are future work.

## Coordinate and matrix contract

- Units: metres, seconds, radians.
- World: right-handed, +Y up. Camera space: right-handed, the camera looks along local -Z with +X
  right and +Y up.
- Column vectors: `world = objectToWorld * local`. A product `A * B` applies `B` first.
- Storage: row-major, `m[row][col]`, on the CPU (`lc::math::Mat4`) and in shader memory
  (`#pragma pack_matrix(row_major)` in `shaders/shared/layouts.hlsli`, and `-Zpr` on every DXC
  command line so no shader can silently get another packing). HLSL indexing `M[row][col]` and
  `mul(M, v)` therefore mean the same as the CPU code. The GPU layout probe (`--validate`)
  confirms element order, offsets, and array packing on the real device.
- Translation lives in column 3. D3D12 instance transforms (`float Transform[3][4]`) are the first
  three rows of `objectToWorld`, copied element for element. Instance transforms must have a
  positive determinant and uniform scale (rejected otherwise, spec §11).
- Triangles are counter-clockwise when seen from outside; the geometric normal is
  `normalize(cross(p1 - p0, p2 - p0))` computed from world-space vertices.
- Facing: DXR reports a triangle as front-facing when `dot(cross(p1 - p0, p2 - p0), rayDirection) < 0`.
  That matches the counter-clockwise rule directly, so instances use
  `D3D12_RAYTRACING_INSTANCE_FLAG_NONE`. The `facing` view and the diagnostic validation check that
  the RayQuery flag and the geometric normal agree on every hit pixel (grazing hits with
  `|cos| < 1e-3` are reported separately because facing is ill-defined there).

## Camera

- Settings store the horizontal field of view (default 90 degrees). The vertical field of view is
  `2 * atan(tan(hfov / 2) / aspect)` with `aspect = width / height`.
- Camera-to-world: `Translation(position) * RotationY(yaw) * RotationX(pitch)`; positive yaw turns
  toward -X, positive pitch looks up.
- Camera ray through pixel `(x, y)` with sub-pixel offset `j` (`0.5` for the diagnostic pass, a
  uniform jitter in `[0, 1)` for the path tracer unless `--no-jitter`):
  `uv = (pixel + j) / renderSize`, `ndc = (2u - 1, 1 - 2v)`,
  `dirView = (ndc.x * tanHalfFovY * aspect, ndc.y * tanHalfFovY, -1)`,
  `dirWorld = normalize(viewToWorld3x3 * dirView)`. The same formula exists on the CPU as
  `Camera::RayDirection`; `Camera::ProjectToImage` is its inverse and places test patches.
- Projection (`viewToClip`, present in the constants for later passes, unused by the current
  shaders): right-handed, D3D clip depth in [0, 1], near plane maps to 0, far plane to 1
  (`m22 = far / (near - far)`, `m23 = near * far / (near - far)`, `m32 = -1`). Near 0.05 m, far 200 m.
- Camera rays use `tMin = 0` and `tMax = far`. Environment radiance is black.

## Ray interface

`shaders/trace/ray_interface.hlsli` provides the only two queries:

- `TraceClosest(scene, origin, direction, tMin, tMax)`: inline `RayQuery<RAY_FLAG_FORCE_OPAQUE>`,
  full traversal loop, returns the committed nearest triangle with `t`, instance index, instance
  id, primitive index, barycentrics, facing flag, and object-to-world matrix. It never uses the
  first-hit shortcut.
- `TraceVisibility(scene, origin, direction, tMin, tMax)`: adds
  `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`; returns true when the finite segment is unoccluded.

All geometry is opaque triangles in one TLAS; camera, continuation, and shadow rays traverse it.

## Materials (spec §7)

Explicit types, validated at `Scene::AddMaterial`:

| Type | Parameters and ranges | Behaviour |
|---|---|---|
| `Diffuse` | `reflectance` in [0, 1] per channel | Lambertian, `f = reflectance / pi`, two-sided (the shading normal is the geometric normal flipped toward the incoming ray) |
| `Mirror` | `reflectance` in [0, 1] per channel (1 allowed for test mirrors) | Ideal specular reflection; delta event, no next-event estimation at this vertex |
| `Emitter` | `radiance` >= 0 per channel, `reflectance` in [0, 1], `emitterOn` | Emits `radiance` from the front (winding) side only; the surface reflects diffusely with `reflectance` (so an off source, or its back, stays in the scene) |
| `RoughConductor` | `reflectance` = F0 in [0, 1] per channel, `roughness` in [0.02, 1] (perceptual; GGX `alpha = roughness^2`) | Single-scattering microfacet conductor: Trowbridge-Reitz (GGX) NDF, height-correlated Smith masking-shadowing, Schlick Fresnel `F = F0 + (1 - F0)(1 - v.h)^5`; `f = D G2 F / (4 (n.v)(n.l))` |

No normal maps; the geometric normal is used everywhere. No energy gain is possible: reflectance
is bounded by 1, the Lambertian sampling weight equals the reflectance, and the conductor weight
`F * G2 / G1(v)` is at most 1.

**Conductor sampling.** Visible-normal sampling (Heitz, "Sampling the GGX Distribution of Visible
Normals", 2018) in the local frame of the shading normal: draw a microfacet normal `h` from
`D_v(h) = G1(v) max(0, v.h) D(h) / (n.v)`, reflect `l = 2 (v.h) h - v`. The solid-angle density of
`l` is `p_B(l) = G1(v) D(h) / (4 (n.v))` (used both for the continuation and for the MIS weight
of light samples), and the sampling weight `f cos / p_B = F(v.h) G2(v, l) / G1(v)`. Samples that
reflect below the surface are dropped: that is the well-known single-scattering energy loss of
microfacet models, recorded as an approximation (no compensation term yet). Test T10 checks the
loss against a CPU integration of the same BRDF and against the closed form `1 - ln 2` for
`alpha = 1` at normal incidence.

**Radiance convention:** scene-linear RGB, unitless. An emitter of radiance `L` that covers a
receiver's whole hemisphere produces irradiance `pi * L`. No watts or lumens are claimed; the
value is the one "radiance scale" the spec asks for.

## Emitters and their sampling distribution

An emitter is an instance whose material is an active `Emitter`. `BuildEmitterTable` (CPU, every
frame) derives the sampling data from the same triangles that are visible, transformed by the same
instance transform:

- per triangle: world-space area `A_t` and a cumulative area fraction within its emitter;
- per emitter: total area `A_e`, radiance `L_e`, power `P_e = luminance(L_e) * A_e`, exact
  selection probability `p_e = P_e / sum(P)` (every active emitter has `p_e > 0`), and a cumulative
  selection probability.

Sampling a point `y`: pick emitter `e` with probability `p_e`, a triangle with probability
`A_t / A_e`, a uniform point on it (square-root parameterization). The area density is exactly
`pdf_A(y) = p_e / A_e`. The emitting side is the triangle's geometric normal `n_y`; a point only
contributes when `cos(theta_y) = dot(n_y, -w) > 0` (one-sided). Scaling an instance changes `A_t`,
`A_e`, `p_e`, and the visible surface together, which test T09 checks.

## The path integrator (spec §12)

`shaders/trace/path_trace.hlsl`, one camera path per pixel per dispatch. Notation: `T` is the path
throughput (starts at 1), `x` the current vertex with shading normal `n`, `f = rho / pi` the
Lambertian BRDF, `p_B(w) = cos(theta) / pi` the BSDF (cosine-hemisphere) density, `p_L(w)` the
solid-angle density of the emitter sampling above, `w_L` and `w_B` the MIS weights.

**Loop** for `hit = 0 .. maxHits - 1` (`maxHits` includes the camera hit and mirror hits;
production value 4):

1. `TraceClosest`. A miss ends the path (black environment).
2. **Emission found by the ray.** If the surface is an active emitter's front side, add
   `T * L_e * w_B` where
   - `w_B = 1` when `hit == 0` (camera ray) or the previous vertex was a mirror (no other
     strategy could have produced this contribution), or in `bsdf` mode;
   - `w_B = p_B^2 / (p_B^2 + p_L^2)` in `mis` mode, with `p_B` the density of the direction that
     produced this hit and `p_L = pdf_A(y) * d^2 / cos(theta_y)` for the hit point;
   - `w_B = 0` in `light` mode.
   Emission is therefore counted once: either here or through next-event estimation, weighted so
   the two strategies sum to one.
3. **Mirror** (delta): reflect the direction about `n`, `T *= reflectance`, mark the vertex as
   delta, continue (no next-event estimation, no MIS against a strategy that cannot run).
4. **Next-event estimation** at a diffuse or emitter surface (`mis` and `light` modes): sample `y`,
   `w = (y - x) / d`, `cos(theta_x) = dot(n, w)`, `cos(theta_y) = dot(n_y, -w)`; skip when either is
   not positive; `p_L = pdf_A(y) * d^2 / cos(theta_y)` (solid-angle measure, the same measure as
   `p_B`); if the segment is unoccluded add `T * f * L_e * cos(theta_x) / p_L * w_L` with
   `w_L = p_L^2 / (p_L^2 + p_B^2)` (power heuristic) in `mis` mode, `w_L = 1` in `light` mode, and
   `w_L = 1` at the last vertex in every mode because no BSDF continuation can compete there
   (spec §12, "MIS weights must match the strategies that can actually contribute").
5. **Termination** at the last vertex. Otherwise sample the BSDF: `w' ~ cos-hemisphere(n)`,
   `p_B = cos / pi`, `T *= f * cos / p_B = rho`, remember `p_B` for step 2 of the next hit.

There is no Russian roulette (fixed short production path; the reference mode uses more hits, not
survival sampling). Paths with zero throughput stop early.

**Path families.** With `N` hits, `mis` and `light` include direct light at the `N`-th surface
through next-event estimation, so they cover emitter positions up to `N + 1` along the path.
`bsdf` can only reach an emitter as one of its `N` hits. Comparing the estimators on the same
family therefore gives `bsdf` one extra hit (`--max-hits N+1`); test T08 does exactly that and the
three agree within 1 %.

**Two-sided surfaces.** Diffuse and emitter surfaces shade with the geometric normal flipped
toward the incoming ray; emission uses the unflipped normal so only the front emits.

## Numerical robustness (spec §11)

- Secondary-ray origins use the scale-aware method from Ray Tracing Gems chapter 6 ("A Fast and
  Robust Method for Avoiding Self-Intersection", `OffsetRay` in `sampling.hlsli`): an integer ULP
  offset (scale 256) along the geometric normal for coordinates with `|p| >= 1/32`, a fixed
  `1/65536` float offset closer to the origin. The normal points toward the side the new ray
  travels on.
- That bound covers the error of the *point*, not of the *triangle*: the interpolated hit position
  and the hardware intersection carry error proportional to the largest vertex coordinate of the
  triangle. `OffsetRayTri` therefore adds `256 * 2^-23 * max|vertex coordinate|` along the normal
  (0.12 mm for a 4 m slab, 6 mm for the 400 m furnace floor). Without it, the T10 furnace on a
  400 m floor lost 3.4 % of the energy to self-hits from below; with it every furnace case matches
  the reference, and T03/T04 still show no leak through 0.15 m walls and the 5 mm door inset.
- Shadow rays run from `OffsetRayTri(x, n)` to `OffsetRayTri(y, n_y)` with an unnormalized
  direction `q - p` and the finite interval `[0, 1 - 1e-4]`, so neither endpoint's own surface is hit.
- There is no per-asset bias.

## Sampling (spec §12)

- Generator: `Hash4(pixelKey, sampleIndex, dimension, seed)` with the PCG output permutation
  (`sampling.hlsli`), 24 significant bits per value. Deterministic for a given seed; no clock input.
  It is a hash sequence, not a low-discrepancy sequence (recorded as an approximation below).
- Dimensions: 0-1 camera jitter; for bounce `b`: `2 + 8b + 0` emitter selection, `+1` triangle,
  `+2, +3` point on the triangle, `+4, +5` BSDF direction, `+6, +7` reserved.
- Raw mode uses `sampleIndex = 0` and `seed = baseSeed + 7919 * frameIndex` so the noise pattern
  changes every frame; reference mode uses the accumulated sample index.
- Invalid values: NaN, infinity, and negative components of a sample are counted in a GPU stats
  buffer (and neutralized to zero so they cannot hide in an average); zero-probability events are
  counted too. `--validate` requires all counters to be zero.

## Modes (spec §13)

| Mode | What happens | Selection |
|---|---|---|
| Diagnostic (`--mode diag`) | `camera_view.hlsl`, one ray per pixel, view selected by `--view` | M1 views |
| Raw real-time (`--mode raw`) | One path sample per pixel per frame, no accumulation, seed varies per frame | Default |
| Progressive reference (`--mode reference`) | The scene is frozen; `--samples-per-frame` dispatches per frame accumulate until `--spp`; no denoising | Tests, captures |
| Real-time reconstruction | **not implemented** (M4) | |

Accumulation is reset when an "accumulation key" changes: camera pose and lens, render size, mode,
`--max-hits`, `--strategy`, `--seed`, jitter, the scene's instance count, any instance transform
revision, or the material revision (emitter on/off). Exposure does not reset it. Each dispatch
increments the sample index; the reset dispatch overwrites the sums instead of adding.

## Accumulation, statistics, and display

- `accum` (RGBA32F): running sum of samples and the sample count in `.w`; `accumSq`: running sum of
  squares. Mean `m = sum / N`. Per-pixel sample variance
  `s^2 = max(0, sumSq / N - m^2) * N / (N - 1)`; standard error of the pixel mean
  `se = sqrt(s^2 / N)` (zero when `N < 2`). The readback recomputes the mean from the sums.
- Patch statistics: mean over the patch pixels of `m`; standard error of the patch mean
  `sqrt(sum(se^2)) / P` assuming independent pixels.
- Display: `sRGB(saturate(m * exposure))`, once, in the same dispatch. There is no tone mapping
  yet (clamp only, so radiance above `1 / exposure` saturates). Exposure is a display setting and
  is not part of captured radiance.
- Hit info in path modes: `x` = stable id of the first non-mirror surface (or the last mirror when
  the path leaves the scene through it), `w` = number of mirror bounces before it.

## Captures

`--capture <dir>` writes, from the running program after the last frame:

- `<scene>_<mode>_<strategy>_spp<N>.png` (or `<scene>_<view>_f<frame>.png` for diagnostics): the
  8-bit display image (lossless, stored-deflate PNG).
- the matching `.pfm`: the linear float RGB image, which is the mean scene-linear radiance in raw
  and reference modes (`"linearIsRadiance": true` in the metadata) or the diagnostic colour.
- the matching `.json`: scene, view or mode, strategy, frame index, samples per pixel, seed, path
  depth, exposure, render size, output size, reconstruction state, adapter, driver, build commit
  and config, timestamp, instance, triangle, and emitter counts, GPU timings per pass.

`--stats <file>` writes mean and standard error per channel for the scene's statistics patches.

## Shared GPU records

`FrameConstants` (b0, 256 bytes), `IntegratorConstants` (b1, 32 bytes), `InstanceRecord` (t1,
112 bytes, TLAS order, carries `emitterIndex`), `MeshRecord` (t2), positions (t3), indices (t4),
`MaterialRecord` (t5, 48 bytes), `EmitterRecord` (t6, 48 bytes), `EmitterTriangle` (t7, 16 bytes).
UAV table: u0 display, u1 linear, u2 hit info, u3 layout probe, u4 accumulation, u5 accumulation
of squares, u6 stats. Matrices in structured buffers are three explicit `float4` rows.

## Known approximations (spec H05)

- Finite path length (4 hits in production) and one sample per pixel per frame in raw mode.
- Hash-based sampling rather than a low-discrepancy sequence; convergence is `1/sqrt(N)`.
- Float32 accumulation sums (adequate to a few thousand samples at these radiance levels).
- Two-sided diffuse shading; emitter surfaces reflect diffusely; single-scattering conductors
  (energy loss at high roughness, no multiple-scattering compensation); no participating media;
  no tone mapping.

## Not implemented yet

Denoiser buffer contract and history policy (M4); motion vectors and previous-frame data (the
records carry previous transforms but no pass consumes them); scene files (M3); tone mapping
beyond clamping; multiple-scattering compensation for rough conductors.
