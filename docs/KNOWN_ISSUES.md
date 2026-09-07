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

## KI-007 — info — all builds — reliability loop not run

The 30-minute reliability loop, repeated reload/resize/focus sequences beyond the resize test, and
device-removal recovery are NOT RUN. Device removal currently reports DRED data and exits with code 1.
