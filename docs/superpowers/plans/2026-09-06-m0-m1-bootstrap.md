# Last Circuit M0 + M1 Bootstrap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the Last Circuit engine skeleton on Windows/D3D12 and prove hardware ray tracing with a camera-ray compute pass (inline `RayQuery`) over a BLAS/TLAS, with diagnostic views, GPU timing, capture, a resize test, and an honest environment report (spec milestones M0 and M1).

**Architecture:** A small layered C++20 codebase: `core` (portable utilities, math, image writers, CLI), `scene` (portable mesh/instance registry and generated primitives), `platform` (Win32 window/files), `graphics/d3d12` (device, queue, swap chain, buffers, descriptors, timestamps, acceleration structures, compute pipeline), `render` (frame orchestration, GPU data contracts, capture), `app` (CLI modes and main loop). Shaders compile at build time with the Windows SDK DXC into `.cso` files next to the executable. Column-vector math with row-major storage on both CPU and GPU, verified by a GPU layout probe.

**Tech Stack:** C++20 (MSVC 14.51 / cl 19.51), CMake 4.3 (VS-bundled) with the "Visual Studio 18 2026" x64 generator, Windows SDK 10.0.26100.0 (D3D12, DXGI 1.6, DXC 1.8.2502.11 + dxil.dll), HLSL Shader Model 6.5 compute shaders with inline ray queries. No third-party source dependencies in M0/M1.

**Spec:** `LAST_CIRCUIT_STARTER.md` (sections 2, 3, 8–12, 17–23, 25) and `AGENTS.md`.

## Global Constraints

- C++20, HLSL, Direct3D 12, hardware DXR Tier 1.1 or greater; initial tracer uses compute shaders with inline `RayQuery`; Shader Model 6.5 baseline (spec §3).
- No complete game engine; no production raster-lighting fallback; unsupported hardware receives a clear error (spec §2 H01, H02).
- One scene for camera, reflection, indirect, and source-visibility rays (H04). No baked lighting, hidden fill sources, fake reflections, ambient brightness (H03).
- Game runs offline; no AI service or API key (H06).
- Existing bounded libraries permitted; pin versions; preserve license notices; record in `docs/DEPENDENCIES.md` (§3 dependency policy).
- Preserve user work; no publishing, purchasing, uploading, driver installs, or destructive changes (H07). No disabling GPU timeout recovery.
- Do not invent APIs, files, build results, screenshots, or GPU measurements. Use the evidence labels WRITTEN / COMPILED / CPU TESTED / GPU EXECUTED / IMAGE CHECKED / PERFORMANCE CHECKED / PASSED / FAILED / NOT RUN precisely (§22).
- Metres, seconds, radians; right-handed world with +Y up; camera looks along local -Z with local +X right; column-vector notation `world_position = object_to_world * local_position`; one explicit HLSL matrix layout (§9).
- Reject software adapters; check DXR tier, shader model, formats, device creation; record vendor, device id, name, driver (§3).
- Debug layer and GPU validation options in development builds; named GPU resources; timestamp queries from the first lighting milestone; DRED where supported (§10).
- Never create a zero-size render target; handle resize, minimize, lost focus, exit without corruption (§10).
- `TraceClosest` must not use a first-accepted-hit shortcut; `TraceVisibility` may end at the first valid blocker (§11).
- Precompile shaders during build (§10). A failed shader build must identify shader, entry point, profile, compiler output, and command (§8).
- Required commands: `cmake --preset windows-debug`, `cmake --build --preset windows-debug`, `ctest --preset windows-debug --output-on-failure`, release equivalents, `LastCircuit.exe --scene rt_triangle --validate` (§21). Unknown options must fail clearly.
- Maintain `docs/STATUS.md`, `BUILD.md`, `ARCHITECTURE.md`, `RENDERING.md`, `DEPENDENCIES.md`, `TESTS.md`, `DECISIONS.md`, `KNOWN_ISSUES.md` (§23).
- M0 gate: reproducible build commands and an honest environment report. M1 gate: real GPU capture, clean validation for the tested path, correct hit identifiers, no software renderer presented as RTX execution (§20).

## Environment facts (audited 2026-09-06, this machine)

