# Rendering

State at milestone M1: hardware camera rays and diagnostic views only. There is no lighting yet,
so the sections the spec requires for the integrator (materials, source units, sampling
equations, MIS weights, buffer contracts for denoising, history policy) are marked **not
implemented** below and will be written with M2.

## Coordinate and matrix contract

- Units: metres, seconds, radians.
- World: right-handed, +Y up. Camera space: right-handed, the camera looks along local -Z with +X
  right and +Y up.
- Column vectors: `world = objectToWorld * local`. A product `A * B` applies `B` first.
- Storage: row-major, `m[row][col]`, on the CPU (`lc::math::Mat4`) and in shader memory
  (`#pragma pack_matrix(row_major)` at the top of `shaders/shared/layouts.hlsli`). HLSL indexing
  `M[row][col]` and `mul(M, v)` therefore mean the same as the CPU code. The GPU layout probe
  (`--validate`) confirms element order, offsets, and array packing on the real device.
- Translation lives in column 3. D3D12 instance transforms (`float Transform[3][4]`) are the first
  three rows of `objectToWorld`, copied element for element.
- Triangles are counter-clockwise when seen from outside; the geometric normal is
  `normalize(cross(p1 - p0, p2 - p0))` computed from world-space vertices.
- Facing: DXR reports a triangle as front-facing when `dot(cross(p1 - p0, p2 - p0), rayDirection) < 0`.
  That matches the counter-clockwise rule directly, so instances use
  `D3D12_RAYTRACING_INSTANCE_FLAG_NONE`. The `facing` view and the validation mode check that the
  RayQuery flag and the geometric normal agree on every hit pixel (0 mismatches required).

## Camera

- Settings store the horizontal field of view (default 90 degrees). The vertical field of view is
  `2 * atan(tan(hfov / 2) / aspect)` with `aspect = width / height`.
- Camera-to-world: `Translation(position) * RotationY(yaw) * RotationX(pitch)`; positive yaw turns
  toward -X, positive pitch looks up.
- Camera ray through pixel `(x, y)` with the pixel centre at `+0.5`:
  `uv = (pixel + 0.5) / renderSize`, `ndc = (2u - 1, 1 - 2v)`,
  `dirView = (ndc.x * tanHalfFovY * aspect, ndc.y * tanHalfFovY, -1)`,
  `dirWorld = normalize(viewToWorld3x3 * dirView)`. The same formula exists on the CPU as
  `Camera::RayDirection` and is used by the tests that check hit expectations.
- Projection (`viewToClip`, present in the constants for later passes, unused by M1 shaders):
  right-handed, D3D clip depth in [0, 1], near plane maps to 0, far plane to 1
  (`m22 = far / (near - far)`, `m23 = near * far / (near - far)`, `m32 = -1`). Near 0.05 m, far 200 m.
- Camera rays use `tMin = 0` and `tMax = far`.

## Ray interface

`shaders/trace/ray_interface.hlsli` provides the only two queries:

- `TraceClosest(scene, origin, direction, tMin, tMax)`: inline `RayQuery<RAY_FLAG_FORCE_OPAQUE>`,
  full traversal loop, returns the committed nearest triangle with `t`, instance index, instance
  id, primitive index, barycentrics, facing flag, and object-to-world matrix. It never uses the
  first-hit shortcut.
- `TraceVisibility(scene, origin, direction, tMin, tMax)`: adds
  `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`; returns true when the finite segment is unoccluded.
  (Written, not yet exercised by a pass: source visibility arrives with M2.)

All geometry is opaque triangles in one TLAS; every ray type will use this same structure.

## Hit identifiers

- `InstanceID` in the TLAS instance descriptor is the scene's stable instance id (starts at 1,
  never reused, 24-bit). `InstanceRecord[instanceIndex].stableId` carries the same value so the
  two can be cross-checked.
- `camera_view.hlsl` writes a hit-info texel per pixel: `x` = stable id or `0xFFFFFFFF` on a miss,
  `y` = primitive index, `z` = flags (bit 0 RayQuery front face, bit 1 geometric normal faces the
  ray), `w` = instance index.

## Diagnostic views (M1)

These are diagnostic colours, identified as such in the window title, the log, and capture
metadata (`"diagnosticView": true`). They are not lighting and never enter a production image.

| `--view` | Meaning | Miss colour |
|---|---|---|
| `normals` | world-space geometric normal * 0.5 + 0.5 | black |
| `ids` | hash colour of the stable instance id | black |
| `depth` | hit distance, 0 m black to 20 m white | black |
| `bary` | barycentric weights (w0, w1, w2) as RGB | black |
| `facing` | green = front face, red = back face, magenta = RayQuery facing and winding disagree | black |
| `prims` | hash colour of primitive index and instance id | black |

## Shared GPU records

`FrameConstants` (256 bytes, root CBV b0), `InstanceRecord` (112 bytes, `StructuredBuffer` t1,
one per TLAS instance in TLAS order), `MeshRecord` (16 bytes, t2), positions (`StructuredBuffer<float3>`
t3, all meshes concatenated), indices (`StructuredBuffer<uint>` t4). Matrices in structured
buffers are stored as three explicit `float4` rows to avoid any packing ambiguity. Root parameter
6 is a UAV table: u0 display (R8G8B8A8_UNORM), u1 linear (R32G32B32A32_FLOAT), u2 hit info
(R32G32B32A32_UINT), u3 layout-probe output.

## Captures

`--capture <dir>` writes, from the running program after the last frame:

- `<scene>_<view>_f<frame>.png`: the 8-bit display image (lossless, stored-deflate PNG).
- `<scene>_<view>_f<frame>.pfm`: the linear float RGB image (the shader's colour before the
  8-bit store; for diagnostic views this is the same colour, for M2 it becomes radiance).
- `<scene>_<view>_f<frame>.json`: scene, view, frame index, sample index, seed, path depth,
  exposure, render size, output size, reconstruction state, adapter, driver, build commit and
  config, timestamp, instance and triangle counts, GPU timings per pass.

## Not implemented yet (M2 and later)

Material model and parameter ranges; emitter radiance convention and scale; source selection,
emitter-surface sampling and area-to-solid-angle conversion; surface sampling and MIS weights;
delta (mirror) handling; path limits and termination; sampling sequence and seed dimensions;
invalid-value detection; reference and raw modes; denoiser buffer contract; history policy;
exposure and display transform.
