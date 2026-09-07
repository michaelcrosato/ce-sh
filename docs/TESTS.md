# Tests

## Commands

```powershell
. .\tools\env.ps1
ctest --preset windows-debug --output-on-failure      # everything
ctest --preset windows-debug -L cpu                    # portable unit tests only
ctest --preset windows-debug -L gpu                    # tests that need the GPU
.\build\windows-debug\bin\lc_cpu_tests.exe --list      # case names
.\build\windows-debug\bin\lc_cpu_tests.exe --filter math
```

GPU tests run `LastCircuit.exe`. Exit code 3 (no adapter with DXR Tier 1.1 and Shader Model 6.5)
is reported by CTest as *skipped* (`SKIP_RETURN_CODE 3`); it is never a pass. A machine without
a supported GPU therefore reports the GPU tests as NOT RUN.

## CPU tests (`cpu_tests`, 39 cases in `tests/cpu`)

| File | Checks |
|---|---|
| `test_log.cpp` | Error counting through level filtering; clock monotonic; timestamp shapes; `LC_THROW` message carries file and line; strong ids |
| `test_cli.cpp` | Unknown/repeated/valueless options are errors; `--opt value` and `--opt=value`; integer and float validation; `AppOptions` mapping; view-name errors list valid names; conflicting flags |
| `test_math.cpp` | Right-handed cross product; rotation directions about each axis; product order; translation column; direction transform ignores translation; inverse round trip with non-uniform scale; singular detection; transpose; `LookAtRh`; projection depth range [0, 1] and axis signs; horizontal-to-vertical FOV; yaw/pitch camera basis; `Mat3x4` rows |
| `test_image_write.cpp` | CRC-32 and Adler-32 known values; PNG signature, IHDR fields, chunk CRCs, chunk order, stored-deflate payload decoded and compared byte for byte; multi-block streams above 65535 bytes; PFM header and bottom-up row order |
| `test_json.cpp` | Nesting, commas, escaping, empty containers, null |
| `test_scene.cpp` | Box is closed and outward-facing; quad faces +Y; mesh validation catches bad indices, degenerate triangles, non-multiple-of-3 counts; stable ids and transform history; every built-in hit expectation agrees with a CPU Möller–Trumbore ray cast (including facing); camera ray directions follow the image axes |
| `test_layouts.cpp` | `sizeof`/`offsetof` of the shared GPU records; view-mode names |

Tolerances are stated in each test (typically `1e-6` for unit-scale float math, `1e-5` for inverse
round trips). They were chosen before the tests were run and must not be widened to pass.

## GPU tests (`tests/gpu/CMakeLists.txt`)

| Test | Command | Pass criteria |
|---|---|---|
| `gpu_list_adapters` | `--list-adapters` | Enumerates adapters, probes feature level, DXR tier, shader model; at least one supported hardware adapter |
| `gpu_validate_rt_triangle` | `--scene rt_triangle --validate --headless --frames 2` | Layout probe 30/30 fields bit-identical; centre pixel hits stable id 1 front-facing; four corners miss; RayQuery facing agrees with the geometric normal on every hit pixel; at least one hit; zero debug-layer errors |
| `gpu_validate_rt_boxes` | `--scene rt_boxes --validate --headless --frames 2 --view ids` | Same checks on the asymmetric scene: left pixel hits id 2, right pixel hits id 3, low pixel hits the floor (id 1), sky and corner miss (T02 handedness) |
| `gpu_validate_rt_boxes_facing` | `--scene rt_boxes --validate --headless --frames 1 --view facing` | Same checks with the facing view selected |
| `gpu_resize_test` | `--scene rt_boxes --resize-test --vsync off` | Window cycles 640x360, 1920x1080, 800x600, 1280x720; after each step the swap chain and render outputs equal the client size; zero debug-layer errors; no crash |

`--validate` prints `VALIDATION PASSED` or `VALIDATION FAILED: n problem(s)` and returns 0 or 1.
The debug layer is on for these runs in Debug builds; the Release preset runs the same tests with
the debug layer off (validation then checks the other criteria only, and the log says so).

## Manual inspection (IMAGE CHECKED)

Produce captures with `--capture` and inspect:

- `normals`: floor light green (+Y), faces toward the camera blue (+Z), a box's +X side reddish,
  -X side teal; hard edges, no speckle, no missing faces.
- `facing`: uniformly green; any red or magenta is a winding or handedness fault.
- `ids`: one flat colour per instance with no bleeding across silhouettes.
- `rt_triangle`: the asymmetric triangle, apex slightly right of centre, lower-right vertex below
  the lower-left one (a mirrored image would put it above).

Approved baselines are not yet stored (`tests/baselines` will hold them from M2 when images carry
radiance and need numerical comparison).

## Not yet implemented

Spec tests T03–T18 need M2 and later features. Reliability loops, benchmark replays, and
distribution tests are NOT RUN.
