# MCP Excerpts — RENDER_0001 Tonemap, Exposure, Gamma

- Companion to: `findings/COMPANION_GUIDE_VOL_0002_to_0005_2025_09_19.md`
- Job: `RENDER_0001` — Composite tonemapping controls

## Queries and Excerpts

### search_all_sources: "CreateShaderResourceView"
- No direct entry (function not cataloged in this DB). Use descriptor range types and SRV/UAV patterns.

### search_all_sources: "CreateUnorderedAccessView"
- No direct entry (function not cataloged in this DB). Use descriptor range types and SRV/UAV patterns.

### search_all_sources: "D3D12_DESCRIPTOR_RANGE"
- D3D12_DESCRIPTOR_RANGE (structure): D3D12 structure [Source: Agility SDK]
- D3D12_DESCRIPTOR_RANGE1 (structure): D3D12 structure [Source: Agility SDK]

## How to use for RENDER_0001
- Keep HDR as SRV in Composite; adjust exposure/gamma via CBV/roots constants.
- Validate descriptor table bindings for SRV and sampler; ensure viewport/scissor set before draw.
