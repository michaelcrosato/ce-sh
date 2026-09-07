# Dependencies

Policy (spec §3): fixed versions, recorded licenses, one acquisition method, no silent fetches.
As of M1 there is **no third-party source code** in the repository. Everything below is an
operating-system or toolchain component.

| Component | Version actually used | Source / acquisition | License | Purpose | Redistribution |
|---|---|---|---|---|---|
| Windows SDK | 10.0.26100.0 | Visual Studio Installer | Microsoft Software License Terms (SDK) | `d3d12.h`, `d3d12sdklayers.h`, `dxgi1_6.h`, `dxgidebug.h`, Win32 headers and import libraries | Nothing redistributed; the runtime is part of Windows |
| DirectX Shader Compiler (`dxc.exe`, `dxcompiler.dll`, `dxil.dll`) | 1.8.2502.11 (from the SDK above) | Windows SDK `bin\10.0.26100.0\x64` | Included with the SDK (DXC itself is LLVM/NCSA licensed on GitHub) | Build-time HLSL to signed DXIL | Not shipped; shaders are precompiled |
| D3D12 debug layer / DRED / DXGI debug | Windows 11 25H2 in-box | "Graphics Tools" optional feature for the debug layer | Windows | Development validation only | Not shipped |
| MSVC toolset | 14.51.36231 (cl 19.51.36256), Visual Studio Community 2026 18.9.12120.119 | Visual Studio Installer | Visual Studio license | Compiler and linker | Release builds need the Visual C++ Redistributable (M7) |
| CMake | 4.3.1-msvc1 (bundled with Visual Studio) | Visual Studio Installer, or `winget install Kitware.CMake` | BSD-3-Clause | Build system generation, CTest | Not shipped |
| Ninja | 1.13.2 (bundled with Visual Studio) | Visual Studio Installer | Apache-2.0 | Available on `PATH` via `tools/env.ps1`; the presets use MSBuild | Not shipped |
| Git | 2.55.0.windows.3 | git-scm.com | GPL-2.0 (tool only) | Optional: build stamps the commit into `build_info.h`; builds without it report `unknown` | Not shipped |

Header-only in-house code replaces what might otherwise be dependencies at this stage: PNG and
PFM encoders (`src/core/image_write.*`), a JSON writer (`src/core/json_writer.*`), the math layer
(`src/core/math`), and the test harness (`tests/cpu/lc_test.h`).

## Planned evaluations (not added)

| Candidate | Spec section | When |
|---|---|---|
| Dear ImGui (diagnostic panel) | §3, §16 | M2/M3, when the raw/reference toggles need a panel |
| A JSON parser (scene files) | §16 | M2/M3 |
| NVIDIA NRD | §13 | M4, after the raw renderer passes |
| NVIDIA Streamline (DLSS Super Resolution) | §13 | M4, after a stable native-resolution path |
| miniaudio | §15 | M5 |
| Microsoft D3D12 Agility SDK | §3 | Only if a feature beyond the in-box runtime is needed |

Each addition must pin a tag or commit, record its license text, and be added to this table
before it is used.
