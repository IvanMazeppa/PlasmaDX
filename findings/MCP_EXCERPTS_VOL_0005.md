# MCP Excerpts — VOL_0005 Empty-Space Skipping

- Companion to: `findings/COMPANION_GUIDE_VOL_0002_to_0005_2025_09_19.md`
- Job: `VOL_0005` — Empty-space skipping and adaptive steps

## Queries and Excerpts

### search_all_sources: "D3D12_DESCRIPTOR_RANGE1"
- D3D12_DESCRIPTOR_RANGE1 (structure): D3D12 structure [Source: Agility SDK]

### search_all_sources: "D3D12_DESCRIPTOR_RANGE_FLAGS"
- D3D12_DESCRIPTOR_RANGE_FLAGS (enum): D3D12 enum [Source: Agility SDK]

## How to use for VOL_0005
- Build occupancy/min-max textures via compute and bind SRV/UAV with descriptor tables; prefer RANGE1 for flags.
- Ensure sampling coarse levels uses SRV state; insert UAV barrier after building the hierarchy before marching.
