# MCP Excerpts — DXR_0025 Re-enable DXR After Compute Parity

- Companion to: `findings/COMPANION_GUIDE_VOL_0002_to_0005_2025_09_19.md`
- Job: `DXR_0025` — Enable DXR with real SBT and matching HDR path

## Queries and Excerpts

### search_all_sources: "D3D12_DISPATCH_RAYS_DESC"
- D3D12_DISPATCH_RAYS_DESC (structure): Descriptor for ray dispatch including shader binding table [Source: DXR]
- D3D12_DISPATCH_RAYS_DESC (structure): Shader binding table configuration for ray dispatch [Source: dxr_official]

### search_all_sources: "DispatchRays"
- DispatchRays (method): Dispatches rays for ray tracing [Source: DXR]
- DispatchRays (method): Launches raytracing work on GPU [Source: dxr_official]
- DispatchRaysDimensions (hlsl_intrinsic): Gets total dispatch dimensions [Tier: Tier 1.0] [SM: SM 6.3]
- DispatchRaysIndex (hlsl_intrinsic): Gets current dispatch ray indices (x,y,z) [Tier: Tier 1.0] [SM: SM 6.3]

## How to use for DXR_0025
- Fill SBT GPU addresses and sizes into D3D12_DISPATCH_RAYS_DESC; verify non-null in logs.
- Keep global root signature using descriptor tables for typed UAVs/SRVs; avoid root UAV descriptors.