| Item | Value |
|---|---|
| OS | Windows 11 Home 10.0.26200 (25H2) |
| CPU / RAM | Intel Core i7-14700F (20C/28T) / 31.8 GiB |
| GPU | NVIDIA GeForce RTX 4070 SUPER, 12282 MiB, driver 610.47 (32.0.16.1047, 2026-05-18) |
| Compiler | VS Community 2026 18.9.12120.119, MSVC toolset 14.51.36231, cl 19.51.36256 |
| CMake / Ninja | 4.3.1-msvc1 and 1.13.2, bundled with VS (not on PATH): `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\{CMake\bin,Ninja}` |
| Windows SDK | 10.0.26100.0 (d3d12.h with DXR Tier 1.1, SM 6.9 enum, DRED 2 interfaces) |
| DXC | 1.8.2502.11 with dxil.dll at `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\` |
| Network | github.com and nuget.org reachable |
| Tools | git 2.55, Python 3.14, uv, ImageMagick 7.1.2 |

## File Structure

```
CMakeLists.txt                     Project, options, subdirectories, CTest.
CMakePresets.json                  windows-debug / windows-release configure, build, test presets.
.gitignore
cmake/LcCompilerOptions.cmake      lc_options interface target: warnings-as-errors, defines.
cmake/LcFindDxc.cmake              Locate dxc.exe + dxil.dll; LC_DXC_EXECUTABLE override.
cmake/LcShaders.cmake              lc_add_shader(): build-time DXC custom commands.
cmake/build_info.h.in              Git hash/dirty/config stamped into the binary.
src/core/                          Portable: log, error, clock, cli, ids, image_write, math/.
src/scene/                         Portable: mesh data + validation, primitives, scene registry, camera, builtin scenes.
src/platform/                      Win32: window, files, win_error (HRESULT check).
src/graphics/d3d12/                Device, queue+fence, swap chain, descriptor heap, buffers, textures, upload arena, timestamps, acceleration structures, compute pipeline, root signature.
src/render/                        gpu_layouts (CPU mirrors of HLSL structs), render_snapshot, scene_gpu, renderer, capture, view_mode.
src/app/                           options (CLI → AppOptions), environment_report, application (modes + loop), main.
shaders/shared/layouts.hlsli       FrameConstants, InstanceRecord, MeshRecord, HitInfo texel packing.
shaders/shared/math.hlsli          Small helpers (hash color, safe normalize).
shaders/trace/ray_interface.hlsli  TraceClosest / TraceVisibility on RayQuery.
shaders/trace/camera_view.hlsl     M1 camera-ray diagnostic pass (cs_6_5).
shaders/tests/layout_probe.hlsl    Writes selected constant/record fields for the CPU to compare (T02).
tests/cpu/                         lc_test.h harness + test_*.cpp per module.
tests/gpu/CMakeLists.txt           CTest entries that run LastCircuit.exe --validate; skip code 3.
tools/env.ps1                      Put VS CMake/Ninja on PATH for the current shell.
tools/build.ps1                    Configure + build + test a preset.
docs/*.md                          Eight project records (spec §23).
```

Executable layout after build: `build/<preset>/bin/LastCircuit.exe`, `build/<preset>/bin/shaders/*.cso`, `build/<preset>/bin/lc_cpu_tests.exe`.

Exit codes (all modes): `0` success, `1` failure (validation failed, device removed, unhandled error), `2` usage error, `3` unsupported hardware or environment (also the CTest `SKIP_RETURN_CODE`).

---

### Task 1: Build skeleton, presets, compiler options, test harness, logging

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `.gitignore`, `cmake/LcCompilerOptions.cmake`, `cmake/build_info.h.in`
- Create: `src/core/CMakeLists.txt`, `src/core/log.h`, `src/core/log.cpp`, `src/core/error.h`, `src/core/error.cpp`, `src/core/clock.h`, `src/core/clock.cpp`, `src/core/ids.h`
- Create: `tests/CMakeLists.txt`, `tests/cpu/CMakeLists.txt`, `tests/cpu/lc_test.h`, `tests/cpu/lc_test_main.cpp`, `tests/cpu/test_log.cpp`
- Create: `tools/env.ps1`, `tools/build.ps1`

**Interfaces:**
- Produces: CMake interface target `lc_options`; static library `lc_core`; generated header `build_info.h` with `namespace lc::build { constexpr const char* kGitCommit; constexpr bool kGitDirty; constexpr const char* kConfig; constexpr const char* kVersion; }`.
- Produces: `lc::log::{Init(const std::filesystem::path* file), Shutdown(), SetMinLevel(Level), Write(Level, std::string_view), Trace/Debug/Info/Warn/Error(fmt, args...), ErrorCount()}`; `lc::Error` (std::runtime_error subclass); `LC_THROW(msg)`; `lc::Clock::{Now() seconds since process start (double), Timestamp()}`; strong ids `lc::MeshId`, `lc::InstanceId`, `lc::MaterialId` as `struct { uint32_t value; }` with `IsValid()` and `kInvalidId = 0xFFFFFFFF`.
- Produces: test harness macros `LC_TEST(name)`, `LC_CHECK(cond)`, `LC_CHECK_EQ(a,b)`, `LC_CHECK_NEAR(a,b,tol)`, `LC_REQUIRE(cond)`; `lc_cpu_tests.exe [--list] [--filter substring]` returns 0 when all pass.

- [ ] **Step 1: Write the harness and a first failing test**

`tests/cpu/lc_test.h`:

```cpp
#pragma once
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace lc::test {
struct Case { std::string name; std::function<void()> body; };
std::vector<Case>& Registry();
struct Registrar { Registrar(const char* name, std::function<void()> body); };
void Fail(const char* file, int line, const std::string& message); // records failure, does not throw
struct RequireFailed {};                                            // thrown by LC_REQUIRE
}

#define LC_TEST(name)                                                         \
    static void lc_test_##name();                                             \
    static ::lc::test::Registrar lc_registrar_##name(#name, &lc_test_##name); \
    static void lc_test_##name()

#define LC_CHECK(cond) do { if (!(cond)) ::lc::test::Fail(__FILE__, __LINE__, "LC_CHECK failed: " #cond); } while (0)
#define LC_CHECK_EQ(a, b) do { if (!((a) == (b))) ::lc::test::Fail(__FILE__, __LINE__, "LC_CHECK_EQ failed: " #a " == " #b); } while (0)
#define LC_CHECK_NEAR(a, b, tol) do { if (!(std::fabs(double(a) - double(b)) <= double(tol))) ::lc::test::Fail(__FILE__, __LINE__, "LC_CHECK_NEAR failed: " #a " ~ " #b); } while (0)
#define LC_REQUIRE(cond) do { if (!(cond)) { ::lc::test::Fail(__FILE__, __LINE__, "LC_REQUIRE failed: " #cond); throw ::lc::test::RequireFailed{}; } } while (0)
```

`tests/cpu/lc_test_main.cpp` iterates `Registry()`, applies `--filter`, catches `RequireFailed` and `std::exception` (recorded as failure), prints `[PASS]`/`[FAIL] name` lines and a summary `N passed, M failed`, returns `M == 0 ? 0 : 1`.

`tests/cpu/test_log.cpp`:

```cpp
#include "lc_test.h"
#include "core/log.h"

LC_TEST(log_counts_errors) {
    lc::log::Init(nullptr);
    const size_t before = lc::log::ErrorCount();
    lc::log::Error("test error {}", 42);
    LC_CHECK_EQ(lc::log::ErrorCount(), before + 1);
    lc::log::Shutdown();
}
```

- [ ] **Step 2: Configure and build; verify the test fails to compile (log.h missing)**

Run (after `. .\tools\env.ps1`): `cmake --preset windows-debug` then `cmake --build --preset windows-debug`.
Expected: error, `core/log.h` not found.

- [ ] **Step 3: Implement logging, error, clock, ids, CMake files**

`src/core/log.h` (uses `std::format`, mutex-protected, writes `[ +12.345s] [INFO ] message` to stderr and optional file; `ErrorCount()` counts Error-level writes):

```cpp
#pragma once
#include <filesystem>
#include <format>
#include <string_view>

namespace lc::log {
enum class Level { Trace, Debug, Info, Warn, Error };
void Init(const std::filesystem::path* fileOrNull);
void Shutdown();
void SetMinLevel(Level level);
void Write(Level level, std::string_view message);
size_t ErrorCount();
template <class... Args> void Trace(std::format_string<Args...> f, Args&&... a) { Write(Level::Trace, std::format(f, std::forward<Args>(a)...)); }
template <class... Args> void Debug(std::format_string<Args...> f, Args&&... a) { Write(Level::Debug, std::format(f, std::forward<Args>(a)...)); }
template <class... Args> void Info(std::format_string<Args...> f, Args&&... a)  { Write(Level::Info,  std::format(f, std::forward<Args>(a)...)); }
template <class... Args> void Warn(std::format_string<Args...> f, Args&&... a)  { Write(Level::Warn,  std::format(f, std::forward<Args>(a)...)); }
template <class... Args> void Error(std::format_string<Args...> f, Args&&... a) { Write(Level::Error, std::format(f, std::forward<Args>(a)...)); }
}
```

`CMakePresets.json` (version 6): configure presets `windows-debug` and `windows-release` with generator `Visual Studio 18 2026`, architecture `x64`, `binaryDir` `${sourceDir}/build/${presetName}`; build presets with `configuration` Debug/Release; test presets with `configuration` and `output.outputOnFailure: true`.

`cmake/LcCompilerOptions.cmake`: `add_library(lc_options INTERFACE)` with `/W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8 /EHsc /MP` and definitions `UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS`.

Top-level `CMakeLists.txt`: `cmake_minimum_required(VERSION 3.28)`, `project(LastCircuit VERSION 0.1.0 LANGUAGES CXX)`, C++20 required, no extensions, `CMAKE_RUNTIME_OUTPUT_DIRECTORY_<CONFIG>` = `${CMAKE_BINARY_DIR}/bin`, git stamp via `execute_process(git rev-parse --short=12 HEAD)` and `git status --porcelain` into `build_info.h`, `enable_testing()`, subdirectories `src`, `shaders` (Task 4+), `tests`.

- [ ] **Step 4: Build and run the CPU tests**

Run: `cmake --build --preset windows-debug` then `ctest --preset windows-debug --output-on-failure`.
Expected: `100% tests passed`.

- [ ] **Step 5: Commit**

```bash
git checkout -b m0-m1-bootstrap
git add CMakeLists.txt CMakePresets.json .gitignore cmake src/core tests tools
git commit -m "build: CMake skeleton, presets, core logging and test harness"
```

---

### Task 2: Strict CLI parser and app options

**Files:**
- Create: `src/core/cli.h`, `src/core/cli.cpp`, `tests/cpu/test_cli.cpp`
- Create: `src/app/options.h`, `src/app/options.cpp` (app-specific; compiled into the app target in Task 8; unit-tested through `lc_core` + a small `lc_app_options` static library)

**Interfaces:**
- Produces: `lc::ArgParser` with `AddFlag(name, help)`, `AddOption(name, help, defaultValue)`, `std::optional<std::string> Parse(std::span<const std::string> args)` (returns an error message for unknown options, missing values, or repeated options), `bool Has(name)`, `std::string Get(name)`, `std::optional<int> GetInt(name)` (error when not an integer), `std::string Usage()`.
- Produces: `lc::AppOptions { std::string scene = "rt_triangle"; uint32_t width = 1280, height = 720; ViewMode view = ViewMode::Normals; bool headless, validate, resizeTest, listAdapters, debugLayer, gpuValidation, vsync = true; int adapterIndex = -1; uint32_t frames = 0; float horizontalFovDegrees = 90; std::optional<std::filesystem::path> capture, envReport, logFile; }` and `ParseAppOptions(args) -> std::variant<AppOptions, std::string /*usage error*/>`.

- [ ] **Step 1: Failing tests** in `tests/cpu/test_cli.cpp`: unknown option `--bogus` is an error; `--frames 3` and `--frames=3` both give 3; `--frames x` is an error; missing value at end is an error; flag present/absent; `ParseAppOptions({"--scene","rt_boxes","--view","ids","--headless"})` yields scene `rt_boxes`, `ViewMode::InstanceIds`, `headless == true`; `--view nonsense` is an error listing valid views.
- [ ] **Step 2: Run** `lc_cpu_tests --filter cli`; expect compile failure/failed tests.
- [ ] **Step 3: Implement** `cli.cpp` (single pass over args; accepts `--name value`, `--name=value`; a value may start with a digit or `-` only when the option takes a value) and `options.cpp` (maps to `AppOptions`, `ViewModeFromName`).
- [ ] **Step 4: Run** tests; expect pass.
- [ ] **Step 5: Commit** `feat(core): strict CLI parser and app options`.

---

### Task 3: Math layer with the coordinate contract

**Files:**
- Create: `src/core/math/vec.h`, `src/core/math/mat.h`, `src/core/math/mat.cpp`, `src/core/math/camera_math.h`, `src/core/math/camera_math.cpp`, `tests/cpu/test_math.cpp`

**Interfaces:**
- Produces: `lc::math::Vec2/Vec3/Vec4` (float, operators, `Dot`, `Cross`, `Length`, `Normalize`, `NearlyEqual(a,b,tol)`), `lc::math::Mat4` (`float m[4][4]`, `m[row][col]`, column-vector convention `p' = M * p`; `Identity()`, `Translation(Vec3)`, `RotationX/Y/Z(rad)`, `Scale(Vec3)`, `operator*(Mat4)`, `operator*(Vec4)`, `TransformPoint(Vec3)`, `TransformDirection(Vec3)`, `Transposed()`, `Inverse()` general 4x4 via cofactors, `Determinant()`), `lc::math::Mat3x4 { float m[3][4]; static Mat3x4 FromMat4(const Mat4&); }`.
- Produces: `LookAtRh(eye, target, up) -> Mat4` (world→view; view space looks down -Z), `PerspectiveRhZeroToOne(fovYRad, aspect, nearZ, farZ) -> Mat4` (D3D clip depth in [0,1], near → 0, far → 1), `HorizontalToVerticalFov(hfovRad, aspect)`.

