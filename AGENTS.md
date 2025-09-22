# Repository Guidelines

## Project Structure & Module Organization
PlasmaDX targets DirectX 12 + DXR on Windows; keep the working copy at `/mnt/d/users/dilli/androidstudioprojects/plasmadx/` so hardcoded paths stay valid. Source lives in `src/`: `core` (app bootstrap & camera), `dxr` (AS builder, DXC integration, SBT), `volumetric` (density, particles, ray marching), `renderer` (composite), `utils` (descriptor heaps, env, logging), and `agents` (automation scripts). `shaders/` houses ray tracing and compute programs, `build/` holds generated binaries, `tools/` hosts helper executables, and `results/` plus `screenshot/` keep captures.

## Build, Test, and Development Commands
Configure once with `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`. Build or rebuild via `cmake --build build --config Debug` (swap Debug for Release when needed). Refresh DXR and compute binaries after shader edits using `cmake --build build --config Debug --target compile_shaders`. Launch the sample from `build/Debug/PlasmaDX.exe`, ensuring `bin/D3D12` sits beside the executable, or call `tools/dxc.exe` for quick shader experiments. Run Python agents with `python3 -m src.agents.technique_agent_runner`.

## Coding Style & Naming Conventions
Stick to 4-space indentation, Windows line endings, and same-line braces. Classes use PascalCase (`DensityVolume`), functions camelCase (`buildTLAS`), and members keep the `m_` prefix; prefer forward declarations in headers. Python modules in `src/agents` follow PEP 8 snake_case. Maintain shader filenames as lower_snake_case and keep entry points concise; match indentation to existing HLSL files. Apply clang-format (VS integration) on C++ edits before committing.

## Testing Guidelines
No automated tests ship yet, so rely on clean builds and manual validation. Before submitting changes, run a fresh Debug build, launch the app, verify swapchain resize, DXR dispatch, and volumetric passes, and watch for console warnings. Capture PIX or RenderDoc traces when touching synchronization or shader stages. Document any ad-hoc tooling in your PR and register future tests with CTest (`ctest --output-on-failure`).

## Commit & Pull Request Guidelines
Keep commits focused and mimic the current history: prefixed roadmap tags (`VOL_0003D: ...`) or clear imperative subjects. Mention regenerated `.dxil` assets when applicable. Pull requests should summarize scope, list validation steps, attach screenshots or captures for visual changes, and link roadmap tasks or issues. Request a GPU-savvy reviewer for DXR pipeline or shader-binding changes.

## Security & Configuration Tips
Do not relocate the repository; scripts expect `/mnt/d/users/dilli/androidstudioprojects/plasmadx/`. Keep the Agility SDK under `bin/D3D12/` in sync with tracked binaries and avoid checking in proprietary captures—store shareable artifacts under `results/`. Review batch or PowerShell scripts before executing on new machines.
