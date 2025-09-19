## PlasmaDX (DirectX 12 + DXR) Skeleton

> **🚨 DIRECTORY REQUIREMENT**: This project MUST be accessed from `/mnt/d/users/dilli/androidstudioprojects/plasmadx/` (lowercase). This is NOT PlasmaVulkan.

Purpose
- Real-time (and offline-capable) ray traced volumetric plasma renderer using DX12 + DXR.

Build
- Toolchain: Visual Studio 2022 (MSVC), CMake 3.21+, Windows 10/11 SDK with DX12 + DXR.
- Configure: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
- Build: `cmake --build build --config Debug`
- Run: `build/Debug/PlasmaDX.exe`
- Optional baseline: `-DPLASMADX_MINIMAL_BASELINE=ON` to disable DXR path and validate swapchain/device

Features (initial)
- Win32 window + DXGI swapchain
- D3D12 device + command queue/list
- DXR capability check (Options5 Tier)

Roadmap
1) Frame loop + resize-safe swapchain and fences
2) DXR acceleration structure boilerplate and a basic raygen/miss/hit pipeline
3) Compute-based volumetric density and ray marching to HDR, then TAA
4) External occluder TLAS and shadow integration

Notes
- Use PIX for Windows for GPU captures; enable debug layer in Debug builds.
- Agility SDK integrated via exports (see `src/core/D3D12AgilitySDK.cpp`) and `bin/D3D12/` copied next to the exe.
- DXC used for DXR shaders: `tools/dxc.exe` compiles `shaders/dxr/raytracing_lib.hlsl` → `.dxil` during build.
- WSL support: You can compile DXR shaders inside WSL (Linux `dxc`) and drop the `.dxil` into the repo; the app must still be built with MSVC on Windows.
