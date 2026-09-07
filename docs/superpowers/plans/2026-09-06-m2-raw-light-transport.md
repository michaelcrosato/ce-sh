# Last Circuit M2 Raw Light Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the M1 camera-ray pass into a small, verifiable RGB path tracer: explicit materials (Diffuse, Mirror, Emitter), one-sided rectangular area emitters sampled from the same triangles that are visible, next-event estimation combined with BSDF sampling by multiple importance sampling, raw and progressive-reference modes with deterministic seeds, invalid-value counters, and automated tests T03, T04, T07, T08, T09 plus an off-screen mirror identity check (spec §7, §11, §12, §19, M2 gate in §20).

**Architecture:** The scene gains a material table and emitter extraction (world-space areas and a power-based selection distribution rebuilt whenever transforms change). A new compute shader `path_trace.hlsl` traces one camera path per pixel per sample through the existing TLAS with `TraceClosest`/`TraceVisibility`, accumulates radiance sums and squared sums into float textures, and writes the display image (fixed exposure, sRGB encoding) in the same dispatch. The renderer owns accumulation state and resets it on incompatible changes; the application adds modes, statistics output, and radiance expectations to `--validate`.

**Tech Stack:** unchanged (C++20, D3D12, HLSL cs_6_5 inline RayQuery, no third-party code). One PowerShell comparison script for cross-run tests.

**Spec:** `LAST_CIRCUIT_STARTER.md` §7 (materials, sources, exposure), §11 (numerical robustness), §12 (path tracer contract), §13 (modes table), §18 (invalid-value counts), §19 (T03–T10 details), §20 (M2 gate), §21 (`--mode`, `--spp`, `--capture`).

## Global Constraints (in addition to the M1 plan's list)

- Explicit material types with documented parameter ranges; reject invalid values; no energy gain from reflective materials (test mirror may use unit reflectance) (§7).
- Rectangular one-sided area emitters represented by real triangles; the sampling distribution and the visible emitting triangles read the same data and are transformed together; scale changes update area and sample weights; the back of a one-sided source does not emit; an off source stays in the scene and may reflect (§7).
- Black environment radiance; fixed exposure; scene-linear HDR buffers; tone mapping and output encoding once (§7).
- Scale-aware secondary-ray origins with a documented error-bound method; finite shadow-ray intervals; no per-asset bias (§11).
- Next-event estimation at eligible non-delta surfaces; MIS of source and surface samples with exact probabilities in one measure; source selection with nonzero support for every active source and the exact selection probability stored; emitted radiance counted once; delta reflection handled separately (§12).
- Production limit: one camera path per internal pixel per frame, at most four surface hits total including the camera hit and mirror hits; light sampled at the last eligible surface with MIS weights matching the strategies that can run; iterative loop with a hard bound; reference mode allows more hits and many samples (§12).
- Deterministic seeds; distinct pixel/sample/bounce/purpose dimensions; NaN/inf/negative/zero-pdf detection reported in validation mode (§12).
- Raw real-time, progressive reference (frozen scene, no denoising), and later real-time reconstruction as independently selectable modes (§13).
- T03 tolerance 1e-6 in the documented radiance scale; T04 uses a dedicated sealed-room scene; T08 compares source, surface, and combined estimators on patch averages; T09 checks the correct response to emitter area, not brightness preservation; raw comparisons use patch means within max(relative tolerance, three standard errors) (§19).

## Definitions used by every task

