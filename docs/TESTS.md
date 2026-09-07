# Tests

## Commands

```powershell
. .\tools\env.ps1
ctest --preset windows-debug --output-on-failure      # everything (about 30 s on the reference machine)
ctest --preset windows-debug -L cpu                    # portable unit tests only
ctest --preset windows-debug -L gpu                    # tests that need the GPU
.\build\windows-debug\bin\lc_cpu_tests.exe --list      # case names
.\build\windows-debug\bin\lc_cpu_tests.exe --filter math
```

GPU tests run `LastCircuit.exe`. Exit code 3 (no adapter with DXR Tier 1.1 and Shader Model 6.5)
is reported by CTest as *skipped* (`SKIP_RETURN_CODE 3`); it is never a pass. A machine without
a supported GPU therefore reports the GPU tests as NOT RUN.

Tolerances were chosen before the tests were run and are recorded here; they must not be widened
to pass. When a scene was redesigned (T07, see below) the tolerance stayed and the scene changed.

## CPU tests (`cpu_tests`, 50 cases in `tests/cpu`)

| File | Checks |
|---|---|
| `test_log.cpp` | Error counting through level filtering; clock monotonic; timestamp shapes; `LC_THROW` message carries file and line; strong ids |
| `test_cli.cpp` | Unknown/repeated/valueless options are errors; `--opt value` and `--opt=value`; integer and float validation; `AppOptions` mapping; view-name errors list valid names; conflicting flags |
| `test_math.cpp` | Right-handed cross product; rotation directions; product order; translation column; direction transform ignores translation; inverse round trip with non-uniform scale; singular detection; transpose; `LookAtRh`; projection depth range and axis signs; FOV conversion; yaw/pitch basis; `Mat3x4` rows |
| `test_image_write.cpp` | CRC-32 and Adler-32 known values; PNG signature, IHDR, chunk CRCs and order, stored-deflate payload decoded byte for byte, multi-block streams; PFM header and row order |
| `test_json.cpp` | Nesting, commas, escaping, empty containers, null |
| `test_material.cpp` | Material ranges (no energy gain, no negative or non-finite values, only emitters emit, active emitters need radiance); luminance weights; default material; material index validation; emitter on/off revision |
| `test_emitters.cpp` | Emitter areas follow instance transforms (uniform scale 2 gives area x4); per-emitter triangle CDF ends at 1; power-proportional selection (radiance 3:1 gives 0.75/0.25); off sources excluded; empty table |
| `test_radiometry.cpp` | Closed-form rectangle irradiance vs 400x400 midpoint quadrature (1e-3 relative) at the centre and at arbitrary points inside, on the edge, and outside the footprint (2e-3); limit `pi * L` for a huge rectangle |
| `test_scene.cpp` | Box closed and outward; quad faces +Y; mesh validation; stable ids and transform history; every built-in hit expectation agrees with a CPU Möller–Trumbore ray cast that follows mirrors; every radiance patch is visible and unoccluded; projection inverts ray generation; `LookAt`; room slabs enclose the volume with solid corners and 0.15 m thickness; the closed door blocks 9 rays through the doorway and clears it when open; rectangle emitters face the requested axis |
| `test_layouts.cpp` | `sizeof`/`offsetof` of every shared GPU record; view-mode names |

## GPU tests (`tests/gpu/CMakeLists.txt`)

### M1: geometry and identifiers

| Test | Command | Pass criteria |
|---|---|---|
| `gpu_list_adapters` | `--list-adapters` | At least one supported hardware adapter |
| `gpu_validate_rt_triangle` | `--scene rt_triangle --mode diag --validate --headless --frames 2` | Layout probe 30/30 fields bit-identical; centre pixel hits id 1 front-facing; four corners miss; RayQuery facing agrees with the geometric normal on every non-grazing hit pixel; zero debug-layer errors |
| `gpu_validate_rt_boxes` | `--scene rt_boxes --mode diag --validate --headless --frames 2 --view ids` | Same on the asymmetric scene (ids 2 left, 3 right, floor 1, misses) |
| `gpu_validate_rt_boxes_facing` | `... --view facing` | Same with the facing view |
| `gpu_resize_test` | `--scene rt_boxes --mode raw --resize-test --vsync off` | Window cycles 640x360, 1920x1080, 800x600, 1280x720; swap chain and outputs follow; no errors |

### M2: raw light transport (spec §19 T03–T09)

All run `--mode reference --headless --validate` with a fixed seed (default 0) and the tolerances
stored in the scene descriptions (`src/scene/builtin_scenes.cpp`). Every run also requires the
invalid-value counters (NaN, inf, negative, zero pdf) to be zero and zero debug-layer errors.