- [ ] **Step 1: Failing tests** (asymmetric, so mirrored mistakes cannot pass):

```cpp
LC_TEST(math_rotation_y_is_right_handed) {
    // +90 degrees about +Y maps +X to -Z in a right-handed system.
    auto r = lc::math::Mat4::RotationY(lc::math::kPi / 2);
    auto p = r.TransformPoint({1, 0, 0});
    LC_CHECK_NEAR(p.x, 0, 1e-6); LC_CHECK_NEAR(p.y, 0, 1e-6); LC_CHECK_NEAR(p.z, -1, 1e-6);
}
LC_TEST(math_translation_then_rotation_order) {
    // M = T * R applies R first, then T (column vectors).
    auto m = lc::math::Mat4::Translation({10, 0, 0}) * lc::math::Mat4::RotationZ(lc::math::kPi / 2);
    auto p = m.TransformPoint({1, 0, 0}); // R: (1,0,0)->(0,1,0); T: -> (10,1,0)
    LC_CHECK_NEAR(p.x, 10, 1e-6); LC_CHECK_NEAR(p.y, 1, 1e-6); LC_CHECK_NEAR(p.z, 0, 1e-6);
}
LC_TEST(math_inverse_round_trip) { /* T*R*S with S=(2,3,4): Inverse()*M ~ Identity within 1e-5 */ }
LC_TEST(math_direction_ignores_translation) { /* TransformDirection of (0,0,1) under Translation(5,6,7) is (0,0,1) */ }
LC_TEST(math_lookat_maps_target_to_negative_z) { /* eye (0,0,5), target origin: origin -> (0,0,-5) in view space; a point at world (1,0,0) has view x = +1 */ }
LC_TEST(math_perspective_depth_range) { /* z=-near -> clip z/w = 0, z=-far -> 1, x>0 -> clip x>0 */ }
LC_TEST(math_mat3x4_layout_rows) { /* FromMat4 copies m[r][c] for r<3 including translation column */ }
LC_TEST(math_cross_right_handed) { /* Cross(X, Y) == Z */ }
```

