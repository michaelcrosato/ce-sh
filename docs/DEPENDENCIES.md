# Dependencies

Policy (spec §3): fixed versions, recorded licenses, one acquisition method, no silent fetches.
Third-party source enters the repository only as git submodules pinned to a commit; nothing is
downloaded at configure or build time (the builds were verified offline-equivalent: every fetch
option of the submodules is switched off in `cmake/LcNrd.cmake`).

## Third-party source (git submodules under `external/`)

| Component | Pinned version | Source / acquisition | License | Purpose | Redistribution |
|---|---|---|---|---|---|
| NVIDIA Real-Time Denoisers (NRD) | v4.17.3 (commit `792eff196afdd350fd9c3f862119017ccb438a0e`, 30 April 2026) | `git submodule` of https://github.com/NVIDIA-RTX/NRD | **NVIDIA RTX SDKs License** (`external/NRD/LICENSE.txt`, v. April 12, 2021) with the RTX supplement | REBLUR diffuse+specular denoiser for the denoised mode (spec §13). Built as a static library with DXIL shaders only; `Include/NRD.h`, `NRDDescs.h`, `NRDSettings.h`, `Shaders/NRD.hlsli` are the interfaces used | Object-code distribution inside an application with "material additional functionality" is permitted; the SDK may not be redistributed stand-alone; **attribution required**: "This software contains source code provided by NVIDIA Corporation." plus the NVIDIA marks on the credit screen (or prominently in the end-user documentation when there is no credit screen) — an M7 packaging item; the application must stay interoperable with NVIDIA GPUs (it is RTX-only); the SDK must not be placed under an open-source license (§4(e)), which constrains the project's own license choice |
| ShaderMake | commit `18f5a344e7ca8fa65daaf079d07bc8ce38453e05` (the commit NRD 4.17.3 pins) | `git submodule` of https://github.com/NVIDIA-RTX/ShaderMake | MIT (`external/ShaderMake/LICENSE.txt`); bundles argparse (MIT, `ThirdPartyLicenses.txt`) | Build-time tool that compiles NRD's shaders with our DXC, and the `ShaderMakeBlob` reader NRD links | Build tool only; nothing shipped |
| MathLib | tag v11 | `git submodule` of https://github.com/NVIDIA-RTX/MathLib | MIT (`external/MathLib/LICENSE.txt`) | Header-only math used by NRD's sources and shaders | Compiled into NRD; MIT notice to be included with the package |
| Dear ImGui | v1.92.9b (commit `f1cc2ae15e53a861a874c3034aae6798fde194ab`) | `git submodule` of https://github.com/ocornut/imgui | MIT (`external/imgui/LICENSE.txt`) | On-screen prompts, pause menu, settings, and the diagnostic panel (spec §3 "Dear ImGui, or a similarly small approved library", §16); the Win32 and D3D12 backends from `backends/` render into the swap-chain image after the present copy | Compiled into the executable; MIT notice to be included with the package |
| miniaudio | 0.11.25 (commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`) | `git submodule` of https://github.com/mackron/miniaudio | Public domain (Unlicense) or MIT No Attribution, at the user's choice (`external/miniaudio/LICENSE`) | Audio device output (spec §3 "One small library, such as miniaudio"): compiled from its own `miniaudio.c` as `lc_miniaudio` with the WASAPI backend only and no decoding, encoding, generators, or resource manager; the mixing rules are ours (`src/audio/director.*`) and every clip is generated in code (`src/audio/clips.cpp`, provenance strings in the clips) | Compiled into the executable; no notice required (MIT-0 chosen) |

Owner review: the NRD license is a proprietary NVIDIA SDK license, not an open-source license.
Its terms were read in full during M4 (`external/NRD/LICENSE.txt`); the points that affect the
project are the attribution requirement, the stand-alone redistribution ban, the interoperability
clause, and the open-source relicensing clause. The spec (§3, §13, M4) directs the NRD
integration; accepting the license for distribution is the owner's decision (spec §22).

Setup: `git submodule update --init --recursive` after cloning. The configure step fails with an
actionable message when a submodule directory is empty.

## Toolchain and operating-system components (not third-party source)

| Component | Version actually used | Source / acquisition | License | Purpose | Redistribution |
|---|---|---|---|---|---|
| Windows SDK | 10.0.26100.0 | Visual Studio Installer | Microsoft Software License Terms (SDK) | `d3d12.h`, `d3d12sdklayers.h`, `dxgi1_6.h`, `dxgidebug.h`, `psapi.h`, Win32 headers and import libraries | Nothing redistributed; the runtime is part of Windows |
| DirectX Shader Compiler (`dxc.exe`, `dxcompiler.dll`, `dxil.dll`) | 1.8.2502.11 (from the SDK above) | Windows SDK `bin\10.0.26100.0\x64` | Included with the SDK (DXC itself is LLVM/NCSA licensed on GitHub) | Build-time HLSL to signed DXIL, for our shaders and (through ShaderMake) NRD's | Not shipped; shaders are precompiled |
| D3D12 debug layer / DRED / DXGI debug | Windows 11 25H2 in-box | "Graphics Tools" optional feature for the debug layer | Windows | Development validation only | Not shipped |
| MSVC toolset | 14.51.36231 (cl 19.51.36256), Visual Studio Community 2026 18.9.12120.119 | Visual Studio Installer | Visual Studio license | Compiler and linker | Release builds need the Visual C++ Redistributable (M7) |
| CMake | 4.3.1-msvc1 (bundled with Visual Studio) | Visual Studio Installer, or `winget install Kitware.CMake` | BSD-3-Clause | Build system generation, CTest | Not shipped |
| Ninja | 1.13.2 (bundled with Visual Studio) | Visual Studio Installer | Apache-2.0 | Available on `PATH` via `tools/env.ps1`; the presets use MSBuild | Not shipped |
| Git | 2.55.0.windows.3 | git-scm.com | GPL-2.0 (tool only) | Submodules; the build stamps the commit into `build_info.h` (builds without it report `unknown`) | Not shipped |

Header-only in-house code replaces what might otherwise be dependencies: PNG and PFM encoders
(`src/core/image_write.*`), a JSON writer and a strict reader (`src/core/json_*`), the math layer
(`src/core/math`), and the test harness (`tests/cpu/lc_test.h`).

## Planned evaluations (not added)

| Candidate | Spec section | When |
|---|---|---|
| NVIDIA Streamline (DLSS Super Resolution) | §13 | After the native denoised path is stable (M4 evaluation deferred; see STATUS.md) |
| Microsoft D3D12 Agility SDK | §3 | Only if a feature beyond the in-box runtime is needed |

Dear ImGui and miniaudio moved from this table into the dependency table above in M5.

Each addition must pin a tag or commit, record its license text, and be added to this file
before it is used.
