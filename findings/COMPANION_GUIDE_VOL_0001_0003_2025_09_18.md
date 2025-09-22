## Companion Guide: VOL_0001 .. VOL_0003 (2025-09-18)

Sequence
1) VOL_0001 — GPU Particles (Compute)
   - Add `src/volumetric/Particles.{h,cpp}` with buffers and dispatch.
   - `shaders/vol/particles_update.hlsl`: update pos/vel; CB contains dt/time.
   - Integrate into frame: before composite, after clearing HDR, write a color ramp to HDR for validation.
   - PIX markers: "Particles/Update".

2) VOL_0002 — Density Grid from Particles
   - Add `src/volumetric/Density.{h,cpp}` managing a 3D R16F texture with UAV/SRV.
   - `shaders/vol/density_splat.hlsl`: convert particle pos to [0,1]^3 → u16 coords, atomic add.
   - `shaders/vol/density_slice_debug.hlsl`: copy chosen Z slice to HDR to visualize occupancy.
   - PIX markers: "Density/Splat", "Density/SliceDebug".

3) VOL_0003 — Compute Ray Marcher
   - Add `src/volumetric/RayMarcher.{h,cpp}`; CS reads density SRV and writes HDR UAV.
   - March along view rays using camera matrices; Beer–Lambert absorption; fixed light dir for now.
   - Parameters in a small CB: steps, stepSize, densityScale, exposure.
   - PIX markers: "Marcher/RayMarch".

Build & Run
- Shaders: compile via Windows target or WSL DXC per `CLAUDE_CODE_AND_WSL_WORKFLOW_2025_09_17.md`.
- App: `cmake --build build-vs2022 --config Debug --target PlasmaDX` then run.

Debugging tips
- Always validate barriers: UAV→SRV before sampling density or HDR; SRV→UAV before writing.
- Guard out-of-bounds in compute shaders; clamp indices.
- On resize: release and recreate HDR, density 3D textures, and any descriptors.
- Use PIX captures on first success; verify dispatch sizes and resource states.

Acceptance checks
- After VOL_0001: time-varying HDR pattern and valid dispatch in PIX.
- After VOL_0002: slice view shows coherent particle clusters.
- After VOL_0003: visible fog that changes with parameters; stable frame times.


