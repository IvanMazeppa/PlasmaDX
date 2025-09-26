# MCP Excerpts — CORE_0003 Camera + CBV Binding

- Companion to: `findings/COMPANION_GUIDE_VOL_0002_to_0005_2025_09_19.md`
- Job: `CORE_0003` — Integrate camera input and CBV(b0)

## Queries and Excerpts

### search_all_sources: "D3D12_DESCRIPTOR_RANGE"
- D3D12_DESCRIPTOR_RANGE (structure): D3D12 structure [Source: Agility SDK]
- D3D12_DESCRIPTOR_RANGE1 (structure): D3D12 structure [Source: Agility SDK]
- D3D12_DESCRIPTOR_RANGE_FLAGS (enum): D3D12 enum [Source: Agility SDK]
- D3D12_DESCRIPTOR_RANGE_TYPE (enum): D3D12 enum [Source: Agility SDK]

### search_all_sources: "CreateConstantBufferView"
- No direct entry (use MSDN for function details). Use descriptor range references above for binding patterns.

## How to use for CORE_0003
- Ensure CBV(b0) is present in compute root signatures; update and bind per frame.
- Respect 256-byte alignment for constant buffers; set via SetComputeRootConstantBufferView.
