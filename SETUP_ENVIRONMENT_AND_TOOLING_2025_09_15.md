## PlasmaDX Setup and Environment Guide (2025-09-15)

This document explains how to set up the full development environment for PlasmaDX (DX12 + DXR), including Windows host requirements, WSL shader compilation for Claude Code, DXC and Agility SDK setup, PIX usage, and a summary of source changes.

### 1) Prerequisites (Windows host)
- Windows 11 (latest updates)
- Visual Studio 2022 (C++ Game Development workload), Windows 10/11 SDK
- CMake 3.21+ (GUI or CLI)
- Latest NVIDIA/AMD GPU drivers with DXR support (RTX 4060 Ti supported)
- PIX for Windows (GPU debugger/profiler)
  - Docs: https://devblogs.microsoft.com/pix/documentation/
- DirectX 12 Agility SDK (optional but recommended)
  - Getting started, version table, and guidance: https://devblogs.microsoft.com/directx/directx12agility/
- DirectX Shader Compiler (DXC)
  - Releases: https://github.com/microsoft/DirectXShaderCompiler

### 2) Project layout expectations
- Agility SDK files deployed under `PlasmaDX/bin/D3D12/`
  - Typically contains `D3D12Core.dll` and `d3d12SDKLayers.dll` matching the chosen SDK version (e.g., 1.616 retail or 1.717 preview)
- DXC toolchain and runtime copies:
  - `PlasmaDX/tools/dxc.exe`
  - `PlasmaDX/lib/dxcompiler.lib`
  - `PlasmaDX/bin/dxcompiler.dll` and `PlasmaDX/bin/dxil.dll`
- CMakeLists.txt already copies the `bin/D3D12` and DXC runtime DLLs into the build output, and compiles shaders with DXC

### 3) Install and place artifacts
1) DXC (Windows)
   - Download latest release from DXC GitHub
   - Copy `dxc.exe` → `PlasmaDX/tools/dxc.exe`
   - Copy `dxcompiler.dll` and `dxil.dll` → `PlasmaDX/bin/`
   - Copy (or extract) `dxcompiler.lib` → `PlasmaDX/lib/dxcompiler.lib`
   - Optional: add `PlasmaDX/tools` to your PATH so `dxc.exe` is available globally
2) Agility SDK (optional)
   - Use the NuGet package for the chosen version (e.g., 1.616.1 retail or 1.717.1-preview)
   - Extract and place `D3D12Core.dll` and `d3d12SDKLayers.dll` into `PlasmaDX/bin/D3D12/`
   - Our CMake copies that directory next to the exe on build
   - Optional env vars (not required if copying next to EXE):
     - `D3D12SDKVersion=616` (or 717 for preview)
     - `D3D12SDKPath=D3D12\` (points to a subfolder next to the exe)

### 4) Build and run (Windows)
- Configure (VS generator shown):
  - `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
- Build:
  - `cmake --build build --config Debug`
- Run:
  - `build/Debug/PlasmaDX.exe`
- On successful run you should see a window with a changing clear color (fallback path) until the DXR path is fully implemented

### 5) WSL integration (Claude Code shader compilation)
Claude Code runs compilation steps within WSL. While the D3D12 app must be built on Windows/MSVC, you can compile DXR shaders inside WSL and drop `.dxil` outputs into the repo for the Windows build to pick up.

Option A: Install Linux DXC in WSL (recommended for shader-only tasks)
- In WSL (Ubuntu), download the Linux DXC binaries from:
  - https://github.com/microsoft/DirectXShaderCompiler
- Place `dxc` into `/usr/local/bin` or a project folder (e.g., `PlasmaDX/tools/linux/dxc`)
- Compile the DXR library to DXIL from WSL:
  - `dxc -T lib_6_3 -Fo /mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/dxr/raytracing_lib.dxil /mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/dxr/raytracing_lib.hlsl`
- The Windows build will either use this `.dxil` or overwrite it via its own DXC step; either is acceptable while iterating

