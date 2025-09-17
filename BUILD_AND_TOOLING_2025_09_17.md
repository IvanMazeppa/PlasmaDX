## Build, Reconfigure, and Tooling Guide (2025-09-17)

Goals
- Reliable configure/reconfigure with Agility SDK and DXC
- Clear recovery steps for common build/run failures
- Safe shader compilation from WSL for Claude without breaking Windows build

Prerequisites
- Visual Studio 2022 (MSVC v143), CMake for Windows, Windows 11 SDK (10.0.26100+), HLSL Tools, PIX for Windows
- Agility SDK deployed by the project (D3D12 exports in src/core/D3D12AgilitySDK.cpp, DLLs copied next to exe by CMake)
- DXC at tools/dxc.exe (used by CMake custom target "shaders")

Recommended build directories
- Keep generators isolated to avoid cache mismatch
  - Visual Studio: build-vs2022 (current)
  - Ninja (optional): build-ninja

Clean reconfigure (fixes generator/platform mismatch)
    Remove-Item -Recurse -Force build-vs2022 -ErrorAction SilentlyContinue
    cmake -S . -B build-vs2022 -G "Visual Studio 17 2022" -A x64 -DPLASMADX_MINIMAL_BASELINE=OFF
    cmake --build build-vs2022 --config Debug --target shaders,PlasmaDX

Run
    ./build-vs2022/Debug/PlasmaDX.exe
If the window exits immediately, run under PIX or launch from a shell to see logs. The app will exit on device removal by design.

Shader rebuild only
    cmake --build build-vs2022 --config Debug --target shaders

Common failure modes and fixes
- Device removed or allocator reset spam
  - We use per-frame fences. If it recurs, capture in PIX and check DRED messages printed to the console.
- Agility linkage errors (D3D12SDKVersion/Path)
  - Ensure exports are at global scope in D3D12AgilitySDK.cpp and that DLLs (D3D12Core.dll, d3d12SDKLayers.dll) are copied next to the exe.
- Shaders not compiling
  - Verify tools/dxc.exe exists; rebuild target "shaders"; ensure shaders/dxr/raytracing_lib.hlsl is present.
- Cache mismatch on configure
  - Always clear the build folder when switching generator/arch or major options; see clean reconfigure above.

WSL shader builds (for Claude)
- Install DXC in WSL (Ubuntu)
    sudo apt update && sudo apt install -y wget unzip
    wget https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip -O dxc.zip
    unzip dxc.zip -d ~/dxc && echo 'export PATH="$HOME/dxc:$PATH"' >> ~/.bashrc
    source ~/.bashrc
- Compile DXR library in WSL (only produces .dxil; do not run MSVC build from WSL)
    # in /mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX
    mkdir -p shaders/dxr
    dxc -T lib_6_3 -Fo shaders/dxr/raytracing_lib.dxil shaders/dxr/raytracing_lib.hlsl
- Windows build will consume shaders/dxr/raytracing_lib.dxil directly. Keep line endings LF/CRLF agnostic.

PIX workflow
- Start a GPU Timing Capture around first frame
- Validate resource barriers: backbuffer Present->RTV, HDR UAV->SRV (after 0018), RTV->Present
- Verify ExecuteCommandLists encloses transitions, (later) DispatchRays, composite, Present

Quick checklist before commit
- Build: cmake --build build-vs2022 --config Debug --target PlasmaDX
- Run: window shows animated gradient; no infinite error spam
- Logs: InfoQueue logs only; no break-ons
- Shader: shaders/dxr/raytracing_lib.dxil exists and loads



