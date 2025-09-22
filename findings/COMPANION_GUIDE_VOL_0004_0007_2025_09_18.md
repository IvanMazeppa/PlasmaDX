## Companion Guide: VOL_0004 .. VOL_0007 (2025-09-18)

VOL_0004 — Directional Self-Shadowing
- Extend `ray_march_cs.hlsl`: per view sample, shoot a short march toward `lightDir` to estimate transmittance T_light; accumulate single-scattering: L += T_view * sigma_s * phase(g, cosTheta) * T_light * step.
- Add params: sigma_a, sigma_s, g, lightDir, lightIntensity.
- Clamp work: max light steps; early-out when T_view < epsilon.

VOL_0005 — Temporal Accumulation (TAA)
- Add history HDR; compute motion vectors from prev view-projection.
- Reproject and blend with clamp; reset history on resize/large param jumps.
- Add jitter sequence (STBN or blue-noise) to reduce banding.

VOL_0006 — Density Mip & Occupancy
- Build 3D mip chain of density (downsample sum or max depending on use).
- Occupancy grid per brick; marcher checks brick occupancy to skip ahead.
- Use adaptive step sizes (cone stepping) based on local mip level.

VOL_0007 — God Rays & Advanced Effects
- Accumulate light shafts along light direction with temporal smoothing.
- Expose intensity/softness controls; validate stability with camera motion.

Validation
- PIX: ensure UAV/SRV transitions correct; measure dispatch times before/after skipping.
- Console logs: print params and toggles on change for reproducibility.


