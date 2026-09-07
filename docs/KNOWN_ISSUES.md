# Known issues

Format: id, severity, affected build, description, reproduction, planned verification.

## KI-001 — closed in M4 — scaled presentation path

`--internal WxH` traces at the internal size and resamples the display to the window
(`gpu_denoised_scaled_resize_test`). A swap chain that matches neither size still skips the copy
with a warning until the next resize.

## KI-002 — closed in M3 — camera and object motion

Replays move the camera, door, lamp, and threat; TLAS rebuilds are counted against motion frames.

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

## KI-013 — info — all builds — rough conductors lose energy at high roughness

Single-scattering GGX: the directional albedo at roughness 0.7 is about 0.69 at 45 degrees and
0.31 at roughness 1 / normal incidence (T10 records the exact values). This is the documented
behaviour of the model, not a leak; multiple-scattering compensation (Kulla-Conty style) can be
added later and must keep T10's "no energy gain" bound.

## KI-014 — medium — all builds — caustic fireflies from smooth conductors and mirrors

Light that reaches a diffuse surface through a near-specular bounce (panel -> smooth steel ->
wall) can only be found by BSDF sampling, so it appears as rare bright samples (visible as white
speckles in `metals_room` at 512 spp). This is the known limitation of unidirectional path tracing
with small bright sources; the estimator is unbiased. The spec allows evaluating a production
outlier clamp later (off in reference mode, bias recorded); the denoiser (M4) is the other half of
the answer. Verification: an M4 test comparing patch means with and without the clamp.

## KI-015 — closed in M5 — collision and sweeps

The capsule controller (`src/game/collision.*`, D-040) keeps the player and the carried lamp out
of the kit boxes and the door leaf; a closing leaf that meets the player swings back open. CPU
tests cover the push-out, sliding, the sweep, and the door; the replays now run against the same
solids (the mirror route grazed the sink block, which moved 20 cm to keep the path clear).

## KI-016 — low — all builds — dark rooms need exposure

With fixed exposure 1 the hall and the inspection room are nearly black on screen (their light
levels are a design choice; the radiance checks are unaffected). `--exposure` scales the display
only and the pause menu has an exposure slider (M5); a documented tone curve arrives with M7.

## KI-017 — closed in M5 — on-screen prompts, pause menu, diagnostic panel

Dear ImGui (D-038) draws the interaction prompt, the objective line, the controls card, the pause
menu with the settings, and the F1 diagnostic panel (GPU passes, CPU time, history resets, world
state, scene file and hash, reload result). The reload command (R, or the menu) reports its result
in the panel and the log.

## KI-018 — medium — denoised mode — spatial filtering biases sharp lighting gradients

The denoised image is 3.6–4.0 % dark on the floor under a small emitter (`t09_rect_light`), inside
the 5 % tolerance but not zero; enclosed scenes are within 1.5 %. The raw mean is exact. Reduce
`--blur-radius` for accuracy at the cost of residual noise. Verification: the T12 review with a
person and a per-scene tolerance record when the six-room content exists.

## KI-019 — low — denoised mode — no history confidence input

NRD's anti-lag runs on its defaults; the optional history-confidence inputs (previous-frame
re-trace) are not provided. Lamp switches therefore fade over the history length unless
`--reset-on-source-change` is used. Verification: measure the fade with a `trail_lag`-style check
on a source switch in M5.

## KI-020 — info — denoised mode — moving mirrors not covered by the PSR motion

The virtual motion assumes the mirror plane did not move (D-030). No mirror moves in the proof.

## KI-021 — info — denoised mode — DLSS Super Resolution not evaluated

Streamline was not added in M4 (the native path came first; the SDK is a separate proprietary
dependency). The bilinear `--internal` path is the only scaled presentation.

## KI-022 — low — denoised mode — first-frame hitch

The denoiser (NRD instance, 14 pipelines, pool textures) is created on the first denoised frame,
which takes about 0.8 s in Debug (`simulation clamped a long frame (0.800 s)` in the play log) and
less in Release. The fixed-step simulation caps the catch-up, so play starts cleanly; the cost
belongs in the loading phase before the first frame (M7 packaging).

## KI-023 — info — play mode — the body is a placeholder

Spec §14 allows a simple rigid construction and asks that it be recorded: the body is a torso box
and two hand boxes in `player_suit`, placed behind the eye axis (D-039). The hands do not grip the
carried lamp and there are no arms or legs; the mirror therefore shows a floating torso with two
hands and the lamp beside them. A proper body is M6/M7 content.

## KI-012 — info — all builds — raw mode is noisy by design

One path sample per pixel per frame with no temporal filtering (spec §13 raw mode). Real-time
reconstruction is M4.

## KI-007 — info — all builds — reliability loop not run

The 30-minute reliability loop, repeated reload/resize/focus sequences beyond the resize test, and
device-removal recovery are NOT RUN. Device removal currently reports DRED data and exits with code 1.
