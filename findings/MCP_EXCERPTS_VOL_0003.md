# MCP Excerpts — VOL_0003 Compute Ray Marcher

- Companion to: `findings/COMPANION_GUIDE_VOL_0002_to_0005_2025_09_19.md`
- Job: `VOL_0003` — Compute ray marcher to HDR

## Queries and Excerpts

### search_all_sources: "D3D12_RESOURCE_BARRIER"
- D3D12_RESOURCE_BARRIER (structure): D3D12 structure [Source: Agility SDK]
- D3D12_RESOURCE_BARRIER_FLAGS (enum): D3D12 enum [Source: Agility SDK]
- D3D12_RESOURCE_BARRIER_TYPE (enum): D3D12 enum [Source: Agility SDK]

### search_all_sources: "D3D12_RESOURCE_BARRIER_TYPE"
- D3D12_RESOURCE_BARRIER_TYPE (enum): D3D12 enum [Source: Agility SDK]

### search_all_sources: "D3D12_RESOURCE_BARRIER_FLAGS"
- D3D12_RESOURCE_BARRIER_FLAGS (enum): D3D12 enum [Source: Agility SDK]

### search_all_sources: "D3D12_DESCRIPTOR_RANGE"
- D3D12_DESCRIPTOR_RANGE (structure): D3D12 structure [Source: Agility SDK]
- D3D12_DESCRIPTOR_RANGE1 (structure): D3D12 structure [Source: Agility SDK]
- D3D12_DESCRIPTOR_RANGE_FLAGS (enum): D3D12 enum [Source: Agility SDK]
- D3D12_DESCRIPTOR_RANGE_TYPE (enum): D3D12 enum [Source: Agility SDK]

## How to use for VOL_0003
- Use UAV barrier after raymarch write, then transition HDR UAV → SRV before composite.
- Keep descriptor tables consistent across compute PSOs; prefer descriptor tables over root descriptors for typed UAVs.
