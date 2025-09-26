## Companion Guide: Post-VOL_0002 Execution Order and Rationale (2025-09-19)

This guide synthesizes the current project state from `results/` and defines the exact order to execute remaining jobs, with acceptance criteria and validation hooks.

### What the results say (condensed)
- DXR_0001…0022: Foundation is strong (Agility SDK, PIX, descriptor allocator, env/input). DXR PSO creation was previously blocked by UAV root descriptor and DXC; we fixed UAV binding via descriptor table and will use offline DXC when re-enabling DXR.
- VOL_0001: Compute path writes visible HDR content; stable frame loop; PIX markers present.
- VOL_0002: Density Texture3D confirmed on-screen. Recorded as `results/VOL_0002_density_grid_result.json` with screenshot.
- DXR_0019: Camera exists but lacks input integration and CBV binding in render loop. This is a gating item for volumetrics.

### Strict execution order
1) CORE_0003: Camera input + CBV(b0) binding
   - Bind 256B camera constants in all compute passes; wire WASD + mouse.
   - Accept: moving camera changes density view; PIX shows CBV bound.

2) VOL_0003: Compute ray marcher (directional + self-shadow)
   - New `shaders/vol/raymarch.hlsl`; march through density, Beer–Lambert transmittance, single-scatter directional lighting with shadow ray (cheap fixed-step occlusion or cone).
   - Params: stepSize, maxSteps, densityScale, sigma_s, sigma_a, lightDir.
   - Accept: coherent volumetric shading visible; performance ~30–60 FPS at 128³.

3) RENDER_0001: Tonemap + exposure + gamma
   - Add exposure/gamma to Composite for consistent display.
   - Accept: user can tweak exposure/gamma at runtime; no clipping/banding.

4) VOL_0004: Temporal accumulation + jitter hooks
   - Add history RGBA16F; Halton jitter on projection; simple reprojection; reset on big camera moves.
   - Accept: reduced noise at same cost; togglable; history resets correctly.

5) VOL_0005: Empty-space skipping
   - Build 3D occupancy/min-max (e.g., 8³ bricks) and skip empty bricks in marcher.
   - Accept: measurable speedup at same visual quality; debug overlay shows skipped bricks.

6) DXR_0025: Re-enable DXR after compute parity
   - Offline DXC for raygen/miss/closesthit; fix SBT; bind TLAS SRV and HDR UAV via descriptor tables; add camera CBV.
   - Accept: DXR path renders parity image vs compute within tolerance; PIX shows PSO + SBT valid.

### Validation per step
- Always capture a short PIX run (2–3 frames). Check: root params, barriers, and UAV→SRV transitions before Composite.
- Log key parameters every second (camera, exposure, marcher settings) to `PlasmaDX.log`.
- On resize: verify HDR + density + history textures recreate without leaks.

### Keyboard toggles (suggested)
- P: pause; F1: verbose logging; F2: log checkpoint.
- 1/2: toggle slice vs raymarch; +/-: exposure; [ ]: gamma.
- 9/0: stepSize, ;' (semicolon/quote): density scale.

### Notes on risk and ordering
- Camera CBV is a strict prerequisite for VOL_0003–0005 and DXR_0025.
- Tonemapping is small but improves iteration quality; place it right after VOL_0003.
- Temporal and skipping can be iterated independently once the marcher is stable.

### Deliverables to produce
- results/VOL_0003_compute_ray_marcher_result.json (with screenshots, settings)
- results/RENDER_0001_tonemap_result.json
- results/VOL_0004_temporal_result.json
- results/VOL_0005_empty_space_result.json
- results/DXR_0025_reenable_result.json

This order keeps risk low, maintains a visible output every step, and positions DXR re-enablement on a stable compute baseline.


