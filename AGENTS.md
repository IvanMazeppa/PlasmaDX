# Repository Guidelines

## Project Structure & Module Organization
PlasmaDX is a C++20 DirectX 12/DXR app. `src/` holds the runtime, split into `core` (window, device, loop), `dxr` (acceleration structures, pipeline, shader binding table), `renderer` (compose path), `volumetric` (density + marcher), and `utils`. Shared headers live in `include/`. GPU shader sources are in `shaders/` (`shaders/dxr` for ray tracing, `shaders/vol` for compute density). Pre-generated artifacts and VS solutions belong under `build-vs2022*`. Logs, telemetry, and GPU captures land in `logs/` and `pix/`; keep large captures out of Git.

## Build, Test, and Development Commands
Run CMake from the repo root with Visual Studio 2022: `cmake -S . -B build-vs2022 -G "Visual Studio 17 2022" -A x64`. Build with `cmake --build build-vs2022 --config Debug`. Launch the renderer via `build-vs2022/Debug/PlasmaDX.exe`. Use `-DPLASMADX_MINIMAL_BASELINE=ON` during configure when you need a non-DXR sanity check. Shader compilation is handled automatically with `tools/dxc.exe`; re-run the build whenever HLSL changes.

## Coding Style & Naming Conventions
Follow Visual Studio defaults: tabs for block indentation, spaces for alignment, braces on the same line as the control statement. Prefer `PascalCase` for classes/structs (`App`, `RayMarcher`), `camelCase` for member functions (`initialize`, `CyclePreset`), and `g_` prefixes only for intentional globals (see `g_appInstance`). Keep logging consistent via `LOGI/LOGE`, and gate expensive debug output with clearly named toggles. Headers should include only what they need; favor forward declarations inside `*.h` files.

## Testing Guidelines
There is no automated test harness yet. Validate changes by building Debug, running the executable, and inspecting `PlasmaDX.log` plus PIX captures when GPU behavior changes. Exercise input toggles (`F1`–`F4`, `1–4`, camera WASDQE) to confirm interaction regressions. When touching DXR or shader code, capture a baseline frame with PIX and attach the diff summary in your review notes.

## Commit & Pull Request Guidelines
Match the existing Conventional-Commit style: `type(scope): summary` (e.g., `chore(hooks): allow deleting build artifacts and compiled shaders from index`). Commit frequently but keep logical units isolated. PRs should outline motivation, approach, and testing evidence (commands run, logs reviewed, screenshots or PIX captures if visuals changed). Link related roadmap items or issues and call out any follow-up work or risks so reviewers can respond quickly.
