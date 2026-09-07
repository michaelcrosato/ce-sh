# Last Circuit M4 Stable Real-Time Image Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A third, independently selectable render path ("denoised") that turns the one-sample-per-pixel path tracer into a stable real-time image: documented guide buffers, NRD REBLUR diffuse+specular denoising with the Primary Surface Replacement method for the planar mirror, explicit history handling, diagnostic views, frame-sequence captures, a measured temporal test (T12), and the benchmark command with a complete report (spec M4 gate: "Temporal tests pass. Record the full moving sequence and performance results.").

**Architecture:** The path tracer gains a second compiled variant (`path_trace_guided`) that follows delta mirror chains to the first non-mirror surface (PSR), writes NRD's guide inputs (normal+roughness, view Z, world-space motion) for that virtual surface, splits the sampled radiance into a diffuse and a specular signal demodulated by material factors, and records the noise-free direct emission separately. A native D3D12 backend for NRD's graphics-agnostic API (`NrdDenoiser`) creates the pipelines, pool textures, descriptors, and barriers that NRD's dispatch descriptions ask for. A composition pass modulates the denoised signals back, adds emission, exposes and encodes the display image, accumulates the recomposed raw samples for an invariant test, and draws diagnostic overlays on the display image only. Raw and reference modes are untouched. NRD, ShaderMake, and MathLib are pinned git submodules built from source with the SDK's DXC; nothing is downloaded at configure or build time.

