# Repository Guidelines

## Project Structure & Module Organization
Source lives under `src/` with focused modules: `core/` (windowing, devices), `dxr/` (acceleration structures, pipeline, SBT), `volumetric/` (particles, density, raymarch), `renderer/`, and `utils/`. HLSL shaders sit in `shaders/` with DXR libraries in `dxr/` and volumetric passes in `vol/`; compiled `.dxil` files are checked in. Agility SDK headers are under `include/`, runtime DLLs in `bin/`, and local `dxc.exe` under `tools/`. CMake builds land in `build/` (or `build-vs2022/`). Always open the repo from `/mnt/d/users/dilli/androidstudioprojects/plasmadx/` so relative scripts resolve.

## Build, Test, and Development Commands
Configure Visual Studio solution: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`. Build Debug binaries: `cmake --build build --config Debug`. Run the app: `build/Debug/PlasmaDX.exe`. For a stripped baseline, add `-DPLASMADX_MINIMAL_BASELINE=ON` during configure. Shader compilation happens via the `compile_shaders` target, invoked automatically by `PlasmaDX`.

## Coding Style & Naming Conventions
Code targets C++20 with MSVC `/W4`. Follow existing indentation (tabs common) and same-line braces. Classes use `PascalCase`, methods `camelCase`, members `m_` prefixes, and constants `kName`. Keep HLSL entry points as `main` for compute and place DXR libraries beneath `shaders/dxr/`.

## Testing Guidelines
No unit harness yet; validate builds by running `PlasmaDX.exe` and inspecting GPU output. Set `PLASMADX_LOG_FILE` to redirect logs. Attach PIX when debugging GPU markers. Baseline verification should confirm window creation, DXR capability checks, and a rendered frame.

## Commit & Pull Request Guidelines
Write imperative commit subjects, scoped with subsystem tags when helpful (e.g., `DXR_0019: Update pipeline config`). PRs must summarize changes, rationale, test steps, and include logs or PIX captures when relevant. Call out hardware/driver info for GPU issues and update docs when introducing new commands or folders.

## Security & Configuration Tips
Ensure VS2022, Windows 10/11 SDK, and the Agility SDK DLLs are present. Post-build, confirm `dxcompiler.dll`, `dxil.dll`, and `D3D12` binaries sit next to the executable under `build/<config>/`.
