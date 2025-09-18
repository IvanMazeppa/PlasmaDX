## PlasmaDX Roadmap (2025-09-18) — Content-First Path

Objective
- Get meaningful visuals on screen quickly (particles → density → compute marcher), then migrate to DXR for lighting/shadows.

Guiding principles
- Favor compute path first for iteration; keep DXR hooks ready but not blocking.
- Single, clear data flow: Particles → Density3D → HDR → Composite.
- Instrument with PIX markers; aggressive error logging; resize-safe resources.

Milestones
1) VOL-0001: Minimal GPU Particles (Compute)
   - Structured buffer (pos.xyz, vel.xyz, life), N=65k default.
   - CS updates positions with gravity/damping; wrap or bounce in bounds.
   - Debug draw: write simple pattern to HDR to confirm pass execution.
   - Acceptance: particle buffer updates per frame (validated in PIX); HDR shows time-varying pattern.

2) VOL-0002: Density Grid from Particles (Compute)
   - Create 3D density texture (R16_FLOAT, size 128³ to start).
   - CS splat particles to 3D grid (atomic adds in shared or global, start simple).
   - Slice debug: copy a Z slice to HDR to visualize occupancy.
   - Acceptance: density histogram sane; slice shows clustered points; stable under motion.

3) VOL-0003: Compute Ray Marcher → HDR
   - CS marches rays per pixel over density; Beer–Lambert absorption; fixed light dir.
   - Parameters: step count, density scale, absorption, exposure.
   - Output HDR UAV; composite to swapchain as today.
   - Acceptance: visible fog/plasma with controllable thickness; no UAV out-of-bounds; PIX shows bounded dispatch.

4) VOL-0004: Camera & Controls
   - View/projection constant buffer; WASD + mouse look; toggle slice/march.
   - Acceptance: camera affects marcher; toggles reflected in logs and output.

5) VOL-0005: Quality/Perf Controls
   - Grid size presets (96³/128³/192³), step counts, jitter option.
   - Acceptance: presets switch live; FPS scales predictably.

6) VOL-0006: Directional Lighting & Volumetric Self-Shadowing (Single-Scattering)
   - In compute marcher, integrate single-scattering with Henyey–Greenstein phase.
   - For each view step, march toward light to estimate transmittance (light integral).
   - Acceptance: believable soft shadowing in the medium; parameters (g, sigma) affect look.

7) VOL-0007: Temporal Accumulation & Reprojection
   - Jittered sampling (STBN/blue-noise), history reprojection, neighborhood clamp.
   - Acceptance: reduced noise with motion stability; controllable accumulation rate.

8) VOL-0008: Density Mip-Chain & Occupancy Grid (Empty-Space Skipping)
   - Build 3D mip chain for density; occupancy grid (brick mask) for skipping.
   - Adaptive step sizing (cone stepping) from mip level.
   - Acceptance: significant perf win with similar visual quality.

9) VOL-0009: God Rays & Advanced Effects (In-Medium Shafts)
   - Light-space integration for shafts; temporal accumulation; optional screen-space assist.
   - Acceptance: visible shafts with adjustable intensity/softness.

10) DXR-Stage A: DXR Raygen Marcher (Parity)
   - Port compute marcher to DXR raygen for future flexibility; no external occluders.
   - Acceptance: visual parity with compute; CreateStateObject success.

Risk controls
- Barriers: explicit UAV→SRV and SRV→UAV where needed; Present path clean.
- Bounds checks in all compute/RT kernels; guard empty resources.
- Size-dependent resource recreate with null checks.

References
- See `PROJECT_OVERVIEW_AND_TECHNICAL_SPECIFICATION.md` for architecture and targets.