- [ ] **Step 2: Run** `lc_cpu_tests --filter math`; expect failure.
- [ ] **Step 3: Implement** (`kPi` constexpr; `RotationY(a)`: `m = [[c,0,s,0],[0,1,0,0],[-s,0,c,0],[0,0,0,1]]`; `LookAtRh`: `f = normalize(target-eye)`, `s = normalize(cross(f, up))`, `u = cross(s, f)`, rows `[s, -dot(s,eye)]`, `[u, -dot(u,eye)]`, `[-f, dot(f,eye)]`; `PerspectiveRhZeroToOne`: `yScale = 1/tan(fovY/2)`, `xScale = yScale/aspect`, `m[2][2] = far/(near-far)`, `m[2][3] = near*far/(near-far)`, `m[3][2] = -1`, `m[3][3] = 0`).
- [ ] **Step 4: Run** tests; expect pass.
- [ ] **Step 5: Commit** `feat(core): math layer with column-vector, right-handed contract`.

---

### Task 4: Image writers (PNG stored-deflate, PFM) with CRC/Adler tests

**Files:**
- Create: `src/core/image_write.h`, `src/core/image_write.cpp`, `tests/cpu/test_image_write.cpp`

**Interfaces:**
- Produces: `lc::ImageRgba8 { uint32_t width, height; std::vector<uint8_t> pixels; }` (top-down rows, 4 bytes/pixel), `lc::ImageRgbF32 { uint32_t width, height; std::vector<float> pixels; }` (top-down rows, 3 floats/pixel), `std::vector<uint8_t> EncodePng(const ImageRgba8&)`, `std::vector<uint8_t> EncodePfm(const ImageRgbF32&)` (header `PF\n<w> <h>\n-1.0\n`, bottom-up, little-endian), `uint32_t Crc32(std::span<const uint8_t>)`, `uint32_t Adler32(std::span<const uint8_t>)`.

- [ ] **Step 1: Failing tests**: `Crc32("123456789") == 0xCBF43926`; `Adler32("Wikipedia") == 0x11E60398`; PNG of a 2x2 image begins with the 8-byte signature, IHDR says 2x2, bit depth 8, color type 6, all chunk CRCs valid, IEND last; the IDAT payload is a zlib stream (`0x78 0x01`) whose stored blocks concatenate to `filter byte 0 + row` per row and whose Adler32 trailer matches; PFM header parses and the first stored row is the bottom image row.
- [ ] **Step 2: Run**; expect failure.
- [ ] **Step 3: Implement**: raw scanlines = for each row: `0x00` + RGBA bytes; zlib = `78 01` + stored blocks (max 65535 bytes each, header byte `BFINAL` on the last, `LEN`, `NLEN` little-endian) + big-endian Adler32; PNG chunks with big-endian length and CRC32 over type+data.
- [ ] **Step 4: Run**; expect pass. Also verify externally once: write the test PNG to the scratchpad and run `magick identify` on it.
- [ ] **Step 5: Commit** `feat(core): lossless PNG and PFM writers`.

---

### Task 5: Scene data: meshes, validation, generated primitives, scene registry, camera, built-in scenes

**Files:**
- Create: `src/scene/CMakeLists.txt`, `src/scene/mesh.h`, `src/scene/mesh.cpp`, `src/scene/primitives.h`, `src/scene/primitives.cpp`, `src/scene/scene.h`, `src/scene/scene.cpp`, `src/scene/camera.h`, `src/scene/camera.cpp`, `src/scene/builtin_scenes.h`, `src/scene/builtin_scenes.cpp`, `tests/cpu/test_scene.cpp`

**Interfaces:**
- Produces: `lc::MeshData { std::string name; std::vector<math::Vec3> positions; std::vector<uint32_t> indices; }` with counter-clockwise front faces in a right-handed frame; `std::vector<std::string> ValidateMesh(const MeshData&)` (index out of range, index count not a multiple of 3, non-finite position, degenerate triangle with area below 1e-10 m², more than 1,000,000 triangles).
- Produces: `MakeTriangle(p0, p1, p2)`, `MakeQuad(center, halfWidthAlongX, halfDepthAlongZ, normalUp = true)` (an XZ quad facing +Y), `MakeBox(halfExtents)` (12 triangles, outward CCW faces, 8 unique vertices duplicated per face for flat normals: 24 vertices, 36 indices).
- Produces: `lc::Scene` with `MeshId AddMesh(MeshData)`, `InstanceId AddInstance(std::string name, MeshId, const math::Mat4& objectToWorld, uint32_t materialIndex = 0)` (stable ids start at 1 and never reuse), `void SetTransform(InstanceId, const math::Mat4&)` (bumps `transformRevision`, keeps `prevObjectToWorld` until `CommitRenderedFrame()`), `void CommitRenderedFrame()` (copies current to previous for every instance), `const std::vector<Mesh>& Meshes() const`, `const std::vector<Instance>& Instances() const` where `Instance { InstanceId id; std::string name; MeshId mesh; uint32_t materialIndex; math::Mat4 objectToWorld, prevObjectToWorld; uint32_t transformRevision; }`.
- Produces: `lc::Camera { math::Vec3 position; float yawRadians, pitchRadians; float horizontalFovRadians; float nearZ = 0.05f, farZ = 200.0f; math::Mat4 ViewToWorld() const; math::Mat4 WorldToView() const; float VerticalFov(float aspect) const; }` (`ViewToWorld = Translation(position) * RotationY(yaw) * RotationX(pitch)`).
- Produces: `lc::HitExpectation { float u, v; uint32_t expectedStableId /*0 = miss*/; bool expectFrontFace; std::string description; }`, `lc::SceneDescription { std::string name; Scene scene; Camera camera; std::vector<HitExpectation> expectations; }`, `std::optional<SceneDescription> BuildBuiltinScene(std::string_view name)`, `std::vector<std::string> BuiltinSceneNames()` (`rt_triangle`, `rt_boxes`).

Built-in scenes: `rt_triangle` = camera at (0, 1.6, 4) yaw 0 pitch 0 hfov 90°, one asymmetric triangle at z=0 with vertices (-1.0,0.6,0), (1.2,0.4,0), (0.3,2.6,0) (CCW seen from +Z; front face toward the camera); expectations: (0.5,0.5) hits id 1 front-facing; (0.02,0.02), (0.98,0.02), (0.02,0.98), (0.98,0.98) miss. `rt_boxes` = floor quad 6x6 m at y=0 (id 1), box half-extents (0.4,0.4,0.4) at (-1.5,0.4,0) (id 2), box half-extents (0.4,0.9,0.4) at (1.5,0.9,0) (id 3), camera at (0,1.5,4.5) pitch -0.15 rad; expectations: (0.25,0.55) hits id 2, (0.75,0.45) hits id 3, (0.5,0.85) hits id 1 (floor), (0.5,0.05) misses.

