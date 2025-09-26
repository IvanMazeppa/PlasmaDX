# Companion Guide — VOL_0003A → VOL_0003D (Visibility Bring‑Up)

## Execution Order (strict)
1) VOL_0003A: Ray setup + volume AABB bounds debug
2) VOL_0003B: Density probe + analytic sphere baseline
3) VOL_0003C: Transport parameter sweep + exposure presets
4) VOL_0003D: Debug visualizations (raydir, density sample, step heatmap)
5) Resume baseline: VOL_0004 → VOL_0005 → DXR_0025

## Rationale
- Validate camera ray generation and AABB first; without correct rays, no amount of parameter tuning will help.
- Establish a known-good density source (analytic sphere) to remove dependence on procedural fields.
- Provide sweeps/presets to quickly reach visible structure and to stabilize tuning.
- Add visual modes to diagnose remaining issues and to prepare for empty-space skipping validation.

## MCP Reading Checklist
- VOL_0003A, 0003D (barriers, states, tables):
  - search_all_sources: "D3D12_RESOURCE_BARRIER"
  - search_all_sources: "D3D12_RESOURCE_STATES"
  - search_all_sources: "D3D12_DESCRIPTOR_RANGE"
- VOL_0003B (descriptor variants):
  - search_all_sources: "D3D12_DESCRIPTOR_RANGE1"
- VOL_0003C (general state awareness):
  - search_all_sources: "D3D12_RESOURCE_STATES"

## Validation & PIX Targets
- Confirm CBV(b0) contains correct view/projection and camera position; inspect root params in PIX.
- Verify HDR UAV → SRV transitions only once per frame before Composite; see barriers timeline.
- For analytic sphere, check UAV barrier before reading in marcher; ensure SRV sampling states.

## Notes
- Keep PLASMADX_DISABLE_DXR=1 until DXR_0025.
- Maintain frame lifecycle: single Close → Execute → Present → Signal; avoid early returns.
- Log parameter changes once per second to avoid spam; bind HUD only in Composite or CPU-side log.
