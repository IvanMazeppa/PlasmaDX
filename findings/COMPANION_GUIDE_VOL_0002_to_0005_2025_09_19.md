# Companion Guide — VOL_0002 → VOL_0005 (2025-09-19)

## Execution Order (do not reorder)
1) VOL_0002: 3D density grid + debug slice
2) VOL_0003: Compute ray marcher (directional light + self-shadow)
3) VOL_0004: Temporal accumulation + jitter hooks
4) VOL_0005: Empty-space skipping
5) DXR_0025: Re-enable DXR once compute parity achieved

## Rationale
- We already have reliable HDR + composite + compute pipeline. Building density and a compute marcher first gives immediate visuals and measurable progress without DXR complexity.

## Validation per step
- VOL_0002: PIX shows density UAV writes; debug slice visible; log heartbeat every ~60 frames.
- VOL_0003: Visible volumetric lighting and shadowing; controls affect image; no device removal.
- VOL_0004: Image stabilizes over time when accumulation is on; history reset works.
- VOL_0005: Average steps per pixel drops; visual diffs minimal when skipping is enabled.

## Notes
- Keep DXR disabled via PLASMADX_DISABLE_DXR=1 until DXR_0025.
- Ensure proper UAV→SRV transitions and UAV barriers before sampling or composite.
- Use presets (96/128/192/256) to manage perf during bring-up.
 
## MCP Reading Checklist (add to your workflow)
- VOL_0003 (Compute Ray Marcher):
  - search_all_sources: "D3D12_RESOURCE_BARRIER"
  - search_all_sources: "D3D12_DESCRIPTOR_RANGE"
  - search_all_sources: "D3D12_RESOURCE_BARRIER_TYPE"
  - dx12_quick_reference: ""

- VOL_0004 (Temporal Accumulation + Jitter):
  - search_all_sources: "D3D12_RESOURCE_BARRIER"
  - search_all_sources: "D3D12_RESOURCE_BARRIER_FLAGS"
  - dx12_quick_reference: ""

- VOL_0005 (Empty-Space Skipping):
  - search_all_sources: "D3D12_DESCRIPTOR_RANGE1"
  - search_all_sources: "D3D12_DESCRIPTOR_RANGE_FLAGS"
  - dx12_quick_reference: ""

- DXR_0025 (DXR Parity & SBT Bring-up):
  - search_all_sources: "D3D12_DISPATCH_RAYS_DESC"
  - search_all_sources: "DispatchRays"
  - dx12_quick_reference: ""

- CORE_0003 (Camera + CBV Binding):
  - search_all_sources: "CreateConstantBufferView"
  - search_all_sources: "D3D12_DESCRIPTOR_RANGE"
  - dx12_quick_reference: ""

- RENDER_0001 (Tonemap/Exposure/Gamma):
  - search_all_sources: "CreateShaderResourceView"
  - search_all_sources: "CreateUnorderedAccessView"
  - dx12_quick_reference: ""

Notes:
- If any of the above queries return no results, use the nearby enums/structs listed and cross-reference with MSDN as needed; the MCP database emphasizes DXR, barriers, and descriptor structures.
