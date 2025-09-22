# MCP Excerpts — VOL_0004 Temporal Accumulation + Jitter

- Companion to: `findings/COMPANION_GUIDE_VOL_0002_to_0005_2025_09_19.md`
- Job: `VOL_0004` — Temporal accumulation and jitter

## Queries and Excerpts

### search_all_sources: "D3D12_RESOURCE_BARRIER"
- D3D12_RESOURCE_BARRIER (structure): D3D12 structure [Source: Agility SDK]

### search_all_sources: "D3D12_RESOURCE_BARRIER_FLAGS"
- D3D12_RESOURCE_BARRIER_FLAGS (enum): D3D12 enum [Source: Agility SDK]

### search_all_sources: "D3D12_RESOURCE_STATES"
- D3D12_RESOURCE_STATES (enum): D3D12 enum [Source: Agility SDK]

## How to use for VOL_0004
- When blending current HDR with history, enforce UAV barriers and correct SRV/UAV transitions for both textures.
- Consider ping-pong history resources to simplify barriers; track state flags per texture.
