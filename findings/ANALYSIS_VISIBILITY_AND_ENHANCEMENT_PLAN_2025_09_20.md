# Visibility Analysis and Enhancement Plan (2025-09-20)

## Summary
Current output resembles homogeneous fog with occasional shapes. Density fill is confirmed working; root causes likely lie in ray setup, volume bounds intersection, marching integration, and parameter ranges.

## What’s Happening
- Density volume is populated (logs confirm analytic/procedural fill and PSO creation).
- Composite + HDR path is stable; barriers fixed.
- Marcher likely not integrating enough contrast due to raygen or transport parameters.

## Fix Strategy (Compute-First)
1) Validate ray generation and AABB intersection (VOL_0003A).
2) Establish a known density baseline via analytic sphere and a direct density probe view (VOL_0003B).
3) Add transport parameter sweeps and exposure presets to reach strong visibility quickly (VOL_0003C).
4) Add debug visualization modes to diagnose remaining issues and prep for skipping validation (VOL_0003D).

## Technical Details
- Raygen: derive world rays from camera CBV(b0) using inverse view-projection; compute ndc → world mapping, normalize.
- AABB: compute entry/exit t for unit volume; early out if miss; clamp t range to [0,1] volume space.
- Beer–Lambert: T *= exp(-sigma_t * density * stepSize); output radiance L = L0 * T for absorption-only.
- Parameters: densityScale (1–5), absorption (0.1–0.5), stepSize (0.01–0.04), maxSteps (64–256), exposure (1–16).
- Barriers: UAV barrier after density writes; transition HDR to SRV only before Composite; avoid redundant transitions.

## Enhancements (after visibility restored)
- Temporal accumulation + jitter (VOL_0004).
- Empty-space skipping with min-max/occupancy (VOL_0005).
- Tonemapping/exposure/gamma adjustments (RENDER_0001).
- DXR parity re-enable with SBT (DXR_0025).

## MCP References (for quick lookup)
- D3D12_RESOURCE_BARRIER, D3D12_RESOURCE_STATES, D3D12_DESCRIPTOR_RANGE, D3D12_DESCRIPTOR_RANGE1, D3D12_DISPATCH_RAYS_DESC, DispatchRays.

## Validation & Testing
- PIX: check CBV bindings, verify barriers timeline, confirm stable GPU timings.
- Logs: print parameter set once/sec; keybind toggles reflected; modes enumerated.

## Risks
- Root signature mismatches if adding CBV/UAV/SRV without updating PSOs.
- Over-aggressive parameters causing NANs; clamp ranges and guard divisions.

## Next Steps
- Execute VOL_0003A → VOL_0003D in order, validating at each step with PIX and logs.
- If visibility remains poor after 0003B/0003C, capture shader source + screenshots for deeper review.