**Tech Stack:** unchanged plus NRD v4.17.3 (static library, DXIL only), ShaderMake (NRD's pinned commit), MathLib v11.

**Spec:** `LAST_CIRCUIT_STARTER.md` §3 (dependency policy, "Evaluate NRD"), §9 (history invalidation events), §12 (estimator unchanged), §13 (three paths, NRD contract table, PSR for the mirror, history handling, upscaling order), §17 (targets, benchmark protocol), §18 (views, captures, frame sequences), §19 T12 and the temporal image targets, §20 M4 gate, §21 (`--benchmark-seconds`, `--report`).

## Global Constraints (in addition to the M1–M3 plans)

- Three independently selectable paths: raw real-time, progressive reference, real-time reconstruction (§13). Raw and reference outputs must be bit-for-bit what they were.
- Buffer contract table in `docs/RENDERING.md` for every NRD input: format, units, coordinate space, range, invalid value, source pass, lifetime (§13).
- "Do not infer a buffer's semantics from its name. Use SDK packing helpers where required." (§13) All packing goes through `NRD.hlsli` functions.
- PSR: "Do not replace only depth and leave incompatible motion or normals." Keep the ray-traced mirror radiance unchanged; no duplicate geometry (§13).
- History: reject history for newly visible surfaces (NRD does this from the guides); handle camera cuts, teleports, resize, scene reload, and source changes explicitly; continuous lamp movement must not reset every frame; bounded history; expose history age and rejection diagnostics (§13).
- Diagnostic colours never enter the production lighting output (§18). Captures come from the running program; temporal faults need a frame sequence plus the event log (§18).
- Dependencies: fixed commits, license recorded in `docs/DEPENDENCIES.md`, one acquisition method (git submodules), no build-time fetches (§3).
- Tolerances chosen before evaluation; a trail metric with a defined local contrast threshold (§19).
- Frame generation off; reconstruction (DLSS) only after the native path is stable (§13, §17).

## Definitions

- **Guided trace**: the path tracer variant that, per pixel, also produces NRD inputs for the PSR surface.
- **PSR surface**: the first non-mirror surface along the camera path after zero or more ideal mirror reflections. Its *virtual* position lies on the primary ray at the unfolded distance; its virtual normal is the surface normal reflected through every mirror plane in reverse order; its virtual motion is the world motion reflected the same way.
- **Signal**: radiance gathered from the PSR vertex onward, divided by the material factor (`NRD_MaterialFactors`, which includes the mirror chain throughput folded into the albedo/F0 term), packed with the normalized hit distance of the BSDF-sampled continuation ray.
- **History reset**: `nrd::AccumulationMode::RESTART` for one frame plus a reset of the raw accumulation. Triggered by: first frame, resize, scene reload, explicit request (blackout test), camera cut (snapshot flag).
- **Trail lag** (T12 metric): in a patch that a moving object leaves, the number of frames between the first frame where no pixel of the patch reports the object's identity and the first frame where the denoised patch luminance reaches `settleFraction` of its final value (mean over the last 12 frames). Pass when `lag <= maxLagFrames` (6 frames = 100 ms at 60 Hz).

## File Structure

```
external/NRD, external/ShaderMake, external/MathLib     git submodules (v4.17.3, 18f5a344, v11)
cmake/LcNrd.cmake                                        options and add_subdirectory for the three; SDK DXC handed to ShaderMake
shaders/shared/reconstruction.hlsli                      GuideConstants (b2), guide view ids, PSR helpers shared by the tracer and compose
shaders/trace/path_trace.hlsl                            + LC_GUIDES sections (compiled twice: path_trace.cso, path_trace_guided.cso)
shaders/post/compose.hlsl                                modulation, emission, exposure/sRGB, raw accumulation, diagnostic overlays
shaders/post/upscale.hlsl                                bilinear internal -> output display (scaled presentation path, KI-001)
src/render/nrd_denoiser.h/.cpp                           NrdDenoiser: D3D12 backend for the NRD API
src/render/gpu_layouts.h + shaders/shared/layouts.hlsli  GuideConstants mirror, new UAV slots, view ids
src/render/renderer.h/.cpp                               RenderMode::Denoised, DenoiserSettings, guide textures, RecordDenoised, history reset, readback additions, scaled present
src/render/view_mode.h                                   new views: motion, viewz, history, diffuse, specular, direct, indirect, validation, raw
src/render/capture.h/.cpp                                reconstruction fields, extra images (motion) for sequences
src/game/replay.h/.cpp                                   check kinds: motion, static_motion, trail_lag, denoised_patch_positive/dark
src/app/options.h/.cpp, application.cpp                  --mode denoised, --internal WxH, --history-frames, --validation-overlay, --no-antifirefly,
                                                         --blackout-at-frame, --capture-sequence/--capture-from/--capture-to/--capture-every/--capture-crop,
                                                         --benchmark-seconds/--warmup-seconds/--report
src/app/benchmark.h/.cpp                                 frame-time statistics and the JSON report
tests/cpu/test_reconstruction.cpp                        Halton jitter, column-major conversion, trail-lag evaluation, benchmark statistics, new replay fields
tests/gpu/CMakeLists.txt                                 M4 tests (see below)
docs/*                                                   contract table, dependency records, tests, status, decisions, issues, build steps
```

## Buffer contract (to be copied into docs/RENDERING.md, filled with the final values)

| Input | Format | Units / space | Range | Invalid | Source pass | Lifetime |
|---|---|---|---|---|---|---|
| IN_MV | RGBA16F | metres, world space, `prev - current` of the virtual PSR position; camera motion excluded | any finite | 0 on miss | guided trace | frame |
| IN_NORMAL_ROUGHNESS | R10G10B10A2_UNORM via `NRD_FrontEnd_PackNormalAndRoughness` | unit world-space virtual normal; linear roughness (`sqrt(alpha)` = our `roughness` field); materialID 0 diffuse, 1 conductor | roughness [0.02, 1], diffuse = 1 | miss: (0,0,1), 1, 0 | guided trace | frame |
| IN_VIEWZ | R32F | metres, view-space z of the virtual position (negative in front of the camera) | (-far, 0) | miss: `-2 * denoisingRange` | guided trace | frame |
| IN_DIFF_RADIANCE_HITDIST | RGBA16F via `REBLUR_FrontEnd_PackRadianceAndNormHitDist` | scene-linear radiance / diffFactor (YCoCg inside), normalized hit distance of the cosine-sampled continuation | radiance [0, 65504] | 0 / 0 | guided trace | frame |
| IN_SPEC_RADIANCE_HITDIST | RGBA16F, same helper | scene-linear radiance / specFactor, normalized hit distance of the VNDF-sampled continuation | same | 0 / 0 | guided trace | frame |
| OUT_DIFF/SPEC_RADIANCE_HITDIST | RGBA16F via `REBLUR_BackEnd_UnpackRadianceAndNormHitDist` | denoised signal; `.w` = history length in frames (`returnHistoryLengthInsteadOfOcclusion`) | | undefined outside denoisingRange | NRD | frame |
| Material factors | 2 x RGBA16F | `diffFactor`, `specFactor` from `NRD_MaterialFactors` with the mirror chain throughput folded in | (0.02, 1] | 0 | guided trace | frame |
| Emission | RGBA16F | scene-linear radiance seen directly at the PSR vertex (times mirror throughput), noise-free | any | 0 | guided trace | frame |
| Direct | RGBA16F | demodulated direct-light contribution at the PSR vertex (diagnostic only) | | 0 | guided trace | frame |

Common settings: column-major matrices (transposed from our row-major storage), `isMotionVectorInWorldSpace = true`, `motionVectorScale = {1,1,1}`, `cameraJitter` = the global Halton(2,3) offset in pixels within [-0.5, 0.5], `denoisingRange = 500 m`, `disocclusionThreshold = 0.01`, `frameIndex` incremented every frame, `accumulationMode = RESTART` on reset events, `timeDeltaBetweenFrames` = frame period in ms (16.667 for replays, measured for live play).

REBLUR settings: defaults except `maxAccumulatedFrameNum = 30` (0.5 s at 60 Hz), `maxFastAccumulatedFrameNum = 6`, `historyFixFrameNum = 3`, `hitDistanceParameters` default (metres), `minMaterialForDiffuse = minMaterialForSpecular = 0` (exact material-ID comparison), `enableAntiFirefly = true`, `returnHistoryLengthInsteadOfOcclusion = true`.

---

### Task 1: Build NRD, ShaderMake, and MathLib from the submodules with the SDK compiler

**Files:** `cmake/LcNrd.cmake` (new), `CMakeLists.txt` (include LcFindDxc before externals, include LcNrd), `shaders/CMakeLists.txt` (LcFindDxc moved to the root), `docs/DEPENDENCIES.md`, `docs/BUILD.md` (submodule step), `.gitignore` if NRD writes into its tree.

**Produces:** targets `NRD` (static), `ShaderMakeBlob`, `MathLib`; the variable `LC_NRD_SHADER_INCLUDE_DIR = external/NRD/Shaders` for our shader compiler; `NRDConfig.hlsli` generated at configure time.

- [ ] Read `external/ShaderMake/LICENSE.txt` and `external/MathLib/LICENSE.txt`; record all three in DEPENDENCIES.md with purpose and redistribution conditions (NRD: NVIDIA RTX SDKs license; attribution required in credits or documentation; no open-source relicensing of the SDK; owner review flagged).
- [ ] `cmake/LcNrd.cmake`: set `SHADERMAKE_FIND_COMPILERS OFF`, `SHADERMAKE_TOOL ON`, `SHADERMAKE_DXC_PATH = LC_DXC_EXECUTABLE` (cache internal), `NRD_STATIC_LIBRARY ON`, `NRD_EMBEDS_DXBC_SHADERS OFF`, `NRD_EMBEDS_SPIRV_SHADERS OFF`, `NRD_EMBEDS_DXIL_SHADERS ON`, `NRD_SUPPORTS_CHECKERBOARD OFF`, `NRD_SHADERS_PATH = ${CMAKE_BINARY_DIR}/nrd_shaders`; `add_subdirectory` ShaderMake, MathLib, NRD; fail with a clear message when a submodule directory is empty ("run git submodule update --init --recursive").
- [ ] Configure and build both presets; confirm NRD compiles its shaders with the SDK DXC (no download), `NRD.lib` links, and the existing 33 tests still pass (COMPILED, CPU TESTED, GPU EXECUTED).
- [ ] Commit: `build: NRD v4.17.3, ShaderMake, MathLib as pinned submodules built with the SDK DXC`.

### Task 2: NrdDenoiser, the D3D12 backend for the NRD API

**Files:** `src/render/nrd_denoiser.h/.cpp`, `src/render/CMakeLists.txt` (link NRD), `src/graphics/d3d12/gpu_texture.*` (SRV creation helper, `CreateUavSrv` with initial state), `src/graphics/d3d12/device.*` (typed UAV load checks for RGBA16F, R32F, R10G10B10A2).

**Interface:**
```cpp
struct NrdInputs { gfx::GpuTexture2D* motion; gfx::GpuTexture2D* normalRoughness; gfx::GpuTexture2D* viewZ;
                   gfx::GpuTexture2D* diffIn; gfx::GpuTexture2D* specIn; gfx::GpuTexture2D* diffOut; gfx::GpuTexture2D* specOut;
                   gfx::GpuTexture2D* validation; /* optional */ };
class NrdDenoiser {
public:
  NrdDenoiser(gfx::Device&, std::uint32_t width, std::uint32_t height);   // creates the instance (REBLUR_DIFFUSE_SPECULAR), pipelines, pools, heaps
  ~NrdDenoiser();
  void SetCommonSettings(const nrd::CommonSettings&);                      // per frame
  void SetReblurSettings(const nrd::ReblurSettings&);
  // Records every NRD dispatch with barriers; constants come from the frame arena; descriptors from the frame's heap slice.
  void Record(ID3D12GraphicsCommandList4*, gfx::UploadArena&, std::uint32_t frameSlot, const NrdInputs&);
  std::uint64_t PoolBytes() const; std::uint32_t DispatchCount() const; std::string VersionText() const;
};
```
- Root signature: root CBV `b0, space1`; static samplers `s0` nearest clamp and `s1` linear clamp in `space1`; one descriptor table with an SRV range `t0..t(perSetTexturesMaxNum-1), space0` and a UAV range `u0..u(perSetStorageTexturesMaxNum-1), space0` (offsets 0 and perSetTexturesMaxNum), flags DESCRIPTORS_VOLATILE.
- Pipelines from `PipelineDesc::computeShaderDXIL`; entry point is fixed by NRD (`shaderEntryPoint`).
- Pool textures: `permanentPool` then `transientPool`, size `ceil(dim / downsampleFactor)`, formats mapped from `nrd::Format` (unsupported formats throw), UAV|SRV allowed, state tracked per texture.
- Per dispatch: transition each resource to NON_PIXEL_SHADER_RESOURCE (TEXTURE) or UNORDERED_ACCESS (STORAGE_TEXTURE); a UAV barrier when a storage resource stays in UAV state between dispatches; write the descriptors into this frame's heap slice (`setsMaxNum * (tex + storage)` descriptors per frame slot); copy `constantBufferData` into the arena (256-aligned) unless `constantBufferDataMatchesPreviousDispatch`; `Dispatch(gridWidth, gridHeight, 1)`.
- After the last dispatch, transition the application's inputs/outputs back to UNORDERED_ACCESS and update the `GpuTexture2D` state.
- Unit test (CPU, no GPU): the `nrd::Format -> DXGI_FORMAT` table covers every enum value; the column-major conversion of a known matrix.
- Commit: `feat(render): NrdDenoiser, a D3D12 backend for the NRD dispatch API`.

### Task 3: Guided path tracer variant (PSR, guides, split signals)

**Files:** `shaders/shared/reconstruction.hlsli`, `shaders/shared/layouts.hlsli`, `src/render/gpu_layouts.h`, `shaders/trace/path_trace.hlsl`, `shaders/CMakeLists.txt` + `cmake/LcShaders.cmake` (defines and an output-name argument; NRD include path), `tests/gpu` layout probe extended with `GuideConstants` fields.

**GuideConstants (b2, 48 bytes):** `float2 jitterPixels; float2 invRenderSize2 (unused, keep 16-byte rows); float3 hitDistParams; float denoisingRange; uint flags (LC_GUIDE_FLAG_GLOBAL_JITTER, LC_GUIDE_FLAG_ACCUMULATE_RESET); uint viewMode; float exposure; float historyFrames;`

**Tracer changes (under `#if LC_GUIDES`):**
- The camera ray uses `pixel + 0.5 + jitterPixels` when the global-jitter flag is set (the same offset for every pixel this frame).
- Track the delta chain: `unfoldedDistance += hit.t` for every mirror hit; `mirrorThroughput *= reflectance`; store up to 4 mirror normals; the PSR vertex is the first non-delta hit (index `mirrorBounces`).
- At the PSR vertex: `virtualPosition = camera + primaryDir * unfoldedDistance` (equals the reflected position for planar mirrors); `virtualNormal = ReflectChain(shadingNormal)` applied in reverse mirror order; `viewZ = (worldToView * virtualPosition).z`; world motion `m = prevObjectToWorld * objectPoint - objectToWorld * objectPoint` from the object-space barycentric point, then `ReflectChainDirection(m)`; materialID 0 (diffuse, emitter surface) or 1 (conductor); roughness 1 for diffuse, the material's for conductors.
- Radiance split: everything the path gathers from the PSR vertex onward goes into `diff` (diffuse / emitter surfaces) or `spec` (conductors) — one lobe per pixel, the other stays 0 with hit distance 0. Emission seen at vertices before or at the PSR (the mirror chain, the PSR emitter itself) goes into `emission` and is not part of the signals. The direct NEE term at the PSR vertex is also written to `direct` (diagnostic).
- Demodulation: `NRD_MaterialFactors(virtualNormal, -primaryDir, mirrorThroughput * albedo, mirrorThroughput * F0, roughness, diffFactor, specFactor)`; signals divide by their factor; the factors are stored for compose.
- Hit distance: the distance of the continuation ray from the PSR vertex to its hit (or 1e5 on miss), normalized with `REBLUR_FrontEnd_GetNormHitDist(hitDist, viewZ, hitDistParams, roughness)`; the packed signal comes from `REBLUR_FrontEnd_PackRadianceAndNormHitDist(signal, normHitDist, true)`.
- Miss: viewZ = `-2 * denoisingRange`, normal (0,0,1)/roughness 1/material 0, signals and motion 0, emission 0.
- Writes: u7 motion, u8 normal-roughness, u9 viewZ, u10 diff, u11 spec, u12 diffFactor, u13 specFactor, u14 emission, u15 direct. Hit info (u2) unchanged (identity tests keep working in denoised mode).
- The non-guided variant compiles the identical integrator; verify with a byte-compare of `path_trace.cso` before and after the change on the same source (the `#if` sections must not alter the default path) — actually verify by running the M2 tests and a checksum of a raw capture before/after.
- CPU test: the PSR virtual position and reflected normal/motion for one mirror (closed form).
- Commit: `feat(render): guided path tracer variant with PSR guide buffers and split signals`.

### Task 4: Denoised mode in the renderer, compose pass, history handling

**Files:** `shaders/post/compose.hlsl`, `src/render/renderer.h/.cpp`, `src/render/view_mode.h`, `src/render/capture.*`, `src/app/options.*`, `src/app/application.cpp` (mode wiring, overlays, `--blackout-at-frame`).

- `RenderMode::Denoised`: per frame: barriers, scene update, guided trace (1 sample), NRD (`NrdDenoiser::Record`), compose. Timers `path_trace`, `denoise`, `compose`.
- Compose: `linear = emission + diffFactor * Unpack(diffOut).rgb + specFactor * Unpack(specOut).rgb`; `display = sRGB(saturate(linear * exposure))`; raw recomposed sample `rawSample = emission + diffFactor * diffIn + specFactor * specIn` (unpack the YCoCg input the same way) accumulated into u4/u5 (reset on history reset); overlays by `viewMode` (motion: 2D projected motion coloured, magnitude scaled; viewz; history: diff/spec history length over maxAccumulated; diffuse/specular denoised; raw; direct; indirect = diffuse-direct; normals; validation overlay alpha-blended from OUT_VALIDATION). Overlays only touch `display`.
- Renderer: previous camera matrices per frame; Halton(2,3) jitter per frame index; `ResetHistory()`; `RenderSnapshot::cameraCut` flag resets; `DenoiserSettings` (history frames, antifirefly, validation overlay, resetOnSourceChange); denoised-mode readback: `linear` = denoised, `rawMean` + `rawStandardError` from the accumulation, `motion` (float3 per pixel), `historyLength` (diff, spec) per pixel; `Resize` recreates the NRD instance.
- Application: `--mode denoised`; `--blackout-at-frame N` switches every emitter off through `Scene::SetEmitterOn` and calls `ResetHistory()`; validation in denoised mode: layout probe, invalid-value counters (the guided trace keeps the counters), hit expectations (identities), radiance expectations evaluated on **rawMean** (AnalyticPatch/PositivePatch/etc.: the split is lossless, so the reference tolerances apply), and additionally on the denoised `linear` with a documented denoiser tolerance (`denoisedRelativeTolerance = 0.05` for AnalyticPatch); replay checks `denoised_patch_positive/dark` evaluate `linear`.
- Commit: `feat(render): denoised mode (NRD REBLUR), compose pass, history reset events`.

### Task 5: T12 checks: motion vectors, PSR motion, blackout reset, trail lag; frame sequences

**Files:** `src/game/replay.*`, `src/app/application.cpp` (per-frame readback path when a replay has `trail_lag` checks or a capture sequence is requested), `src/render/capture.*` (cropped PNG writer), `src/scene/two_room_level.*` (mirror plane normal exposed), `tests/replay/t12_*.json`, `tests/cpu/test_reconstruction.cpp`, `tests/gpu/CMakeLists.txt`.

- Replay checks (evaluated at the final tick of a non-frozen run, `--frames N` = tick N):
  - `motion`: pixel of `point` must report `entity`; its world motion must equal `entity`'s previous minus current position (from the world state) within `tolerance` (1e-4 m); with `"reflected": true` the expected motion is reflected across the level mirror's normal (PSR check).
  - `static_motion`: pixel of `point` reports `entity` (static) and motion == 0 exactly.
  - `trail_lag`: per-frame patch statistics (mean denoised luminance, count of pixels reporting `entity`) recorded from frame `from` to the final frame; evaluation as defined above with `maxLagFrames` and `settleFraction`.
- `--capture-sequence <dir> --capture-from A --capture-to B --capture-every K --capture-crop x,y,w,h`: per-frame readback, cropped display PNG per frame plus `sequence.json` (frame, tick, world events: door state, lamp state, threat position, history reset flags) — the event log the spec asks for.
- GPU tests (all denoised mode, Debug and Release):
  - `gpu_denoised_split_lossless`: `t09_rect_light`, 64 frames; rawMean AnalyticPatch within the M2 tolerance; denoised patch within 5 %.
  - `gpu_denoised_box_static`: `t08_box`, 120 frames; rawMean PositivePatch; invalid counters 0; debug layer 0 errors.
  - `gpu_denoised_motion_lamp`: `t06_lamp_shelf.json` `--frames 400`; `motion` on the held lamp housing (expected from the world), `static_motion` on a wall.
  - `gpu_denoised_motion_mirror_threat`: `t05_mirror_threat.json` `--frames 741`; `motion` with `reflected` on the mirror pixel (threat), `hit` threat_body, `not_visible`.
  - `gpu_denoised_blackout_reset`: `t08_box`, `--blackout-at-frame 60 --frames 62`; `linear` max luminance <= 1e-4 (no prior illumination after two frames).
  - `gpu_denoised_trail_lag`: `t05_mirror_threat.json` `--frames 990` with a `trail_lag` check on the mirror patch (`maxLagFrames` 6, `settleFraction` 0.8).
  - `gpu_denoised_resize`: `--resize-test --mode denoised` (NRD recreated on resize, 0 debug-layer errors).
- Captures for the record: cropped sequences around the mirror (t05 frames 700–760), the door edge (t06 frames 290–330), the moving lamp (t06 lamp frames 380–420); event logs beside them.
- Commit: `test(gpu): T12 motion, PSR motion, reset, and trail-lag tests; frame-sequence captures`.

### Task 6: Scaled presentation and the benchmark command

**Files:** `shaders/post/upscale.hlsl`, `src/render/renderer.*` (`--internal` size, output display texture), `src/app/benchmark.h/.cpp`, `src/app/options.*`, `src/app/application.cpp`, `tests/cpu/test_reconstruction.cpp` (statistics), `tests/gpu/CMakeLists.txt`.

- `--internal WxH`: trace and denoise at the internal size; a bilinear compute upscale writes the output-size display texture that is presented (the "simple diagnostic scale path", §13); captures record both sizes; `--resize-test` with `--internal 1280x720` proves the present copy is never skipped.
- `--benchmark-seconds S --warmup-seconds W --report file.json` (replay required, windowed or headless, vsync off recommended): loops the replay (world reset at its end, history reset logged as an event) for W + S seconds; records CPU and GPU frame times (frame_gpu timer) after warm-up; report fields: build commit/config/dirty, scene content hash (meshes, materials, instances), shader hashes (.cso bytes), NRD version, device details (name, vendor, device id, driver), resolutions (internal, output), quality settings (mode, hits, history frames, antifirefly), timing distributions (avg, median, p95, p99, max, frames > 33.3 ms, > 50 ms) for CPU and GPU, memory (video budget/usage from DXGI, process working set), replay identifier and loops, warm-up and measured durations, vsync and frame-cap state. Exit 1 when the run aborts.
- GPU test `gpu_benchmark_smoke`: `--benchmark-seconds 3 --warmup-seconds 1` headless on `t06_lamp_shelf.json`, denoised mode, report written and parsed back by the test (fields present).
- The M4 performance record (Release, windowed, vsync off, 1280x720 internal / 1920x1080 output, three 60-second runs of each replay: t05, t06 door, t06 lamp) goes into STATUS.md with the caveat that the §17 180-second full-encounter replay arrives with M5/M6.
- Commit: `feat(app): scaled presentation path and the benchmark report`.

### Task 7: Records

- `docs/RENDERING.md`: the buffer contract table (final values), the PSR section, history events, jitter, composition order (trace -> denoise -> compose -> exposure/sRGB -> upscale -> UI later), diagnostic views.
- `docs/TESTS.md`: T12 tests and thresholds; the trail-lag definition; sequence captures and how to regenerate.
- `docs/DEPENDENCIES.md`: NRD, ShaderMake, MathLib rows; license notes; the credit-screen attribution requirement recorded for M7.
- `docs/BUILD.md`: submodule init, new options, benchmark command.
- `docs/DECISIONS.md`: D-028 submodules and static NRD; D-029 REBLUR; D-030 PSR for the planar mirror (static plane assumption); D-031 global jitter in denoised mode; D-032 world-space motion; D-033 material factors stored, not recomputed; D-034 history bounds; D-035 trail-lag metric.
- `docs/KNOWN_ISSUES.md`: DLSS not evaluated (Streamline deferred), fireflies through the mirror, moving-mirror PSR limitation, no history confidence input.
- `docs/STATUS.md`: M4 evidence with labels; next task.
- Full Debug + Release suites; commit.
