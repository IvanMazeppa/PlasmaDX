# Repository Guidelines

## Project Structure & Module Organization
Source lives under `src/`, split into focused modules: `core/` for windowing and device setup, `dxr/` for acceleration structures and raytracing pipeline, `volumetric/` for particle and raymarch logic, `renderer/` for frame orchestration, and `utils/` for shared helpers. Shaders are under `shaders/` with DXR libraries in `shaders/dxr/` and volumetric passes in `shaders/vol/`; compiled `.dxil` outputs are checked in. Agility SDK headers are stored in `include/`, runtime DLLs in `bin/`, and `tools/` contains the local `dxc.exe`. Build artifacts land in `build/` (or `build-vs2022/`). Always open the repo from `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX` so relative scripts resolve correctly.

## Build, Test, and Development Commands
Use `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` to generate the Visual Studio solution. Build Debug binaries with `cmake --build build --config Debug`. Launch the app via `build/Debug/PlasmaDX.exe`; it compiles shaders automatically through the `compile_shaders` target. For a minimal feature set, configure with `-DPLASMADX_MINIMAL_BASELINE=ON`.

## Coding Style & Naming Conventions
Code is C++20 with MSVC `/W4`. Match existing indentation (tabs are common) and keep braces on the same line. Classes use `PascalCase`, methods `camelCase`, members `m_` prefixes, and constants `kName`. HLSL entry points stay as `main` for compute stages, and DXR libraries belong beneath `shaders/dxr/`.

## Testing Guidelines
No automated harness exists. Validate changes by building Debug and running `PlasmaDX.exe`, confirming window creation, DXR capability checks, and a rendered frame. Set `PLASMADX_LOG_FILE` to capture diagnostics and attach PIX when investigating GPU markers.

## Commit & Pull Request Guidelines
Write imperative commit subjects, optionally scoped (e.g., `DXR_0019: Update pipeline config`). Pull requests should describe the rationale, outline test steps, and attach logs or PIX captures when relevant. Call out hardware and driver details for GPU issues and update docs when introducing new commands or folders.

## Security & Configuration Tips
Ensure VS2022, the Windows 10/11 SDK, and Agility SDK DLLs are installed. After building, verify `dxcompiler.dll`, `dxil.dll`, and required D3D12 binaries sit beside `PlasmaDX.exe` under the chosen configuration directory.
