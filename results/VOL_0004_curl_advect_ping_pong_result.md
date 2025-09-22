VOL_0004: Curl-Advection + Ping-Pong Density (Compute-Only Baseline)

Summary
- Implemented curl-advection compute shader `shaders/vol/density_advect_curl.hlsl` (compiled to DXIL).
- Added ping-pong 3D density textures in `DensityVolume` (A/B) with descriptor pairs and state tracking.
- Integrated per-frame `AdvectCurl` in `App::renderFrameDXR` gated by `PLASMADX_USE_CURL` (default 1).
- Preserved analytic sphere baseline behind `PLASMADX_FILL_SPHERE` (runs once when enabled).
- Ensured correct descriptor heap binding and resource transitions (SRV/UAV + UAV barriers).

How to Run
1) Build Debug (Visual Studio 2022 or CMake/Ninja). Shaders are compiled by CMake and copied post-build.
2) Launch compute-only with curl advection enabled:
   - PowerShell:
     $env:PLASMADX_DISABLE_DXR='1'; $env:PLASMADX_NO_DEBUG='1'; $env:PLASMADX_USE_CURL='1'; $env:PLASMADX_NO_QUIT_ON_REMOVAL='1'; build-vs2022\Debug\PlasmaDX.exe
3) Optional toggles:
   - $env:PLASMADX_FILL_SPHERE='1'   # one-time analytic sphere fill to both ping-pong textures
   - $env:PLASMADX_DEBUG_MODE='4'    # UVW visualizer; '5' for step heatmap

Validation Notes
- Visible evolving density with tendril-like motion and central injection/decay.
- Density read state: TransitionToSRV on current src before ray march; write state: AdvectCurl transitions dst→UAV.
- After AdvectCurl dispatch, UAV barrier on dst and swap src/dst for next frame.

Files Touched
- src/volumetric/DensityVolume.h/.cpp: ping-pong resources, root sig/PSO for Advect, AdvectCurl(), transitions, sphere/analytic updates.
- src/core/App.cpp: hooked AdvectCurl per-frame via PLASMADX_USE_CURL.
- shaders/vol/density_advect_curl.hlsl: curl-like velocity, trilinear backtrace, inject/decay.
- CMakeLists.txt: DXC step for density_advect_curl.hlsl (already present).

Next Steps
- Expose advect parameters via presets (PLASMADX_PRESET) and hotkeys.
- Add occupancy/mip for empty-space skipping (VOL_0007) after stabilization.