- Radiance convention: scene-linear RGB, unitless "radiance scale" where an emitter with `radiance = (1,1,1)` produces irradiance `E = pi` at a point directly under an infinite emitter. No watts or lumens are claimed.
- Analytic check (Lambertian rectangle, sides `a` and `b`, at height `h` above a point on a parallel plane, point under the rectangle's centre): `E = 4 * (L/2) * [ (a2/sqrt(a2*a2+h*h)) * atan(b2/sqrt(a2*a2+h*h)) + (b2/sqrt(b2*b2+h*h)) * atan(a2/sqrt(b2*b2+h*h)) ]` with `a2 = a/2`, `b2 = b/2`; the radiance of a Lambertian floor with reflectance `rho` lit only by that rectangle is `rho * E / pi`.
- Strategies: `mis` (production), `light` (NEE only, BSDF-found emission ignored except from the camera or after a delta bounce), `bsdf` (no NEE, emission counted when hit). All three are unbiased for the same path limit and must agree on patch means.

## File Structure

```
src/scene/material.h                       Material types, validation, ranges.
src/scene/scene.h/.cpp                     Scene::AddMaterial, material table, instance material validation.
src/scene/emitters.h/.cpp                  EmitterTable: world-space emitter triangles, areas, power CDF, selection pdfs.
src/scene/rooms.h/.cpp                     Generated slabs (walls with thickness), door frames, rectangle emitters.
src/scene/builtin_scenes.cpp               New scenes: t03_dark_room, t04_sealed, t04_open, t07_bleed, t08_box, t09_rect_light, t09_rect_light_large, mirror_box.
src/scene/expectations.h                   RadianceExpectation (patch, kind: zero | positive | analytic | ratio, tolerances).
src/core/math/radiometry.h/.cpp            RectangleIrradianceBelowCentre and a numerical reference integrator for the test.
src/render/gpu_layouts.h                   MaterialRecord, EmitterRecord, EmitterTriangle, IntegratorConstants, StatsRecord.
shaders/shared/layouts.hlsli               Same records in HLSL.
shaders/shared/sampling.hlsli              Hash RNG, dimension allocation, cosine hemisphere, uniform triangle, ray offset.
shaders/shared/materials.hlsli             BSDF evaluation and sampling per material type.
shaders/trace/path_trace.hlsl              The integrator (mis | light | bsdf), accumulation, stats, display encoding.
src/render/renderer.h/.cpp                 RenderMode, accumulation buffers, integrator constants, emitter upload, reset key, reference batching, stats readback, patch statistics.
src/app/options.h/.cpp                     --mode, --spp, --max-hits, --strategy, --exposure, --seed, --samples-per-frame, --stats, --patch-report.
src/app/application.cpp                    Radiance expectations in --validate; stats output; reference-mode run loop.
tests/cpu/test_material.cpp, test_emitters.cpp, test_radiometry.cpp
tests/gpu/CMakeLists.txt                   T03, T04 (sealed, open), T07, T08 (3 strategies), T09 (2 sizes), mirror ids.
tests/scripts/compare_patches.ps1          Cross-run comparison of stats JSON (used by the T08 Cornell-style scene).
docs/RENDERING.md, docs/TESTS.md, docs/STATUS.md, docs/DECISIONS.md
```

---

### Task 1: Materials and emitter tables on the CPU

**Files:** `src/scene/material.h`, `src/scene/scene.h/.cpp`, `src/scene/emitters.h/.cpp`, `tests/cpu/test_material.cpp`, `tests/cpu/test_emitters.cpp`, `src/render/gpu_layouts.h`, `shaders/shared/layouts.hlsli`, `tests/cpu/test_layouts.cpp`

**Interfaces:**
- `enum class MaterialType : uint32_t { Diffuse = 0, Mirror = 1, Emitter = 2 }`.
- `struct Material { std::string name; MaterialType type; math::Vec3 reflectance; math::Vec3 radiance; bool emitterOn; }`; `std::vector<std::string> ValidateMaterial(const Material&)`: reflectance components in [0, 1], radiance components >= 0 and finite, mirror reflectance <= 1, emitter radiance > 0 when on.
- `Scene::AddMaterial(Material) -> uint32_t` (index; validates); `Scene::Materials()`; `AddInstance` rejects a material index that does not exist; material 0 is a default grey diffuse created by the Scene constructor.
- GPU `MaterialRecord { uint type; uint flags; float pad0, pad1; float reflectance[3]; float pad2; float radiance[3]; float pad3; }` (48 bytes). Flags bit 0 = emitter on.
- `struct EmitterTriangle { uint32_t instanceIndex; uint32_t primitiveIndex; float area; float cdf; }` (16 bytes, cdf within the emitter, last = 1).
- `struct EmitterRecord { uint32_t instanceIndex; uint32_t firstTriangle; uint32_t triangleCount; uint32_t materialIndex; float area; float selectionPdf; float selectionCdf; float pad; float radiance[3]; float pad2; }` (48 bytes).
- `EmitterTable BuildEmitterTable(const Scene&)`: one record per instance whose material is an emitter that is on; triangles in world space; `area` = sum; `power` = luminance(radiance) * area; `selectionPdf = power / totalPower`; `selectionCdf` cumulative. Every active emitter has a positive pdf.
- `IntegratorConstants { uint maxHits; uint strategy; uint emitterCount; uint flags; float exposure; float pad[3]; }` (32 bytes, root CBV b1). Flags bit 0 = reset accumulation, bit 1 = jitter pixels.
- `StatsRecord { uint nanCount; uint infCount; uint negativeCount; uint zeroPdfCount; }` (RWStructuredBuffer u5).

- [ ] Tests: validation ranges; emitter table of a translated and uniformly scaled emitter quad has area scaled by s^2; cdf ends at 1; two emitters with radiance ratio 3:1 and equal area get selection pdfs 0.75/0.25; an off emitter is excluded; layout sizes.
- [ ] Implement; run; commit `feat(scene): materials and emitter tables`.

---

### Task 2: Analytic radiometry helper

**Files:** `src/core/math/radiometry.h/.cpp`, `tests/cpu/test_radiometry.cpp`

- `float RectangleIrradianceBelowCentre(float a, float b, float h, float radiance)` per the formula above.
- `float RectangleIrradianceNumerical(a, b, h, radiance, int n)`: midpoint quadrature of `L * cos_p * cos_l / r^2` over an n x n grid, used only by the test.
- [ ] Test: analytic vs numerical (n = 400) within 1e-3 relative for (a, b, h) = (1, 0.5, 1.5) and (2, 2, 0.8); irradiance under a huge rectangle approaches `pi * L`.
- [ ] Commit `feat(core): analytic rectangle irradiance`.

---

### Task 3: Room kit and M2 scenes with expectations

**Files:** `src/scene/rooms.h/.cpp`, `src/scene/expectations.h`, `src/scene/builtin_scenes.h/.cpp`, `tests/cpu/test_scene.cpp`

**Interfaces:**
- `MakeSlab(name, halfExtents)` (a box), `AddRoom(Scene&, centre, innerSize, wallThickness = 0.15, materials...)`: floor, ceiling, and four wall slabs whose inner faces enclose exactly `innerSize`; `AddDoorway(...)`: replaces one wall with two side pieces and a lintel around a 0.9 x 2.1 m opening; `AddDoor(...)`: a 0.04 m slab instance that closes the opening with 0.01 m overlap, hinged at one edge (transform helper `DoorTransform(angleRadians)`).
- `MakeRectangleEmitter(name, width, depth)`: a quad facing -Y (downward) made of two triangles (emission side is the winding side).
- `struct RadianceExpectation { std::string description; uint32_t x, y, w, h; enum Kind { Zero, Positive, Analytic, RatioGreaterThan }; float expected; float relativeTolerance; float minimum; uint32_t otherX, otherY; }`.
- Scenes (camera and patch coordinates are in normalized [0,1] image space and converted at run time):
  - `t03_dark_room`: 4 x 4 x 2.8 m room, grey diffuse (0.5), camera inside, no emitter. Expectation: whole image Zero (max |radiance| <= 1e-6).
  - `t04_sealed`: same room; a large emitter (radiance 10) *outside* the room 1 m from a wall, facing the wall; a second room-less "outside" floor is not needed. Expectation: whole image Zero.
  - `t04_open`: `t04_sealed` with a doorway in that wall and the door rotated open 90 degrees; the emitter is placed to shine through the opening. Expectation: a patch on the opposite wall Positive (mean > 1e-3).
  - `t09_rect_light`: floor quad 10 x 10 m, reflectance 0.5, one 1 x 0.5 m emitter (radiance 4) at height 1.5 m facing down, camera looking down at the floor centre from the side. Expectation: 5 x 5 pixel patch at the floor point under the emitter centre Analytic = `0.5 * E / pi`, relative tolerance 2 %.
  - `t09_rect_light_large`: emitter 2 x 1 m (area x4) at the same height. Same analytic expectation (different value).
  - `t07_bleed`: room, white floor/walls (0.8), a red box (0.8, 0.1, 0.1) 0.1 m from the left wall, an identical white box near the right wall; camera facing the back wall. Expectation: RatioGreaterThan: R/(G+B) mean of a wall patch next to the red box exceeds the same ratio next to the white box by at least 20 %; plus Positive on both.
  - `t08_box`: closed Cornell-style box with a ceiling rectangle emitter, grey walls, two boxes. Expectations: Positive; three patches written to stats for the cross-run comparison.
  - `mirror_box`: room; a unit-reflectance mirror slab on one wall angled 45 degrees; a coloured box hidden behind a partition from the camera but visible in the mirror; a small emitter. Expectation: HitExpectation on a pixel inside the mirror reflection -> stable id of the hidden box (first non-delta hit), and a pixel next to it -> the wall behind the mirror region; radiance Positive in the mirror patch.
- [ ] Tests: room slabs enclose the requested volume (inner faces at the right coordinates); the closed door leaves no gap (CPU ray cast through the doorway from the emitter side hits the door); emitter quad normal is -Y; scene names list includes the new scenes; every scene builds.
- [ ] Commit `feat(scene): room kit and M2 test scenes`.

---

### Task 4: Shaders: sampling, materials, path tracer

**Files:** `shaders/shared/sampling.hlsli`, `shaders/shared/materials.hlsli`, `shaders/trace/path_trace.hlsl`, `shaders/CMakeLists.txt`

**Contract (write it into docs/RENDERING.md in Task 7):**
- RNG: `uint Hash(uint4 key)` (PCG-style mixing of pixel x, pixel y, sample index, dimension, then seed); `float Rand(...)` = top 24 bits / 2^24. Dimension allocation: camera jitter 0-1; bounce b (0-based) uses `8 + b*8 + {0: emitter selection, 1-2: emitter triangle and point, 3-4: BSDF direction}`.
- Ray offset (`OffsetRay(p, n)` from Ray Tracing Gems ch. 6, "A Fast and Robust Method for Avoiding Self-Intersection"): integer ULP offset scaled by 256 along the geometric normal for |p| >= 1/32, float offset 1/65536 otherwise. `n` is the geometric normal flipped toward the outgoing ray. Shadow rays go from `OffsetRay(p, n_p)` to `OffsetRay(q, n_q_toward_p)` with direction = (q' - p'), tMin = 0, tMax = 1 - 1e-4.
- Diffuse: `f = reflectance / pi`; sample cosine-weighted, `pdf = cos / pi`. Two-sided: the shading normal is the geometric normal flipped toward the incoming ray.
- Mirror: delta; `wo = reflect(wi, n)`; throughput *= reflectance; no NEE at this vertex; `lastDelta = true`.
- Emitter surface: `Le = radiance` when `dot(n_geom_unflipped, -rayDir) > 0` and on; otherwise 0; surface behaves as Diffuse with `reflectance`.
- Loop for `hit = 0 .. maxHits-1`: trace; miss -> break (black environment). On hit: if emitter and (hit == 0 or lastDelta) -> add `throughput * Le` with weight 1; else if emitter and strategy != light -> add with MIS weight `w_bsdf = pdfBsdf^2 / (pdfBsdf^2 + pdfLight^2)` where `pdfLight = selectionPdf * (1/area) * dist^2 / |cos_light|` (0 when the previous vertex could not do NEE, e.g. strategy bsdf -> weight 1). If `hit == maxHits-1`: NEE with weight 1 when strategy != bsdf; break. Else NEE (strategy != bsdf) with `w_light = pdfLight^2 / (pdfLight^2 + pdfBsdf^2)` (weight 1 for strategy light); then sample the BSDF, update throughput `f * cos / pdf`, remember `pdfBsdf` for the next hit.
- Accumulate: `accum += radiance` (sum), `accumSq += radiance * radiance`; `N = sampleIndex + 1`; `linear = accum / N`; `display = sRGB(saturate(linear * exposure))`; hit info x = stable id of the first non-delta hit (or of the last mirror if the path leaves the scene through the mirror), reset on flags bit 0.
- Stats: any non-finite or negative component of the per-sample radiance increments the counters and the sample is replaced by 0 (counted, never hidden).
- [ ] Compile; commit `feat(shaders): path tracer with NEE, MIS, raw/reference accumulation`.

---

### Task 5: Renderer modes, accumulation, statistics

**Files:** `src/render/renderer.h/.cpp`, `src/render/scene_gpu.h/.cpp`

**Interfaces:**
- `enum class RenderMode { Diagnostic, Raw, Reference }`; `struct IntegratorSettings { uint32_t maxHits = 4; Strategy strategy = Mis; float exposure = 1; uint32_t seed = 0; bool jitter = true; uint32_t samplesPerFrame = 1 (Raw) / N (Reference); }`.
- `Renderer::SetMode(RenderMode)`, `SetIntegrator(IntegratorSettings)`, `ResetAccumulation()`, `SampleIndex()`; `RecordTrace` computes the accumulation key (camera, instance revisions, materials, size, mode, settings) and resets when it changes; in Reference mode it records `samplesPerFrame` dispatches, incrementing the sample index in the constants for each.
- `SceneGpu::UpdateEmitters(const Scene&, UploadArena&)` (per frame, rebuild when any emitter instance revision or material changed) and `MaterialsAddress()`.
- `Renderer::ReadStats() -> StatsRecord` and `Readback()` returns the linear radiance image (accum / N) plus the standard-error image derived from accumSq; `PatchStatistics(x, y, w, h) -> { mean[3], standardError[3] }`.
- Root signature: add b1 (IntegratorConstants), t5 materials, t6 emitters, t7 emitter triangles; UAV table u0..u5 (display, linear, hit info, probe, accum, accumSq) plus u6 stats. Diagnostic mode keeps using `camera_view.hlsl`.
- [ ] Commit `feat(render): raw and reference modes, accumulation, emitter upload, statistics`.

---

### Task 6: Application: modes, expectations, stats, tests

**Files:** `src/app/options.h/.cpp`, `src/app/application.cpp`, `tests/gpu/CMakeLists.txt`, `tests/scripts/compare_patches.ps1`, `tests/cpu/test_cli.cpp`

- Options: `--mode diag|raw|reference` (default raw), `--spp N` (reference target, must be >= 1; never silently capped), `--max-hits N` (2..32), `--strategy mis|light|bsdf`, `--exposure F`, `--seed N`, `--samples-per-frame N` (1..64), `--stats FILE` (patch means and standard errors for the scene's patches), `--patch-report` prints them.
- Reference run loop: keep rendering until `SampleIndex() >= spp`, then capture/validate/exit; log progress every second.
- `--validate` in raw/reference mode evaluates `RadianceExpectation`s on the linear readback (Zero: max |value| <= 1e-6; Positive: mean > minimum; Analytic: |mean - expected| <= max(relTol * expected, 3 * se); RatioGreaterThan) and `HitExpectation`s, and requires stats counters == 0.
- CTest (all with `SKIP_RETURN_CODE 3`, headless, fixed seed):
  - `gpu_t03_no_sources`: `--scene t03_dark_room --mode reference --spp 64 --validate`.
  - `gpu_t04_sealed`: `--scene t04_sealed --mode reference --spp 64 --validate`; `gpu_t04_open`: `--scene t04_open --mode reference --spp 64 --validate`.
  - `gpu_t08_analytic_mis|light|bsdf`: `--scene t09_rect_light --mode reference --spp 256 --strategy <s> --validate --max-hits 4`.
  - `gpu_t09_area_small|large`: `--scene t09_rect_light[_large] --mode reference --spp 256 --validate`.
  - `gpu_t07_bleed`: `--scene t07_bleed --mode reference --spp 256 --validate`.
  - `gpu_mirror_ids`: `--scene mirror_box --mode reference --spp 16 --validate`.
  - `gpu_t08_box_strategies`: three runs with `--stats` then `pwsh -File tests/scripts/compare_patches.ps1 a.json b.json c.json --rel 0.03`.
  - `gpu_reference_depth_truncation`: `t08_box` at `--max-hits 4` vs `--max-hits 12` stats; the script reports the difference (informational, records path truncation; not a pass/fail gate).
- [ ] Commit `feat(app): render modes, radiance validation, statistics, M2 GPU tests`.

---

### Task 7: Documentation and status

- `docs/RENDERING.md`: material model and ranges, radiance convention, emitter sampling and probabilities, MIS weights and the path-limit rule, delta handling, ray offset method, RNG dimensions, modes, accumulation and reset policy, stats, display transform, known approximations.
- `docs/TESTS.md`: new tests, tolerances (chosen before running), how to regenerate captures; `docs/STATUS.md`: M2 evidence; `docs/DECISIONS.md`: D-014 (materials as explicit types), D-015 (analytic checks as the M2 reference), D-016 (built-in scenes until M3 JSON), D-017 (RNG choice).
- Save raw and reference captures of `t08_box` and `mirror_box` (spec M2 gate) under `artifacts/m2` and describe them in STATUS.
- [ ] Commit `docs: M2 records`.

---

### Task 8 (after the gate): Rough conductor (GGX)

Add `MaterialType::RoughConductor { reflectance (F0), roughness in [0.02, 1] }` with GGX NDF, Smith height-correlated masking, and VNDF sampling; T10 checks: no invalid values, white-furnace-style energy test documented (expected loss at high roughness recorded as a known approximation), roughness sweep keeps weights finite. Separate plan section once Tasks 1–7 pass.

## Self-review

- Spec coverage: §7 materials/emitters/exposure (Tasks 1, 4, 5); §11 offsets (Task 4); §12 estimator, limits, sampling, invalid values, review criteria (Tasks 4, 7); §13 modes (Tasks 5, 6); §18 invalid-value counts (Tasks 4, 6); §19 T03, T04, T07, T08, T09 (Tasks 3, 6), T05 identity part (mirror_box), T10 partially (Task 8); §20 M2 gate captures (Task 7). Deferred: T05/T06 motion (M3), T10 conductor (Task 8), JSON scenes (M3).
- Placeholders: none. Type names used consistently: `Material`, `EmitterTable`, `EmitterRecord`, `EmitterTriangle`, `IntegratorConstants`, `StatsRecord`, `RadianceExpectation`, `RenderMode`, `IntegratorSettings`.
