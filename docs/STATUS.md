# Status

Evidence labels follow spec §22: WRITTEN, COMPILED, CPU TESTED, GPU EXECUTED, IMAGE CHECKED,
PERFORMANCE CHECKED, PASSED, FAILED, NOT RUN.

```text
Milestone:            M0 PASSED, M1 PASSED (on the machine below). M2 not started.
Build or commit:      branch m0-m1-bootstrap; code at a64db7b (docs committed after it).
                      Presets windows-debug and windows-release both configured, built, and tested.
Environment:          Windows 11 Home 10.0.26200.9278 (25H2); Intel Core i7-14700F, 31.8 GiB RAM;
                      Visual Studio Community 2026 18.9.12120.119, MSVC 14.51.36231 (cl 19.51.36256);
                      CMake 4.3.1-msvc1 (VS-bundled); Windows SDK 10.0.26100.0; DXC 1.8.2502.11 + dxil.dll.
Implemented in this session:
                      CMake presets and build-time DXC pipeline; lc_core (log, errors, clock, ids, strict CLI,
                      math with the column-vector/right-handed contract, PNG/PFM/JSON writers); lc_scene (mesh
                      validation, generated primitives, scene registry with stable ids and transform history,
                      camera, rt_triangle and rt_boxes scenes with hit expectations); Win32 window; D3D12
                      device with adapter probing (feature level 12_1, DXR tier, shader model, typed UAV
                      stores, tearing, video memory budget), debug layer, GPU-based validation option, DRED,
                      info-queue callback; queue/fence, flip swap chain with resize, descriptor heap, buffers,
                      UAV textures with readback, upload arena, timestamp queries, BLAS/TLAS; renderer with
                      camera_view.hlsl (inline RayQuery, 6 diagnostic views), GPU layout probe, hit-id and
                      facing validation, PNG/PFM/JSON capture, resize test, environment report; eight docs.
Checks actually run:  COMPILED: Debug and Release, /W4 /WX clean, two shaders (cs_6_5, HLSL 2021, -WX).
                      CPU TESTED: lc_cpu_tests 39/39 cases PASSED (both presets).
                      GPU EXECUTED + PASSED (both presets): gpu_list_adapters, gpu_validate_rt_triangle,
                      gpu_validate_rt_boxes, gpu_validate_rt_boxes_facing, gpu_resize_test. ctest: 6/6.
                      Validation details: layout probe 30/30 fields bit-identical (T02 layouts); all hit
                      expectations correct (ids 1/2/3, misses); RayQuery facing vs geometric winding
                      mismatches 0 of 311,329 hit pixels (rt_boxes) and 0 of 59,642 (rt_triangle);
                      D3D12 debug layer 0 errors, 0 warnings; also PASSED once with --gpu-validation.
                      Usage error returns 2; --adapter 1 (software adapter) returns 3 with a clear message.
GPU and driver used:  NVIDIA GeForce RTX 4070 SUPER, vendor 0x10DE device 0x2783 rev 161, 11997 MiB,
                      driver 32.0.16.1047 (NVIDIA 610.47, 2026-05-18); reports DXR Tier 1.2, SM 6.8,
                      root signature 1.1, resource binding tier 3, tearing supported.
Image evidence:       IMAGE CHECKED: artifacts/m1/rt_boxes_{normals,facing,ids}_f2.png and
                      rt_triangle_normals_f2.png (1280x720, regenerate with the commands in docs/BUILD.md;
                      artifacts/ is not committed). Normals view shows +Y floor green, +Z faces blue, the
                      small box's +X side red, the tall box's -X side teal, tops only where the camera is
                      above them; facing view uniformly green; ids view one flat colour per instance;
                      the triangle's lower-right vertex is below the lower-left (not mirrored). PNG and
                      PFM open in ImageMagick 7.1.2 (identify reports 1280x720, 8-bit and 32-bit float).
Performance evidence: PERFORMANCE CHECKED for instrumentation only, not a benchmark: Release, headless,
                      rt_boxes (26 triangles, 3 instances), 1280x720, 300 frames: CPU 0.34 ms average per
                      frame; GPU trace 0.06-0.67 ms per frame (varies with GPU clock state). The scene has
                      no lighting, so this says nothing about the §17 targets. T16 NOT RUN.
Failed checks:        None outstanding. During the session the first GPU run FAILED validation: the
                      TRIANGLE_FRONT_COUNTERCLOCKWISE instance flag inverted facing on every hit pixel;
                      removed (see docs/DECISIONS.md D-004) and re-verified.
Checks not run and reasons:
                      T03-T18: need lighting, materials, sources, motion, denoising, gameplay (M2+).
                      Second RTX device compatibility: only one GPU in this machine.
                      30-minute reliability loop and device-removal recovery: not part of M1.
                      Live-object report under a debugger: NOT RUN (no debugger session).
                      Non-Windows CPU test build: NOT RUN (no other platform available).
Changed assumptions:  DXR's default facing rule already matches right-handed counter-clockwise geometry;
                      no winding flag is used. The in-box Windows SDK suffices (no Agility SDK).
                      Internal resolution equals window size until the scaled path (M4).
Next concrete task:   M2 step 1 — add material and emitter records (Diffuse, Mirror, Emitter) to the
                      scene and GPU contracts, build the sealed test room scene (T03/T04 geometry), and
                      write the first path-integrator compute pass with next-event estimation to one
                      rectangular area emitter, accumulating into an HDR buffer with raw and reference
                      modes. First gate: T03 (no sources -> raw radiance |value| <= 1e-6) on the GPU.
```
