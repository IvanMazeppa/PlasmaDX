## Claude Code + WSL Workflow (2025-09-17)

Purpose
- Keep MSVC/CMake builds on Windows while letting Claude/WSL safely build shaders.

Golden rules
- Only compile HLSL in WSL. Do not run CMake/MSBuild from WSL.
- Windows MSVC build consumes `.dxil` artifacts from `shaders/dxr`.

One-time setup (Windows)
- Ensure Visual Studio 2022, Windows 11 SDK, Agility SDK, PIX installed.
- DXC available at `tools/dxc.exe` (used by CMake target `shaders`).

One-time setup (WSL)
    sudo apt update && sudo apt install -y wget unzip
    wget https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip -O dxc.zip
    unzip dxc.zip -d ~/dxc && echo 'export PATH="$HOME/dxc:$PATH"' >> ~/.bashrc
    source ~/.bashrc

Edit/build shader in WSL
    # Repo at /mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX
    cd /mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX
    # Edit shaders/dxr/raytracing_lib.hlsl
    # Build DXR library
    mkdir -p shaders/dxr
    dxc -T lib_6_3 -Fo shaders/dxr/raytracing_lib.dxil shaders/dxr/raytracing_lib.hlsl

Build app on Windows
    cmake --build build-vs2022 --config Debug --target PlasmaDX

Run options
    # Normal with debug layer
    ./build-vs2022/Debug/PlasmaDX.exe
    # Keep window open on device removal
    $env:PLASMADX_NO_QUIT_ON_REMOVAL="1"; ./build-vs2022/Debug/PlasmaDX.exe
    # Disable debug layer noise
    $env:PLASMADX_NO_DEBUG="1"; ./build-vs2022/Debug/PlasmaDX.exe

Troubleshooting
- LNK1168 (cannot open exe): close running app before building.
- DXIL not picked up: rebuild shader target or confirm `.dxil` timestamp and size.
- CreateStateObject E_INVALIDARG: verify export names (RayGen/Miss/ClosestHit), reserve subobject vector, confirm associations.
- HDR creation failure: do not pass D3D12_CLEAR_VALUE for UAV-only resources.

Checklist per change
- Shader changed: rebuild `.dxil` in WSL or run `shaders` target on Windows.
- App build: `cmake --build build-vs2022 --config Debug --target PlasmaDX`.
- Validate in PIX when changing barriers or state objects.
