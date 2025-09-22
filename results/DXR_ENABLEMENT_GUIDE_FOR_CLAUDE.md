### DXR Enablement Guide for Claude

Purpose
- Help Claude bring up DXR reliably in PlasmaDX and similar DX12 projects.
- Includes exact steps, validation points, pitfalls, and reference snippets (copied inline vs MCP).

Quick Success Path
1) Root signature for DXR global:
   - Params:
     - 0: SRV (t0) = TLAS
     - 1: Descriptor table with 1 UAV (u0) = HDR output
2) Pipeline state object (raytracing state object):
   - Add one DXIL library exporting: RayGen, Miss, ClosestHit
   - Add a hit group referencing ClosestHit
   - Shader config: payload size = 16 bytes (float4), attribute size = 8 bytes (float2)
   - Pipeline config: max recursion depth = 1
3) Shader Binding Table (SBT):
   - Table sections must be aligned to 64 bytes. Each record must be aligned to 32 bytes.
   - Minimal SBT layout:
     - RayGen table: 1 record
     - Miss table: 1 record
     - HitGroup table: 1 record
   - Records contain at least the 32-byte shader identifier; follow with local root args if any.
4) Resources and state:
   - Create HDR texture: R16G16B16A16_FLOAT, UAV allowed.
   - Before DispatchRays, ensure UAV ordering barrier on HDR (UAV barrier is sufficient if staying in UAV).
   - Bind the descriptor heap that holds your UAV before setting the root descriptor table.
5) DispatchRays setup:
   - Width/Height = swapchain size (or HDR size)
   - Address ranges must point to the SBT GPU buffer sections; StartAddress != 0.

Debug Checklist
- If DispatchRays succeeds but image is blank:
  - Force a write in raygen: `g_output[DispatchRaysIndex().xy] = float4(1,0,1,1);`
  - Immediately after DispatchRays, Clear UAV to green; if green shows, composite path is correct and raygen write is the issue.
  - Log SBT StartAddress/SizeInBytes for RayGen/Miss/HitGroup; zero indicates bad SBT build.
  - Ensure the DXR global root signature is actually set on the command list used for DispatchRays.
  - Confirm descriptor heap is bound before setting root tables.

Reference Snippets (copied inline)
- RayGen minimal:
```hlsl
// Global
RaytracingAccelerationStructure g_scene : register(t0);
RWTexture2D<float4> g_output : register(u0);

struct RayPayload { float4 color; };

[shader("raygeneration")]
void RayGen() {
  uint2 index = DispatchRaysIndex().xy;
  uint2 dims  = DispatchRaysDimensions().xy;
  float2 uv = float2(index)/float2(dims);
  // Procedural test: lit unit box
  float aspect = (float)dims.x / (float)dims.y;
  float2 ndc = uv*2.0-1.0; ndc.y = -ndc.y;
  float3 ro = float3(0,0,-3);
  float3 rd = normalize(float3(ndc.x*aspect, ndc.y, 1.0));
  float3 bmin=float3(-1,-1,-1), bmax=float3(1,1,1);
  float3 invD=1.0/rd; float3 t0=(bmin-ro)*invD, t1=(bmax-ro)*invD;
  float3 tmin=min(t0,t1), tmax=max(t0,t1);
  float tN=max(max(tmin.x,tmin.y),tmin.z), tF=min(min(tmax.x,tmax.y),tmax.z);
  float3 col=0;
  if (tF>=max(tN,0.0)) {
    float3 p=ro+rd*max(tN,0.0), n=0, L=normalize(float3(0.3,0.8,0.2));
    if (abs(p.x-(-1))<1e-3) n=float3(-1,0,0); else if (abs(p.x-1)<1e-3) n=float3(1,0,0);
    else if (abs(p.y-(-1))<1e-3) n=float3(0,-1,0); else if (abs(p.y-1)<1e-3) n=float3(0,1,0);
    else if (abs(p.z-(-1))<1e-3) n=float3(0,0,-1); else n=float3(0,0,1);
    float diff=max(0.0,dot(n,L)); col=diff*float3(1.0,0.95,0.85)*(0.7+0.3*abs(n));
  }
  g_output[index]=float4(col,1);
}
```

- Root signature concept (C++):
```cpp
D3D12_DESCRIPTOR_RANGE uavRange{}; uavRange.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
uavRange.NumDescriptors=1; uavRange.BaseShaderRegister=0; uavRange.RegisterSpace=0;
D3D12_ROOT_PARAMETER params[2]{};
params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; // t0 TLAS
params[0].Descriptor.ShaderRegister = 0; params[0].Descriptor.RegisterSpace = 0;
params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
params[1].DescriptorTable.NumDescriptorRanges = 1; params[1].DescriptorTable.pDescriptorRanges = &uavRange;
```

- SBT alignment facts:
  - Shader table (each section) must be aligned to 64 bytes.
  - Each shader record must be aligned to 32 bytes.
  - Store StartAddress and SizeInBytes for each section; never zero.

- DispatchRays usage (C++):
```cpp
auto desc = m_sbt->GetDispatchRaysDesc(width,height);
// Validate SBT addresses
assert(desc.RayGenerationShaderRecord.StartAddress != 0);
cmdList->SetPipelineState1(pso);
cmdList->SetComputeRootSignature(globalRootSig);
cmdList->SetComputeRootShaderResourceView(0, tlas->GetGPUVirtualAddress());
cmdList->SetDescriptorHeaps(1, &srvUavHeap);
cmdList->SetComputeRootDescriptorTable(1, hdrUavHandle);
// Optional ordering on UAV before DXR write
cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(hdrTexture));
cmdList->DispatchRays(&desc);
```

Common Pitfalls
- Using a compute root signature with DXR and forgetting to set it again before DispatchRays.
- SBT built in upload memory but never mapped/written identifiers.
- Forgetting to bind the descriptor heap before setting the root descriptor table.
- HDR resource in SRV state during DXR write; keep it in UAV or insert proper transition/ordering.

PlasmaDX-Specific Notes
- DXR is enabled by default in the HDR path (override with `PLASMADX_DISABLE_DXR=1`).
- HDR UAV index is allocated via our DescriptorHeap allocator; we bind via SetComputeRootDescriptorTable(1, handle).
- A procedural lit-box raygen is in `shaders/dxr/raytracing_lib.hlsl` to visually confirm DXR writes.
- When moving to volumetric RT, we’ll pass camera matrices and sample the density volume via SRV in DXR.

Next Actions for Claude
- If DXR output ever goes blank again:
  1) Replace raygen body with a forced magenta write (see Debug Checklist).
  2) If magenta does not appear, immediately clear UAV after DispatchRays. Green means composite path is fine.
  3) Log and verify SBT addresses and descriptor heap bindings.
- When ready, I can expose camera constants to raygen and integrate volume SRV + shadow rays.