| Test | Scene, samples | Pass criteria |
|---|---|---|
| `gpu_t03_no_sources` | `t03_dark_room`, 32 spp: sealed grey room, one *off* emitter panel, a crate | Every pixel `max |radiance| <= 1e-6` |
| `gpu_t04_sealed_room` | `t04_sealed`, 32 spp: sealed room, a radiance-10 panel 1 m outside the +Z wall facing it | Every pixel `<= 1e-6` |
| `gpu_t04_open_door` | `t04_open`, 64 spp: the same wall with a doorway and the door open 90 degrees | Floor patch 1 m inside the door: mean luminance `> 1e-3` |
| `gpu_t08_analytic_mis` / `_light` / `_bsdf` | `t09_rect_light`, 256 / 256 / 4096 spp, strategies `mis`, `light`, `bsdf`: 10 x 10 m floor (albedo 0.5), 1 x 0.5 m emitter of radiance 4 at 1.5 m facing down | Floor radiance under the emitter centre equals `0.5 * E / pi` with `E` from the closed-form rectangle irradiance within `max(2 %, 3 SE)`; a second patch beside the footprint within `max(3 %, 3 SE)` |
| `gpu_t09_area_large` | `t09_rect_light_large`, 256 spp: emitter 2 x 1 m (area x4) | Same analytic checks with the larger emitter's closed form (the response is not 4x: near field) |
| `gpu_t07_indirect_colour` | `t07_bleed`, 256 spp: white room, red -X wall, white +X wall, ceiling panel, a crate | `R/(G+B)` of the floor patch 0.15 m from the red wall exceeds 1.2x the same ratio 0.15 m from the white wall; both patches lit |
| `gpu_t08_box_positive` | `t08_box`, 64 spp: closed Cornell-style box | Floor, red wall, and back wall patches lit |
| `gpu_mirror_identity_and_energy` | `mirror_box`, 64 spp: 4 x 7 m room, unit mirror on the -Z wall, orange box and a small wall lamp behind the camera | Mirror pixel that reflects the box reports the box's stable id as the first non-mirror hit; a neighbouring mirror pixel reports the +Z wall; the mirror centre's radiance equals the wall lamp's radiance (3.0) within 0.1 % (delta path, noise-free); the mirrored box is lit |

Cross-run comparisons via `tests/scripts/compare_runs.ps1` (patch means per channel within
`max(relTol * |ref|, 3 * sqrt(se_a^2 + se_b^2))`):

| Test | Runs | Pass criteria |
|---|---|---|
| `gpu_t08_box_strategies_agree` | `t08_box`: `mis` 512 spp 4 hits, `light` 1024 spp 4 hits, `bsdf` 4096 spp **5** hits | Every patch and channel agrees within 3 % (observed: within 1 %). `bsdf` gets one extra hit so all three cover the same path family (see RENDERING.md, "Path families") |
| `gpu_t07_agrees_with_higher_sample_reference` | `t07_bleed`: `mis` 2048 spp vs 256 spp | Within 2 % or 3 SE (spec T07: agreement with a higher-sample reference) |
| `gpu_t08_box_depth_truncation_report` | `t08_box`: 12 hits vs 4 hits, 512 spp | Informational (never fails): reports the truncation bias of the 4-hit production budget |

### Mirror expectation derivation (`mirror_box`)

Camera at (0, 1.2, 3.0) looking along -Z; mirror front face at z = -1.98 (4.98 m away). A ray that
hits the mirror at `(xm, ym, -1.98)` reflects into direction `(xm, ym - 1.2, 4.98)` (unnormalized)
and reaches the box's front face at z = 3.2 after `s = (3.2 + 1.98) / 4.98 = 1.0402`, where
`x = xm (1 + s)` and `y = 1.2 + (ym - 1.2)(1 + s)`. The box front-face centre (0.8, 0.4) therefore
appears at `xm = 0.392`, `ym = 0.808`. The mirror centre reflects straight back through the camera
to the wall lamp at (0, 1.2, 4.99).

## Manual inspection (IMAGE CHECKED)

Regenerate with `--capture` (see `docs/BUILD.md`) and inspect:

- M1 diagnostic views: floor +Y green, faces toward the camera blue, a box's +X side reddish; the
  `facing` view uniformly green; one flat colour per instance in `ids`.
- `t08_box` reference: soft shadows under and beside both blocks, red and green colour bleeding on
  the floor and the block sides near the coloured walls, no fireflies, no black seams at wall
  joints, the emitter panel white.
- `mirror_box`: the orange box and the two lamps visible only inside the mirror, correct left-right
  (the box that stands to the camera's right appears on the right side of the mirror image), a
  sharp mirror edge.
- `t04_open`: light enters only through the doorway; the open leaf casts a shadow; the region seen
  through the doorway above the panel is black (nothing exists outside).

Approved image baselines are not yet stored; radiance is currently checked numerically as above.

## Not yet implemented

T05/T06 motion (M3), T10 rough conductor (planned), T11 numerical-offset sweeps (partly covered by
T03/T04 seals), T12–T18.