- [ ] **Step 1: Failing tests**: box has 36 indices and passes validation; every box face normal (cross product of first two edges) points away from the box centre (outward, CCW); a mesh with an out-of-range index reports an error; stable ids are 1,2,3 for three instances and `SetTransform` bumps the revision without changing `prevObjectToWorld` until `CommitRenderedFrame()`; `rt_triangle` builds and the centre expectation is inside the triangle when projected with the camera (CPU ray-triangle test using Möller–Trumbore in the test file).
- [ ] **Step 2: Run**; expect failure.
- [ ] **Step 3: Implement**.
- [ ] **Step 4: Run**; expect pass.
- [ ] **Step 5: Commit** `feat(scene): mesh validation, generated primitives, scene registry, builtin scenes`.

---

### Task 6: GPU data contracts (CPU mirrors of HLSL layouts) and shader build pipeline

**Files:**
- Create: `shaders/CMakeLists.txt`, `cmake/LcFindDxc.cmake`, `cmake/LcShaders.cmake`
- Create: `shaders/shared/layouts.hlsli`, `shaders/shared/math.hlsli`, `shaders/trace/ray_interface.hlsli`, `shaders/trace/camera_view.hlsl`, `shaders/tests/layout_probe.hlsl`
- Create: `src/render/gpu_layouts.h`, `src/render/view_mode.h`, `tests/cpu/test_layouts.cpp`

**Interfaces:**
- Produces (HLSL and C++ identical byte layouts, `static_assert`ed):

```hlsl
#pragma pack_matrix(row_major)            // memory m[row][col]; math uses column vectors: mul(M, v)
struct FrameConstants {                   // 256 bytes
    float4x4 viewToWorld;                 // camera-local -> world
    float4x4 worldToView;
    float4x4 viewToClip;                  // D3D clip depth [0,1]
    float3   cameraPosition; float tanHalfFovY;
    uint2    renderSize;     float2 invRenderSize;
    uint frameIndex; uint sampleIndex; uint viewMode; uint instanceCount;
    uint seed; uint flags; float rayTMin; float rayTMax;
    float aspectRatio; float pad0; float pad1; float pad2;
};
struct InstanceRecord {                   // 112 bytes
    float4 objectToWorldRow[3];           // rows 0..2 of the 4x4 (translation in .w)
    float4 prevObjectToWorldRow[3];
    uint meshIndex; uint materialIndex; uint stableId; uint transformRevision;
};
struct MeshRecord { uint firstVertex; uint firstIndex; uint vertexCount; uint indexCount; }; // 16 bytes
// Hit info texel (RWTexture2D<uint4>): x = stableId or 0xFFFFFFFF on miss, y = primitiveIndex, z = frontFace, w = instanceIndex
```

- Produces: `lc::ViewMode { Normals = 0, InstanceIds = 1, Depth = 2, Barycentrics = 3, FrontFace = 4, PrimitiveIds = 5 }` with `ViewModeFromName`, `ViewModeName`.
- Produces: CMake function `lc_add_shader(<out-list-var> <source> <entry> <profile>)`; target `lc_shaders` producing `bin/shaders/camera_view.cso` and `bin/shaders/layout_probe.cso`; `LC_DXC_EXECUTABLE` cache variable.
- Produces: `shaders/trace/ray_interface.hlsli` with

```hlsl
struct ClosestHit { bool hit; float t; uint instanceIndex; uint instanceId; uint primitiveIndex; float2 barycentrics; bool frontFace; float3x4 objectToWorld; };
ClosestHit TraceClosest(RaytracingAccelerationStructure scene, float3 origin, float3 direction, float tMin, float tMax);
bool TraceVisibility(RaytracingAccelerationStructure scene, float3 origin, float3 direction, float tMin, float tMax); // true when unoccluded
```

`TraceClosest` uses `RayQuery<RAY_FLAG_FORCE_OPAQUE>` and loops `while (q.Proceed()) {}` then reads `Committed*`. `TraceVisibility` uses `RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH>`.

- [ ] **Step 1: Failing test** `tests/cpu/test_layouts.cpp`: `sizeof(lc::gpu::FrameConstants) == 256`, `offsetof(FrameConstants, cameraPosition) == 192`, `offsetof(FrameConstants, renderSize) == 208`, `sizeof(InstanceRecord) == 112`, `offsetof(InstanceRecord, meshIndex) == 96`, `sizeof(MeshRecord) == 16`; `ViewModeFromName("ids") == InstanceIds`.
- [ ] **Step 2: Run**; expect failure.
- [ ] **Step 3: Implement** `gpu_layouts.h` with `alignas(16)` structs and the static_asserts; write the shaders; `LcFindDxc.cmake` reads `HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots;KitsRoot10`, prefers `bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64/dxc.exe`, requires `dxil.dll` beside it; `LcShaders.cmake`:

```cmake
function(lc_add_shader OUT_LIST SOURCE ENTRY PROFILE)
    get_filename_component(_name "${SOURCE}" NAME_WE)
    set(_out "${LC_SHADER_OUTPUT_DIR}/${_name}.cso")
    set(_pdb "${LC_SHADER_OUTPUT_DIR}/${_name}.pdb")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND "${LC_DXC_EXECUTABLE}" -T ${PROFILE} -E ${ENTRY} -HV 2021 -WX -Zi -Fd "${_pdb}"
                "$<IF:$<CONFIG:Debug>,-Od,-O3>" -I "${CMAKE_SOURCE_DIR}/shaders"
                -Fo "${_out}" "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" ${LC_SHADER_HEADERS}
        COMMENT "DXC ${SOURCE} entry=${ENTRY} profile=${PROFILE}"
        VERBATIM)
    set(${OUT_LIST} ${${OUT_LIST}} "${_out}" PARENT_SCOPE)
endfunction()
```

`camera_view.hlsl` (entry `main`, `cs_6_5`, `[numthreads(8,8,1)]`): pixel centre → NDC (flip Y so +Y is up) → view direction `(ndc.x * tanHalfFovY * aspect, ndc.y * tanHalfFovY, -1)` → world direction `mul((float3x3)viewToWorld, dirView)` → `TraceClosest` → geometric normal from the three world-space vertices `normalize(cross(p1 - p0, p2 - p0))` → colour by `viewMode` → write `gDisplay` (R8G8B8A8_UNORM), `gLinear` (R32G32B32A32_FLOAT), `gHitInfo` (R32G32B32A32_UINT). Miss colour: black for every view.

