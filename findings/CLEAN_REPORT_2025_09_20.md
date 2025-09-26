## Clean Report — PlasmaDX (2025-09-20)

### Executive Summary
- Focus: Restore clear volumetric visibility and de-risk marching via compute-first diagnostics.
- Created 4 scoped jobs (VOL_0003A–D) + 2 guides to validate ray setup, verify density, tune transport, and add debug visualizations.
- Next action: Implement VOL_0003A (raydir + AABB bounds) to confirm raygen and volume intersection.

### Current Graphical State (from Claude report and local analysis)
- Output often appears as homogeneous fog / subtle color bands.
- Density fill path runs (DXIL found; PSOs built; dispatches logged), suggesting the issue lies in raygen/march/integration or parameter ranges.
- HDR pipeline stable; barriers corrected; DXR disabled until compute parity (DXR_0025).

### New Jobs Added
- `changes/VOL_0003A_ray_setup_and_bounds_debug.json`
  - Validate camera ray generation from CBV(b0); AABB entry/exit t; on-screen debug modes (RayDir, Bounds).
- `changes/VOL_0003B_density_probe_and_sphere_baseline.json`
  - Add analytic sphere density fill + grayscale density probe mode to isolate marching issues.
- `changes/VOL_0003C_transport_params_and_exposure_presets.json`
  - Hotkeys for densityScale/absorption/stepSize/maxSteps/exposure; HUD logging; safe ranges.
- `changes/VOL_0003D_debug_visualizations_and_step_heatmap.json`
  - Modes: RayDir, DensitySample, StepCount heatmap (for future empty-space skipping verification).

### Guides Added
- `findings/COMPANION_GUIDE_VOL_0003A_0003D_2025_09_20.md`
  - Strict order, MCP reading checklist, PIX targets, lifecycle notes.
- `findings/ANALYSIS_VISIBILITY_AND_ENHANCEMENT_PLAN_2025_09_20.md`
  - What’s happening, compute-first fix strategy, enhancements, risks, testing.

### Strict Execution Order
1) VOL_0003A: Ray setup + AABB bounds debug
2) VOL_0003B: Density probe + analytic sphere baseline
3) VOL_0003C: Transport sweep + exposure presets
4) VOL_0003D: Debug visualizations (raydir/density/heatmap)
5) VOL_0004: Temporal accumulation + jitter hooks
6) VOL_0005: Empty-space skipping (min–max/occupancy) + adaptive steps
7) DXR_0025: Re-enable DXR with real SBT after compute parity

Note: If `CORE_0003` (camera CBV(b0) integration across compute) isn’t complete, do it before VOL_0003A.

### What Likely Happened Technically
- Rays may not originate/directionally align with camera (incorrect inverse VP or world transforms), causing low-contrast integration.
- Marcher integrates absorption-only Beer–Lambert but with conservative parameters, producing low-frequency fog.
- The density field may be too sparse/complex relative to current step size, yielding weak silhouettes.

### How We Fix It Now (Compute-First)
- Prove raygen and AABB hit/miss with explicit debug output (VOL_0003A).
- Switch to a guaranteed high-contrast density (analytic sphere) and a density probe view (VOL_0003B).
- Sweep parameters and presets to land on strong visibility quickly (VOL_0003C).
- Use visual debug modes, including step heatmap, to diagnose and later validate skipping (VOL_0003D).

### PIX/Validation Targets
- Verify CBV(b0) bound for compute; inspect root params.
- Confirm UAV barrier after density writes; transition HDR to SRV only once before Composite.
- Check marching dispatch timings; ensure no early returns break the Close→Execute→Present→Signal lifecycle.

### MCP Server — Suggested Queries
- Barriers and states:
  - D3D12_RESOURCE_BARRIER
  - D3D12_RESOURCE_STATES
- Descriptor tables:
  - D3D12_DESCRIPTOR_RANGE / D3D12_DESCRIPTOR_RANGE1
- DXR (for later):
  - DispatchRays / D3D12_DISPATCH_RAYS_DESC

### Risks & Mitigations
- Root signature/PSO mismatches when adding CBVs/UAVs/SRVs → update root signatures and recreate PSOs accordingly.
- Over-aggressive parameters → clamp ranges and guard against underflow/overflow in exponential terms.
- Debug modes impacting performance → branch by early-mode selection; keep default path fast.

### Next Steps (Actionable)
- Implement VOL_0003A now; capture a PIX run confirming:
  - Correct per-pixel ray directions (RayDir mode) vary predictably with camera.
  - AABB bounds overlay distinguishes inside vs outside.
- If passes, proceed to VOL_0003B and verify probe silhouette of analytic sphere.

### References in Repo
- Claude issue context: `workflow_claude/0001_volume_rendering_visibility_issue.md`
- Companion and analysis docs: see above paths.
