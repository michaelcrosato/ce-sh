# Architecture

State at milestone M1. Everything below exists in the code; nothing is a plan.

## Modules and ownership

| Library | Directory | Owns | Depends on |
|---|---|---|---|
| `lc_core` | `src/core` | Logging, errors, clock, strong ids, strict CLI parser, math (`Vec3`, `Mat4`, camera math), PNG/PFM encoders, JSON writer, generated `build_info.h` | nothing platform-specific |
| `lc_scene` | `src/scene` | `MeshData` + validation, generated primitives, `Scene` registry (meshes, instances, stable ids, transform history), `Camera`, built-in test scenes with hit expectations | `lc_core` |
| `lc_render_contracts` | `src/render` (headers) | CPU mirrors of the HLSL records (`gpu_layouts.h`), `ViewMode` | nothing |
| `lc_app_options` | `src/app/options.*` | `AppOptions` from the command line | `lc_core`, contracts |
| `lc_platform` | `src/platform` | Win32 window and events, file helpers, HRESULT reporting | `lc_core` |
| `lc_graphics` | `src/graphics/d3d12` | Device/adapter/feature checks, queue + fence, swap chain, descriptor heap, buffers, UAV textures + readback, upload arena, timestamp queries, BLAS/TLAS, root signature + compute PSO | `lc_platform` |
| `lc_render` | `src/render` | `SceneGpu` (geometry residency, BLAS per mesh, TLAS), `Renderer` (frame recording, diagnostics pass, readback, layout probe), capture writer | `lc_graphics`, `lc_scene` |
| `LastCircuit.exe` | `src/app` | Modes (list adapters, windowed, headless, validate, resize test, capture), environment report, main loop | everything above |
| `lc_cpu_tests.exe` | `tests/cpu` | Portable unit tests | `lc_core`, `lc_scene`, options, contracts |

The game/scene layer (`lc_scene`) contains no D3D12 types. The renderer reads a `RenderSnapshot`
(scene pointer, camera, frame index, view mode) and never mutates the scene.

## Data flow per frame (spec §9 order, M1 subset)

```text
Window::PumpMessages            poll input and window events (close, resize, focus, minimize)
(no simulation yet)             M3 adds the fixed-step simulation
RenderSnapshot                  scene + camera + frame index + view mode
Renderer::BeginFrame            wait for this slot's fence (2 frames in flight), reset allocator and upload arena,
                                collect the GPU timings of the frame that last used the slot
Renderer::RecordTrace           SceneGpu::UpdateInstances: InstanceRecords into the upload arena every frame;
                                TLAS rebuild (with UAV barriers) only when a transform revision changed
                                FrameConstants into the upload arena; bind root signature; dispatch camera_view.hlsl
Renderer::RecordCopyToBackBuffer display texture UAV->COPY_SOURCE, back buffer PRESENT->COPY_DEST, CopyResource, back
Renderer::EndFrame              resolve timestamps, close, ExecuteCommandLists, fence signal
SwapChain::Present              vsync on/off; DXGI_ERROR_DEVICE_REMOVED triggers the DRED report and exit 1
Scene::CommitRenderedFrame      previous transforms := current (motion history refers to rendered images)
```

## Lifetimes and synchronization

- One direct command queue, one fence. Frames in flight: 2. Each frame slot owns a command
  allocator, a 4 MiB upload arena (constants, instance records, instance descriptors), and a fence
  value. A slot is reused only after `WaitForFenceValue` on its previous frame.
- One TLAS result buffer and one scratch buffer. The rebuild is recorded in the same command list
  as the dispatch that consumes it; the single in-order queue plus UAV barriers before and after
  the build order it against the previous frame's traversal.
- Full GPU waits are used only for setup (scene upload and BLAS builds), swap-chain and output
  resize, readback/capture, and the layout probe. They are logged as such and never occur in the
  per-frame path.
- Default-heap buffers are created in `COMMON` and rely on the documented promotion/decay rules
  (buffers decay to `COMMON` after every `ExecuteCommandLists`). Textures track their own state in
  `GpuTexture2D::Transition`. Acceleration structures live in the
  `RAYTRACING_ACCELERATION_STRUCTURE` state for their whole life.
- Staging buffers for uploads are kept alive until the setup command list has executed and the
  queue has been drained.
- COM objects use `Microsoft::WRL::ComPtr`. Destruction order in `Application::RunRender`:
  Renderer and SwapChain (both wait for the GPU) before GraphicsQueue, then Device. The
  info-queue callback is unregistered in `Device::~Device`.

## Coordinates and data contracts

See `docs/RENDERING.md`. In short: metres, right-handed, +Y up, camera looks along local -Z,
column vectors, row-major storage on both CPU and GPU, one shared layout header
(`shaders/shared/layouts.hlsli` mirrored by `src/render/gpu_layouts.h`, checked by static_asserts
and by the GPU layout probe).

## Error policy

Initialization failures throw `lc::Error` (or `lc::UnsupportedHardware`, `lc::UsageError`) with
the failing call, HRESULT text, and file:line. The application maps them to exit codes 1, 3, and
2. Per-frame paths log and return; `Present` failure triggers `Device::ReportDeviceRemoved`
(removal reason plus DRED breadcrumbs and page-fault data). Debug-layer messages arrive through
`ID3D12InfoQueue1::RegisterMessageCallback`, are logged, and are counted; `--validate` fails on
any error-severity message.

## Not yet present

Materials, emitters, the path integrator, denoising, input, simulation, audio, scene files, and
the game layer. `docs/STATUS.md` names the next task.
