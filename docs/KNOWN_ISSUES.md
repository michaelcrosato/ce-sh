# Known issues

Format: id, severity, affected build, description, reproduction, planned verification.

## KI-001 — low — all builds — no scaled presentation path

`--width/--height` set both the trace resolution and the window client size; there is no
1280x720-internal / 1920x1080-output mode yet (spec §17). If the swap chain and render outputs
ever differ in size, the present copy is skipped with a warning until the next resize.
Verification: M4 adds the scaled path and a test that presents a 1280x720 trace into a 1920x1080 window.

## KI-002 — low — all builds — no camera or object motion yet

Scenes are static; input, the fixed-step simulation, and moving instances arrive with M3. The
TLAS rebuild-on-change path is therefore only exercised by the first frame (`tlas_update` reports
0 ms afterwards). Verification: M3 replay test with a moving object and a TLAS rebuild-versus-update measurement.

## KI-003 — low — Debug builds — shaders compiled with `-Od`

Debug-preset GPU timings are not representative. Use the Release preset for any timing evidence.

## KI-004 — low — all builds — live-object report goes to the debugger only

`IDXGIDebug1::ReportLiveObjects` output is visible under a debugger, not in the log. No leak has
been observed (the report is empty of application objects at exit when run under the debugger is
NOT RUN this session). Verification: run under Visual Studio once per milestone and record the result.

## KI-005 — info — all builds — GPU timings arrive two frames late

`LastTimings()` reflects the frame that last used the current frame slot. Short runs
(`--frames 1` or `2`) therefore get their timings only from `CollectFinalTimings()` after the loop.

## KI-006 — info — all builds — only one GPU model tested

All GPU evidence comes from one NVIDIA GeForce RTX 4070 SUPER with driver 32.0.16.1047. Other RTX
models and vendors are untested (spec §3). Verification: run the GPU test label on a second machine.

## KI-008 — low — all builds — no tone mapping

The display transform is `sRGB(saturate(radiance * exposure))`. Radiance above `1 / exposure`
clips (visible on emitter surfaces, which show as white). Correctness tests read the linear
radiance, which is unaffected. Verification: a documented tone curve with M4/M5 and a test that
the raw radiance is unchanged by it.

## KI-009 — info — all builds — BSDF-only estimator covers a shorter path family at equal hits

By construction (RENDERING.md, "Path families"). The comparison test compensates with one extra
hit; the production integrator (MIS) is unaffected.

## KI-010 — info — all builds — float32 accumulation

Sums and sums of squares are float32; at a few thousand samples the relative precision loss is
below 1e-4 for the radiance levels in the test scenes. Verification: switch to float64 or
compensated sums if reference runs beyond ~16k spp are needed.

## KI-011 — low — windowed builds — rendering pauses during a caption drag

The Win32 modal size/move loop blocks the frame loop until the drag ends; the last frame stays on
screen and the swap chain resizes on release. No corruption. Verification: a timer-driven render
inside `WM_ENTERSIZEMOVE` if live resize is wanted.

## KI-012 — info — all builds — raw mode is noisy by design

One path sample per pixel per frame with no temporal filtering (spec §13 raw mode). Real-time
reconstruction is M4.

## KI-007 — info — all builds — reliability loop not run

The 30-minute reliability loop, repeated reload/resize/focus sequences beyond the resize test, and
device-removal recovery are NOT RUN. Device removal currently reports DRED data and exits with code 1.
