# Build

## Prerequisites (tested versions)

| Requirement | Tested with |
|---|---|
| Windows 11 x64 | 10.0.26200.9278 (25H2) |
| Visual Studio with "Desktop development with C++" | Visual Studio Community 2026 18.9.12120.119, MSVC toolset 14.51.36231 (cl 19.51.36256) |
| Windows SDK (ships `d3d12.h`, `dxc.exe`, `dxil.dll`) | 10.0.26100.0, DXC 1.8.2502.11 |
| CMake 4.1 or newer (needed for the "Visual Studio 18 2026" generator) | 4.3.1-msvc1, bundled with Visual Studio |
| GPU with DXR Tier 1.1 and Shader Model 6.5 | NVIDIA GeForce RTX 4070 SUPER, driver 32.0.16.1047 (610.47) |
| Optional: "Graphics Tools" Windows feature | Needed for the D3D12 debug layer (`--debug-layer on`) |

No third-party source packages are downloaded. Network access is not needed to build or run.

## Configure, build, test

CMake and Ninja are bundled with Visual Studio but not on `PATH`. Either install CMake system-wide
(`winget install Kitware.CMake`) or put the bundled copy on the path of the current shell:

```powershell
. .\tools\env.ps1
```

Then use the committed presets (spec §21):

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure

cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
```

`.\tools\build.ps1 [-Preset windows-release] [-NoTest] [-Fresh]` runs the three steps.

Outputs (per preset):

```text
build\<preset>\bin\LastCircuit.exe
build\<preset>\bin\lc_cpu_tests.exe
build\<preset>\bin\shaders\*.cso   signed DXIL, compiled at build time by dxc.exe
build\<preset>\bin\shaders\*.pdb   shader symbols for PIX
```

The presets use the Visual Studio generator so no `vcvars` environment is needed. Each preset has
its own binary directory and builds exactly one configuration; the Debug preset compiles shaders
with `-Od`, the Release preset with `-O3`.

### Options

| CMake variable | Purpose |
|---|---|
| `LC_DXC_EXECUTABLE` | Path to a specific `dxc.exe` (its `dxil.dll` must sit beside it). Default: the Windows SDK copy matching the target platform version. |

## Run

```powershell
.\build\windows-debug\bin\LastCircuit.exe --list-adapters
.\build\windows-debug\bin\LastCircuit.exe --scene rt_boxes                # windowed diagnostic view, Escape closes
.\build\windows-debug\bin\LastCircuit.exe --scene rt_triangle --validate --headless
.\build\windows-debug\bin\LastCircuit.exe --scene rt_boxes --headless --frames 3 --capture .\artifacts\m1 --view normals
.\build\windows-debug\bin\LastCircuit.exe --scene rt_boxes --resize-test
.\build\windows-debug\bin\LastCircuit.exe --list-adapters --env-report .\artifacts\environment.json
.\build\windows-debug\bin\LastCircuit.exe --scene two_room --play --exposure 4        # live play (see below)
.\build\windows-debug\bin\LastCircuit.exe --scene two_room --replay .\tests\replay\t05_mirror_threat.json --stop-at-tick 740 --mode reference --spp 64 --validate --headless
.\build\windows-debug\bin\LastCircuit.exe --help
```

Live play (`--play`, scene `two_room`): W A S D move, mouse look, Shift sprint, E interact (door,
lamp pick-up, placing the lamp on a socket), F lamp switch, Escape releases or recaptures the
cursor (the simulation pauses while it is released or the window lacks focus). Add
`--record <file>` to write the per-tick input as a replay on exit; `--replay <file>` plays it
back one tick per frame, and `--stop-at-tick N` freezes the world at tick N for reference-mode
renders and `--validate` checks.

Runtime requirements: the `shaders` directory must stay beside `LastCircuit.exe`; the D3D12 debug
layer is on by default in Debug builds (`--debug-layer off` disables it, `--gpu-validation` adds
GPU-based validation). Unknown options are errors.

Exit codes: `0` success, `1` failure (validation failed, device removed, unhandled error),
`2` usage error, `3` unsupported hardware (no adapter with DXR Tier 1.1 and Shader Model 6.5).
CTest treats `3` as "skipped", never as passed.

## Packaging

Not implemented yet (milestone M7). Release builds link the dynamic CRT, so a package will need the
Visual C++ redistributable; Debug builds use the debug CRT and are not redistributable.