Option B: Trigger Windows build from within Cursor
- Use the Windows terminal inside Cursor to invoke the normal CMake/VS build
- This keeps shader compilation on Windows via `PlasmaDX/tools/dxc.exe`

Notes
- You cannot run Windows `dxc.exe` directly inside WSL; use the Linux `dxc` binary instead
- The app itself must be built with MSVC (Windows SDK dependencies)

### 6) PIX workflow
- After the first successful run, open PIX and start a GPU capture.
- Verify:
  - Backbuffer state transitions (PRESENT↔RTV / UAV during DXR path)
  - Any InfoQueue messages (enable debug layer in Debug builds)
  - Timing for command queue submit and present
- Add PIX events around major steps as DXR lands (AS build, SBT build, DispatchRays)

### 7) Troubleshooting white window / silent exit
- Ensure the following files exist next to the exe:
  - `./D3D12/D3D12Core.dll`, `./D3D12/d3d12SDKLayers.dll` (if using Agility)
  - `./dxcompiler.dll`, `./dxil.dll`
  - `./shaders/dxr/raytracing_lib.dxil`
- Confirm the debug layer is on (Debug builds) and check the console (attach in code)
- If present/execute fails, log `GetDeviceRemovedReason()` and dump DRED (Device Removed Extended Data):
  - DRED overview: https://learn.microsoft.com/en-us/windows/win32/direct3d12/d3d12-device-removed-extended-data
- Verify GPU drivers and reboot after Agility/DXC changes

### 8) What changed in source
- New DXR stubs to unblock build and enable incremental implementation:
  - `src/dxr/ASBuilder.h/.cpp` — minimal placeholder allocations, `CreateTriangleBLAS`/`BuildTLAS` signatures wired
  - `src/dxr/Pipeline.h/.cpp` — minimal shell for DXR state creation; exposes `GetPSO()`/`GetPSOProperties()`
  - `src/dxr/SBT.h/.cpp` — minimal SBT interface returning a default `D3D12_DISPATCH_RAYS_DESC`
- CMake updates (already in your CMakeLists):
  - Includes DXR stubs in target sources
  - Compiles shaders with DXC: `tools/dxc.exe -T lib_6_3 -Fo shaders/dxr/raytracing_lib.dxil shaders/dxr/raytracing_lib.hlsl`
  - Copies DXC runtime DLLs and Agility SDK `D3D12` folder next to the exe
  - Links `lib/dxcompiler.lib` for potential reflection/advanced usage
- App framework (`src/core/App.*`):
  - Window/device/swapchain creation, debug layer in Debug builds, DXR tier check (Options5)
  - Fallback raster clear path while DXR pipeline is stubbed
  - Header declares `checkDeviceRemoved`, info queue helpers for future DXGI/D3D12 InfoQueue + DRED integration (see CRs below)

### 9) Change requests to apply next
- `changes/DXR_0011_console_infoqueue_dred.json`
  - Attach a console early in `initialize()`, enable DXGI/D3D12 InfoQueue, wrap `ExecuteCommandLists` and `Present` with HRESULT checks, dump DRED on device removal/hung
- `changes/DXR_0012_cmake_dxc_agility.json`
  - Standardize DXC compilation in CMake (already present), log Agility SDK version on startup; confirm env if you choose env-based loading
- `changes/DXR_0001_hello_pipeline.json` (from earlier)
  - Replace stubs with a minimal working DXR PSO/SBT and a raygen gradient to the backbuffer

### 10) References
- DXR overview and samples (Microsoft Learn):
  - https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-raytracing
  - https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-raytracing-samples-win32/
- DirectX-Specs (DXR): https://github.com/microsoft/DirectX-Specs
- DirectX-Headers (API surface): https://github.com/microsoft/DirectX-Headers
- DXC: https://github.com/microsoft/DirectXShaderCompiler
- PIX: https://devblogs.microsoft.com/pix/documentation/
- Agility SDK: https://devblogs.microsoft.com/directx/directx12agility/


