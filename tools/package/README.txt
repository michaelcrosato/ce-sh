LAST CIRCUIT - external test package
====================================

Build:      {BUILD_ID}
Packaged:   {DATE}
Contents:   LastCircuit.exe, shaders\, assets\, replays\, NOTICES.txt, KNOWN_ISSUES.md,
            smoke_test.cmd, benchmark.cmd, manifest.txt

Last Circuit is a small first-person escape demo whose camera and lighting are hardware path
tracing (Direct3D 12, DirectX Raytracing inline ray queries). There is no raster fallback: the
program needs a graphics adapter with DirectX Raytracing Tier 1.1 and Shader Model 6.5 (an NVIDIA
GeForce RTX card or an equivalent) with a current driver, on Windows 10 or 11 (64-bit). Nothing
else is installed or downloaded; the package runs offline and never contacts a network.

Tested on: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.16.1047, Windows 11 25H2. Other adapters
are untested (KNOWN_ISSUES.md, KI-006).


PLAY
----
Double-click LastCircuit.exe. The demo starts in play with the interface on; a controls card
explains the keys. If the program cannot start, a message box says why and names the log file.

Controls (rebindable in the pause menu, Escape):
    W A S D         move            Mouse           look
    Shift           sprint          E               interact: doors, the lamp, the fuse, their sockets
    F               lamp on / off   Escape          pause menu: settings, controls, restart, quit
    F1              diagnostic panel (frame times, the world state)

The objective line at the top left says what to do next. The lamp is your light in the dark
parts; the maintenance machine walks the halls and catches what it sees. A catch restarts you at
the last checkpoint. The menu's brightness slider is a display setting only: it never changes what
the machine can see.

Settings (look, brightness, volumes, text cues, interface scale, key bindings) are saved in
    %LOCALAPPDATA%\LastCircuit\settings.json
and the play logs in
    %LOCALAPPDATA%\LastCircuit\logs\
Delete the settings file to return to the defaults.


SMOKE TEST
----------
Double-click smoke_test.cmd (or run "smoke_test.cmd /quiet" from a terminal). It lists the
adapters, writes an environment report, validates the hardware ray-tracing test scene, runs 600
reconstructed frames of the demo's fixed encounter replay with its checks, and writes a short
benchmark report. Results go to smoke\; the last line says SMOKE TEST PASSED or FAILED.


BENCHMARK
---------
Double-click benchmark.cmd: a 5-second warm-up, then 180 measured seconds of the fixed encounter
replay in a 1920x1080 window rendered at 1280x720, reconstructed, vsync off. The window closes by
itself. The report (benchmark\benchmark_<time>.json) records the build and content hashes, the
adapter and driver, sizes and settings, the CPU and GPU frame-time distributions (average, median,
95th and 99th percentiles, maximum, counts above 33.3 and 50 ms), memory, and the durations. The
pass criterion is a 95th-percentile GPU frame time of 16.67 ms or less and a 99th percentile of
22 ms or less.


FROM A TERMINAL
---------------
    LastCircuit.exe --help          every option
    LastCircuit.exe --version       the build identification
    LastCircuit.exe --scene mirror_lab --play --mode denoised     the two-room proof level
    LastCircuit.exe --list-adapters --env-report environment.json


KNOWN ISSUES AND NOTICES
------------------------
KNOWN_ISSUES.md lists what is known not to work or not to have been tested. NOTICES.txt carries
the third-party license notices: this software contains source code provided by NVIDIA
Corporation (NVIDIA Real-Time Denoisers), Dear ImGui, NVIDIA MathLib, and miniaudio.