`layout_probe.hlsl` (entry `main`, `cs_6_5`, `[numthreads(1,1,1)]`) writes 32 uints to `RWStructuredBuffer<uint> gProbe : register(u3)`: `asuint(viewToWorld[0][3])`, `asuint(viewToWorld[1][3])`, `asuint(viewToWorld[2][3])`, `asuint(viewToWorld[3][3])`, `asuint(worldToView[1][2])`, `asuint(viewToClip[2][3])`, `asuint(cameraPosition.z)`, `asuint(tanHalfFovY)`, `renderSize.x`, `renderSize.y`, `frameIndex`, `viewMode`, `instanceCount`, `flags`, `asuint(rayTMax)`, `asuint(aspectRatio)`, then from `gInstances[1]`: `asuint(objectToWorldRow[0].w)`, `asuint(objectToWorldRow[2].w)`, `asuint(prevObjectToWorldRow[1].y)`, `meshIndex`, `materialIndex`, `stableId`, `transformRevision`, then from `gMeshes[1]`: `firstVertex`, `firstIndex`, `vertexCount`, `indexCount`, then `asuint(gPositions[2].y)`, `gIndices[4]`, and `0xC0FFEE` as a sentinel.

- [ ] **Step 4: Build** `cmake --build --preset windows-debug`; expect `bin/shaders/camera_view.cso` and `layout_probe.cso` to exist and `lc_cpu_tests --filter layout` to pass. Deliberately introduce a syntax error in `camera_view.hlsl`, build, and confirm the error output names the shader, entry, profile, and DXC diagnostic; revert.
- [ ] **Step 5: Commit** `feat(render): GPU data contracts and build-time DXC shader compilation`.

---

### Task 7: D3D12 graphics layer

**Files:**
- Create: `src/platform/CMakeLists.txt`, `src/platform/win_error.h`, `src/platform/win_error.cpp`, `src/platform/files.h`, `src/platform/files.cpp`, `src/platform/window.h`, `src/platform/window.cpp`
- Create: `src/graphics/CMakeLists.txt`, `src/graphics/d3d12/d3d12_common.h`, `device.h/.cpp`, `graphics_queue.h/.cpp`, `swap_chain.h/.cpp`, `descriptor_heap.h/.cpp`, `gpu_buffer.h/.cpp`, `gpu_texture.h/.cpp`, `upload_arena.h/.cpp`, `timestamp_queries.h/.cpp`, `acceleration_structure.h/.cpp`, `compute_pipeline.h/.cpp`

