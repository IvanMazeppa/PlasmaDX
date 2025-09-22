# VOL_0004: Preset System + Curl-Advection Density Baseline

## Goal
Deliver an immediately visible "plasma in a box" baseline with tendril-like motion, growth, and decay, while establishing a preset system to iterate effects cleanly. This decouples volumetric dynamics from particle/SPH complexity so we can iterate visuals and validate marching/lighting paths.

## Approach
- Add a preset system with a simple runtime selector and JSON-backed parameters.
- Implement a compute pass that advects density using curl-noise velocity in a 3D grid (semi-Lagrangian).
- Ping-pong two 3D density textures each frame; inject density near center; apply decay; clamp at box walls to create soft bounce.
- Keep ray marching in compute path (PLASMADX_DISABLE_DXR=1) until visuals are solid. Use debug modes (Bounds/UVW/Steps) to validate mapping and coverage.

## Why This Works Now
- No reliance on particles or float atomics; avoids data upload gaps.
- Produces compelling motion (tendrils/filaments) quickly.
- Preset system frames future work (accretion disc, torus, jets) without re-wiring core paths.

## Preset System
- Source: presets/vol_*.json
- Core fields (example):
```
{
  "name": "PlasmaBox",
  "grid": 128,
  "curlSpeed": 0.8,
  "decay": 0.995,
  "injectRate": 1.2,
  "injectRadius": 0.12,
  "flowScale": 1.5,
  "seed": 1337
}
```
- Selection: PLASMADX_PRESET=PlasmaBox (default), later a menu.

## New Pass: Curl-Advection
- Shader: shaders/vol/density_advect_curl.hlsl
- Inputs
  - Texture3D<float> srcDensity (t0)
  - Constants: deltaTime, time, curlSpeed, flowScale, decay, injectRate, injectRadius, gridDim
- Output
  - RWTexture3D<float> dstDensity (u0)
- Steps per voxel id
  1) uvw = (id + 0.5)/dim
  2) velocity v = curlNoise(uvw * flowScale + time)
  3) backtrace: uvwPrev = uvw − v * curlSpeed * deltaTime
  4) sample src: d = srcDensity.SampleLevel(trilinear, saturate(uvwPrev), 0)
  5) inject: d += injectRate * smoothstep(0, injectRadius, injectRadius − distance(uvw, 0.5))
  6) decay: d *= decay
  7) write: dstDensity[id] = d
- Boundaries: saturate sampling handles walls; optional v reflection near edges later.

## Integration Plan
- DensityVolume: allocate second 3D texture and descriptors; root sig (SRV t0, UAV u0, constants); PSO for advect.
- App.cpp: per-frame call m_densityVolume->AdvectCurl(cmdList, dt, time) when PLASMADX_USE_CURL=1 (default). Remove analytic fill from steady-state.
- Marcher unchanged aside from parameters; continue to expose debug modes 2/4/5.

## Expected Visuals
- Filament and vortex-like patterns that grow from center and swirl within the box.
- Bounds mode: clear green/yellow hits at volume; UVW mode: smooth gradients; Steps mode: higher through dense cores.

## Next (after baseline visible)
- Lighting model passes (emission + Beer-Lambert; then single scattering).
- Particle-driven density or SPH, reusing the same grid and marcher.
- DXR re-enablement and ray-traced shadows once stability proven.

---
Status: Approved direction for fast, meaningful progress toward the intended look while keeping architecture clean.

