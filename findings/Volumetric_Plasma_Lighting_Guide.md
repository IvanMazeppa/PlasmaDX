### Volumetric Plasma Lighting and Self-Shadowing in PlasmaDX (DX12/DXR)

This document is a comprehensive, implementation-focused guide for lighting and self-shadowing volumetric plasma (e.g., orbiting accretion disks, confined plasma in wireframe cages) on RTX 4060 Ti + Ryzen 5950X (32 GB). It references the detailed DXR technique reports in `reports/` and explains how to use the new volumetric scaffold added to `shaders/` and `src/dxr/`.

Contents
- When to use DXR for volumetrics
- Representation choices (volume grid, procedural field, particles)
- Recommended hybrid pipeline
- HLSL scaffold (ComputeVolumetric + RayQuery visibility)
- C++ integration skeleton (`VolumetricPass`)
- Denoising and temporal accumulation
- Performance guidance and quality controls
- Debugging and validation
- References to reports

### When to use DXR for volumetrics

Use DXR selectively for volumetrics:
- Inline RayQuery for light visibility along the volumetric integration path at coarse intervals.
- DXR for visibility against solid geometry (cages/meshes) rather than for every step of the volume march.

Avoid per-step `TraceRay`/closest-hit; instead, run sparse visibility checks and reuse via temporal accumulation or neighborhood filters. See `reports/DXR_Inline_Ray_Tracing_RayQuery_Guide.md` and `reports/DXR_Ray_Traced_Shadows_Guide.md`.

### Representation choices

- 3D density/temperature texture (recommended): easy to ray march; supports SDF-like bounds; friendly to denoisers and temporal reprojection.
- Procedural field (noise/curl): compute density on the fly; cache to a 3D texture for stability and LODs if needed.
- Particles: good for dynamic effects; integrate into a grid (splat) for volumetric lighting, or use tile-based accumulation.

### Recommended hybrid pipeline

1) Primary volumetric ray march in compute at half resolution (64–128 steps, early exit via transmittance).
2) Single-scattering: evaluate in-scattering from discrete lights at sparse intervals (every 4–8 steps). For a point light, use RayQuery visibility with `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | FORCE_OPAQUE`.
3) Self-emission: integrate emissive term from density/temperature (blackbody LUT optional).
4) Temporal accumulation (30–60 frames) and denoising (NRD ReLAX for radiance; SIGMA for visibility).
5) Composite with scene and post-process.

### HLSL scaffold

Files added:
- `shaders/VolumetricCommon.hlsli`: common CBs, resource bindings, helpers (HG phase, density sampling, ray reconstruction).
- `shaders/ComputeVolumetric.hlsl`: compute kernel with marching, optional Inline RayQuery visibility.

Highlights:
- RayQuery visibility only every N steps (configurable): amortizes cost.
- Instance mask for TLAS to limit hits (exclude decals/particles).
- Outputs radiance (RGB) and hit distance (for denoising/temporal reprojection).

Configuration fields (subset):
- `gStepSize`, `gMaxSteps`, `gShadowStepInterval`, `gSigmaExtinction`, `gAnisotropy`, `gEmissionScale`, `gDensityScale`, `gInstanceMask`.
See the source for the full list.

### C++ integration skeleton

Files added:
- `src/dxr/VolumetricPass.h/.cpp`

Responsibilities:
- Build a minimal compute pipeline (CS 6.5) and a root signature providing: constants (b0), RTAS SRV (t0), density volume SRV (t1), sampler (s0), output UAVs (u0/u1).
- Dispatch compute with 8×8 threads; constants include inverse view-projection, camera/light parameters, and integration params.

Integration steps:
1) Ensure TLAS/BLAS built: see `reports/DXR_Acceleration_Structures_Guide.md`.
2) Create or update a 3D density texture SRV bound to t1; sampler to s0.
3) Create UAVs for `OutRadiance` and `OutHitDist` (match `gOutputSize`).
4) Set descriptor heap(s) so t1/s0/u0/u1 are visible to the compute pipeline and call `VolumetricPass::Dispatch(...)`.
5) Composite `OutRadiance` into your HDR target.

Note: The stub uses root constants for simplicity; adapt to your binding model as needed.

### Denoising and temporal accumulation

Use NRD (see `reports/DXR_Denoising_NRD_Integration_Guide.md`):
- For radiance: ReLAX (temporal + spatial). Inputs: radiance, normals (surface or view-aligned proxy), motion vectors, hit distance, roughness proxy if used.
- For visibility term (if output separately): SIGMA. In the scaffold we directly blend visibility into radiance; you can optionally output a separate visibility to denoise then modulate lighting.
- Temporal accumulation: accumulate radiance across frames with jitter and history clamping. Use motion vectors; reset on disocclusions.

### Performance guidance (RTX 4060 Ti)

- Resolution: start at half-res; upscale with edge-aware upsampler using scene depth/normal.
- Steps: 64–96 typical; tune `gStepSize` and early exit when transmittance < 1e-3.
- Visibility: `gShadowStepInterval` 6–8; clamp ray `tMax` to light distance.
- Masks: set `gInstanceMask` to exclude irrelevant geometry for visibility rays.
- Phase function: HG with `g` ~ 0.6–0.85 sells forward glow.
- Emission: cheap, drives the look; avoid evaluating costly visibility for pure self-emission unless needed.

### Quality controls

- Density field MIPs for empty-space skipping and stable sampling.
- Blue-noise jitter per frame to reduce banding; combine with temporal accumulation.
- Optional deep shadow map volume for fast transmittance from point/directional lights as an alternative to DXR visibility.

### Debugging and validation

- Visualize: step heatmap, transmittance, integrated radiance, candidate visibility count.
- PIX: verify TLAS bound, instance mask correct, and RayQuery flags as expected.
- Sanity: clamp radiance; check exposure in composite.

### References to reports

- Acceleration Structures: `reports/DXR_Acceleration_Structures_Guide.md`
- Inline Ray Tracing (RayQuery): `reports/DXR_Inline_Ray_Tracing_RayQuery_Guide.md`
- Ray-Traced Shadows: `reports/DXR_Ray_Traced_Shadows_Guide.md`
- Reflections (hybrid ideas apply to volumetric fallback strategies): `reports/DXR_Ray_Traced_Reflections_Guide.md`
- ReSTIR (for many lights; adopt DI ideas for volumetrics): `reports/DXR_ReSTIR_DI_GI_Guide.md`
- Denoising (NRD): `reports/DXR_Denoising_NRD_Integration_Guide.md`

### Next steps

- Wire `VolumetricPass` into your frame after G-buffer and before lighting composite.
- Add NRD integration passes consuming `OutRadiance`/`OutHitDist`.
- Replace unit-cube volume bounds with your accretion disk/plasma field bounds and density sampling.
- Optionally add a Deep Shadow Map path and toggle vs RayQuery visibility for profiling.

With this scaffold and the referenced reports, you can render self-emissive, self-shadowed plasma volumes efficiently, reserving DXR for sparse visibility queries while keeping the heavy lifting in a coherent compute march suitable for real-time constraints.