**Interfaces:**
- `lc::CheckHr(HRESULT, const char* expr, const char* file, int line)` throws `lc::Error` with `expr`, HRESULT hex, system message, file:line; macro `LC_CHECK_HR(expr)`.
- `lc::files::{ReadBinaryFile(path) -> std::vector<uint8_t>, WriteBinaryFile(path, span) -> bool, ExecutableDirectory() -> path, EnsureDirectory(path)}`.
- `lc::Window(const WindowDesc{ std::wstring title; uint32_t clientWidth, clientHeight; bool visible; })`; `HWND Handle()`, `void PumpMessages()`, `const WindowEvents& Events()` (`closeRequested`, `resized`, `clientWidth`, `clientHeight`, `minimized`, `focusLost`, `focusGained`, `escapePressed`), `void ClearEvents()`, `void SetClientSize(w, h)`, `void SetTitle(std::wstring)`. Process is per-monitor-v2 DPI aware; client size is physical pixels.
- `lc::gfx::AdapterInfo { std::wstring description; uint32_t vendorId, deviceId, subSysId, revision; uint64_t dedicatedVideoMemory, sharedSystemMemory; bool software; std::string driverVersion; uint32_t index; }`; `lc::gfx::DeviceCapabilities { D3D12_RAYTRACING_TIER raytracingTier; D3D_SHADER_MODEL highestShaderModel; bool tearingSupported; bool rootSignature1_1; uint64_t videoMemoryBudget, videoMemoryUsage; }`; `lc::gfx::DeviceOptions { bool debugLayer, gpuValidation, dred; int adapterIndex = -1; }`.
- `lc::gfx::Device`: `static std::vector<AdapterInfo> EnumerateAdapters()`; ctor throws `lc::UnsupportedHardware` (subclass of `Error`) when no non-software adapter supports feature level 12_1, DXR Tier 1.1, and SM 6.5; `ID3D12Device5* Get()`, `IDXGIFactory6* Factory()`, `const AdapterInfo& Adapter()`, `const DeviceCapabilities& Caps()`, `size_t InfoQueueErrorCount()` (messages with severity ERROR or CORRUPTION observed so far), `void DrainInfoQueue()` (logs new messages), `void ReportDeviceRemoved()` (logs `GetDeviceRemovedReason` and DRED breadcrumbs/page fault when available), `void SetName(ID3D12Object*, std::wstring_view)` helper.
- `lc::gfx::GraphicsQueue(Device&)`: `ID3D12CommandQueue* Get()`, `uint64_t Signal()`, `void WaitForFenceValue(uint64_t)`, `void WaitIdle()`, `uint64_t CompletedValue()`, `uint64_t TimestampFrequency()`.
- `lc::gfx::SwapChain(Device&, GraphicsQueue&, HWND, uint32_t w, uint32_t h, bool vsync)`: `void Resize(w, h)` (waits idle; never zero), `ID3D12Resource* CurrentBackBuffer()`, `uint32_t CurrentIndex()`, `void Present()`, `uint32_t Width()/Height()`.
- `lc::gfx::DescriptorHeap(Device&, D3D12_DESCRIPTOR_HEAP_TYPE, uint32_t capacity, bool shaderVisible)`: `DescriptorHandle Allocate()` → `{ D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu; uint32_t index; }`, `ID3D12DescriptorHeap* Get()`.
- `lc::gfx::GpuBuffer`: `static GpuBuffer CreateDefault(Device&, size, D3D12_RESOURCE_FLAGS, D3D12_RESOURCE_STATES initial, name)`, `CreateUpload(...)`, `CreateReadback(...)`, `ID3D12Resource* Get()`, `D3D12_GPU_VIRTUAL_ADDRESS Address()`, `uint64_t Size()`, `void* Map()`, `void Unmap()`. Plus `UploadToDefaultBuffer(Device&, ID3D12GraphicsCommandList*, GpuBuffer& dst, span data, D3D12_RESOURCE_STATES finalState) -> GpuBuffer stagingToKeepAlive`.
- `lc::gfx::GpuTexture2D`: `static GpuTexture2D CreateUav(Device&, w, h, DXGI_FORMAT, name)` (state UNORDERED_ACCESS), `ID3D12Resource* Get()`, `Width()/Height()/Format()`, `ReadbackPlan Plan(Device&)` (footprint, row pitch, total bytes), `void RecordCopyToReadback(cmdList, GpuBuffer& readback, const ReadbackPlan&)` (transitions UAV→COPY_SOURCE→UAV around the copy).
- `lc::gfx::UploadArena(Device&, size, name)`: `Allocation Allocate(size, alignment)` → `{ void* cpu; D3D12_GPU_VIRTUAL_ADDRESS gpu; }`, `void Reset()`.
- `lc::gfx::TimestampQueries(Device&, GraphicsQueue&, uint32_t framesInFlight, uint32_t maxTimersPerFrame)`: `void BeginFrame(uint32_t frameSlot)`, `void Begin(cmdList, const char* name)`, `void End(cmdList, const char* name)`, `void Resolve(cmdList)`, `std::vector<TimerResult> Collect(uint32_t frameSlot)` (call after that slot's fence completed) → `{ std::string name; double milliseconds; }`.
- `lc::gfx::Blas`: `void Build(Device&, ID3D12GraphicsCommandList4*, const BlasGeometry{ D3D12_GPU_VIRTUAL_ADDRESS vertexBuffer; uint32_t vertexCount; uint32_t vertexStride; D3D12_GPU_VIRTUAL_ADDRESS indexBuffer; uint32_t indexCount; }&, name)` (allocates result + scratch; records the build and a UAV barrier; keep scratch alive until the fence completes), `D3D12_GPU_VIRTUAL_ADDRESS Address()`, `uint64_t ResultSize()`.
- `lc::gfx::Tlas`: `void Reserve(Device&, uint32_t maxInstances, name)`, `void RecordBuild(ID3D12GraphicsCommandList4*, D3D12_GPU_VIRTUAL_ADDRESS instanceDescs, uint32_t instanceCount)` (UAV barriers before and after), `D3D12_GPU_VIRTUAL_ADDRESS Address()`.
- `lc::gfx::CreateRootSignature(Device&, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC&, name) -> ComPtr<ID3D12RootSignature>`; `lc::gfx::ComputePipeline(Device&, ID3D12RootSignature*, const std::filesystem::path& csoFile, name)`: `ID3D12PipelineState* Get()`.

- [ ] **Step 1: Write the failing GPU smoke test** as the CTest entry `gpu_device_report` = `LastCircuit.exe --list-adapters` (Task 8 wires the command); for now add a temporary `tests/gpu/device_smoke.cpp` executable `lc_device_smoke` that creates `Device`, logs adapter and caps, creates `GraphicsQueue`, signals and waits, and returns 0; CTest property `SKIP_RETURN_CODE 3`.
- [ ] **Step 2: Build**; expect link errors (classes absent).
- [ ] **Step 3: Implement** the layer. Key sequences:

Device creation:

```cpp
UINT factoryFlags = 0;
if (opts.debugLayer) {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        if (opts.gpuValidation) { ComPtr<ID3D12Debug1> d1; if (SUCCEEDED(debug.As(&d1))) d1->SetEnableGPUBasedValidation(TRUE); }
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    } else { log::Warn("D3D12 debug layer unavailable (install the Graphics Tools optional feature)"); }
}
if (opts.dred) {
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
        dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
}
LC_CHECK_HR(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)));
// Enumerate with EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, ...), skip DXGI_ADAPTER_FLAG_SOFTWARE,
// try D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device5)), check OPTIONS5.RaytracingTier >= TIER_1_1
// and D3D12_FEATURE_SHADER_MODEL (probe 6_9 .. 6_5 until CheckFeatureSupport succeeds) >= 6_5; first passing adapter wins
// unless opts.adapterIndex selects one (then a failure is UnsupportedHardware with the exact missing feature).
// Driver version: LARGE_INTEGER v; adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &v) -> "%u.%u.%u.%u".
// Video memory: IDXGIAdapter3::QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info).
// Tearing: IDXGIFactory5::CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)).
// Info queue: ID3D12InfoQueue1::RegisterMessageCallback when available; otherwise poll GetMessage in DrainInfoQueue().
```

BLAS build:

```cpp
D3D12_RAYTRACING_GEOMETRY_DESC geom{};
geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
geom.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT; geom.Triangles.IndexCount = g.indexCount; geom.Triangles.IndexBuffer = g.indexBuffer;
geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; geom.Triangles.VertexCount = g.vertexCount;
geom.Triangles.VertexBuffer = { g.vertexBuffer, g.vertexStride };
D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL; in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
in.NumDescs = 1; in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs = &geom;
D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
device.Get()->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
// result: default heap, ALLOW_UNORDERED_ACCESS, initial state RAYTRACING_ACCELERATION_STRUCTURE; scratch: default heap, ALLOW_UNORDERED_ACCESS, state UNORDERED_ACCESS
D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC b{}; b.Inputs = in; b.DestAccelerationStructureData = result.Address(); b.ScratchAccelerationStructureData = scratch.Address();
list->BuildRaytracingAccelerationStructure(&b, 0, nullptr);
D3D12_RESOURCE_BARRIER uav{}; uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; uav.UAV.pResource = result.Get(); list->ResourceBarrier(1, &uav);
```

TLAS: same with `TOP_LEVEL`, `in.InstanceDescs = instanceDescsGpuVa` (16-byte aligned upload memory holding `D3D12_RAYTRACING_INSTANCE_DESC` with `InstanceID = stableId`, `InstanceMask = 0xFF`, `Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE`, `AccelerationStructure = blas.Address()`, `Transform` = rows 0..2 of `objectToWorld`).

Swap chain: `DXGI_SWAP_CHAIN_DESC1{ Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, SampleDesc {1,0}, DXGI_USAGE_RENDER_TARGET_OUTPUT, BufferCount 3, DXGI_SCALING_NONE, DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_ALPHA_MODE_IGNORE, Flags = tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0 }`, `CreateSwapChainForHwnd`, `MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)`; `Present(vsync ? 1 : 0, (!vsync && tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0)`; `Resize` waits idle, releases buffers, `ResizeBuffers(3, w, h, DXGI_FORMAT_UNKNOWN, flags)`.

- [ ] **Step 4: Build and run** `ctest --preset windows-debug -R gpu_device_smoke`; expect PASS with the adapter, driver, DXR tier, and shader model logged; no debug-layer errors.
- [ ] **Step 5: Commit** `feat(graphics): D3D12 device, queue, swap chain, resources, timestamps, acceleration structures`.

---

### Task 8: Renderer, scene upload, capture, application modes, environment report

**Files:**
- Create: `src/render/CMakeLists.txt`, `src/render/render_snapshot.h`, `src/render/scene_gpu.h/.cpp`, `src/render/renderer.h/.cpp`, `src/render/capture.h/.cpp`
- Create: `src/app/CMakeLists.txt`, `src/app/environment_report.h/.cpp`, `src/app/application.h/.cpp`, `src/app/main.cpp`
- Create: `tests/gpu/CMakeLists.txt`
- Remove: `tests/gpu/device_smoke.cpp` (superseded by `--list-adapters`)

**Interfaces:**
- `lc::RenderSnapshot { const Scene* scene; Camera camera; uint32_t frameIndex; ViewMode view; }`.
- `lc::SceneGpu(Device&, GraphicsQueue&, const Scene&)`: uploads all mesh positions and indices into two default-heap buffers (documented setup wait), builds one BLAS per mesh, fills `MeshRecord`s; `void UpdateInstances(const Scene&, UploadArena&, ID3D12GraphicsCommandList4*)` writes `InstanceRecord`s (upload heap, per frame) and instance descs and rebuilds the TLAS when any `transformRevision` changed or on first use; getters for buffer addresses and `Tlas&`.
- `lc::Renderer(Device&, GraphicsQueue&, uint32_t w, uint32_t h)`: `void Resize(w, h)`, `void BeginFrame()`, `void RecordTrace(SceneGpu&, const RenderSnapshot&)`, `void RecordCopyToPresentable(ID3D12Resource* backBuffer)`, `uint64_t EndFrame()` (closes, executes, signals, resolves timestamps), `const std::vector<TimerResult>& LastTimings()`, `uint32_t Width()/Height()`, `CaptureImages Readback()` (full wait; returns `ImageRgba8 display`, `ImageRgbF32 linear`, `std::vector<uint32_t> hitInfo` (4 uints per pixel)).
- `lc::WriteCapture(const std::filesystem::path& dir, const std::string& baseName, const CaptureImages&, const CaptureMetadata&)` writes `<base>.png`, `<base>.pfm`, `<base>.json` with fields: scene, view, frameIndex, sampleIndex, seed, pathDepth, exposure, renderSize, outputSize, reconstruction, adapter, driver, buildCommit, buildConfig, timingsMs.
- `lc::WriteEnvironmentReport(path, const Device* deviceOrNull)` JSON: os version (RtlGetVersion), cpu brand (`__cpuid`), physical memory, adapters, selected adapter caps, build info.
- `lc::Application(const AppOptions&)`: `int Run()` implementing modes: `--list-adapters`, default windowed loop (Escape or close exits; `--frames N` exits after N presented frames), `--headless` (no window/swap chain), `--validate` (after rendering: info-queue error count must be 0; every `HitExpectation` of the scene must match the hit-info readback; the layout probe must match; prints `VALIDATION PASSED`/`FAILED` and returns 0/1), `--resize-test` (windowed: cycles client sizes 1280x720 → 640x360 → 1920x1080 → 800x600 → 1280x720 rendering 3 frames each, asserting the output size follows and no debug-layer errors), `--capture <dir>`.

- [ ] **Step 1: Failing tests** in `tests/gpu/CMakeLists.txt`:

```cmake
add_test(NAME gpu_list_adapters COMMAND LastCircuit --list-adapters)
add_test(NAME gpu_validate_rt_triangle COMMAND LastCircuit --scene rt_triangle --validate --headless --frames 2)
add_test(NAME gpu_validate_rt_boxes COMMAND LastCircuit --scene rt_boxes --validate --headless --frames 2 --view ids)
add_test(NAME gpu_resize_test COMMAND LastCircuit --scene rt_boxes --resize-test)
set_tests_properties(gpu_list_adapters gpu_validate_rt_triangle gpu_validate_rt_boxes gpu_resize_test PROPERTIES SKIP_RETURN_CODE 3 LABELS gpu)
```

- [ ] **Step 2: Build**; expect failures (no executable).
- [ ] **Step 3: Implement**. Root signature: `[0] CBV b0`, `[1] SRV t0 (TLAS)`, `[2] SRV t1 (InstanceRecord)`, `[3] SRV t2 (MeshRecord)`, `[4] SRV t3 (positions, StructuredBuffer<float3>)`, `[5] SRV t4 (indices, StructuredBuffer<uint>)`, `[6] table UAV u0..u3 (display, linear, hitInfo, probe)` with `DESCRIPTORS_VOLATILE | DATA_VOLATILE`. Dispatch `(w+7)/8, (h+7)/8, 1`. Frame sequence per spec §9: pump input → (no simulation yet) → snapshot → update instances/TLAS → trace → copy to back buffer → present; timestamps named `tlas_build`, `trace`, `copy_out`, `frame_gpu`.
- [ ] **Step 4: Run** `ctest --preset windows-debug --output-on-failure`; expect all CPU and GPU tests to pass. Run `LastCircuit.exe --scene rt_boxes --headless --frames 1 --capture artifacts/m1` and inspect the PNG (IMAGE CHECKED: left box, right taller box, floor, normals colouring plausible, no acne at box/floor contact).
- [ ] **Step 5: Commit** `feat(app): renderer, scene upload, capture, validation and resize modes`.

---

### Task 9: Documentation, status, decisions, README, AGENTS pointer

**Files:**
- Create: `docs/BUILD.md`, `docs/ARCHITECTURE.md`, `docs/RENDERING.md`, `docs/DEPENDENCIES.md`, `docs/TESTS.md`, `docs/STATUS.md`, `docs/DECISIONS.md`, `docs/KNOWN_ISSUES.md`
- Modify: `README.md` (replace stub), `AGENTS.md` (add a pointer to `docs/BUILD.md`)

- [ ] **Step 1:** Write each record with actual results only (evidence labels; NOT RUN where applicable). STATUS uses the spec template. DECISIONS records D-001..D-010 (generator choice, in-box SDK instead of Agility SDK, own math layer, CCW front faces via instance flag, exceptions for init errors, in-house test harness, no third-party deps in M0/M1, 2 frames in flight, build-time DXC, console subsystem for development builds).
- [ ] **Step 2:** Re-run the full `windows-release` configure/build/test and paste the summarized outcome into STATUS.
- [ ] **Step 3: Commit** `docs: M0/M1 records, README, status`.

---

## Self-review

- **Spec coverage:** §3 fixed technology (Task 1, 6, 7), §8 structure (all), §9 contracts (Task 3, 6), §10 D3D12 requirements (Task 7, 8: debug layer, GPU validation, names, timestamps, fences, resize, DRED), §11 acceleration structures and ray interfaces (Task 6, 7), §18 capture (Task 8), §19 T01/T02 (Task 8 validate + layout probe + asymmetric `rt_boxes`), §20 M0/M1 gates (Task 8, 9), §21 commands (Task 1, 8), §23 records (Task 9). Deferred to the M2 plan: §4 two-room scene, §7 materials and emitters, §12 integrator, §13 denoising, §14–16 gameplay/audio/assets, §17 benchmark protocol, T03–T18.
- **Placeholders:** none; every step names files, code, or exact commands.
- **Type consistency:** `ViewMode` defined in `src/render/view_mode.h` (Task 6) and consumed by `AppOptions` (Task 2 — `options.cpp` includes it; the `lc_app_options` library links `lc_render_layouts` interface target), `Scene`/`Camera` (Task 5) consumed by `RenderSnapshot`/`SceneGpu` (Task 8), `ImageRgba8`/`ImageRgbF32` (Task 4) consumed by `Renderer::Readback` and `WriteCapture` (Task 8), `TimerResult` (Task 7) consumed by `Renderer::LastTimings` (Task 8).
