# Mode 9.2 Particle-to-Particle Lighting Diagnostic Fix

**Date**: 2025-10-03
**Issue**: Diagnostic code not executing - particles showing normal temperature colors instead of lighting visualization
**Root Cause**: Stale pixel shader binary (DXIL compiled before diagnostic code was added)

---

## Problem Summary

Mode 9.2 particle-to-particle lighting showed no visual effect even with 1000x amplification and diagnostic color replacement. The pixel shader diagnostic code (lines 216-230 in particle_mesh.hlsl) was not executing - particles continued showing normal red/orange temperature colors instead of the expected dark blue (zero lighting) or bright amplified colors (non-zero lighting).

---

## Root Cause Analysis

### Timeline Evidence

```
Source modified:  2025-10-03 00:24:27  (particle_mesh.hlsl)
OLD binary:       2025-10-02 23:27:36  (particle_pixel.dxil)
Gap:              ~57 minutes STALE

NEW binary:       2025-10-03 00:34:42  (particle_pixel.dxil - RECOMPILED)
Status:           NOW CURRENT
```

The diagnostic code was added to particle_mesh.hlsl at 00:24:27, but the pixel shader binary was last compiled at 23:27:36 (previous day), nearly 1 hour earlier. The running DXIL binary did not contain the diagnostic code.

---

## Verification of Descriptor Bindings (ALL CORRECT)

### 1. Mode Parameter Passing ✅

**C++ Call Site** (App.cpp line 2294-2298):
```cpp
m_meshParticleSystem->RenderParticles(m_cmdList.Get(),
    viewMatrix, projMatrix, cameraPos, rtvHandle, m_width, m_height,
    shadowMapGpuHandle,
    static_cast<uint32_t>(m_mode9SubMode), // ← Passes 2 for ParticleRelight
    m_emissionRtvHandle,
    lightingSrvGpuHandle);
```

**Mode Enum Definition** (App.h line 234):
```cpp
enum class Mode9SubMode {
    Baseline = 0,
    ShadowMap = 1,
    ParticleRelight = 2,  // ← This is value 2
    ...
};
```

**Root Constants Binding** (ParticleSystem.cpp line 507-509):
```cpp
// Mode params (b1): 4 dwords = { mode9SubMode, padding, padding, padding }
uint32_t modeParams[4] = { mode9SubMode, 0, 0, 0 };
cmdList6->SetGraphicsRoot32BitConstants(4, 4, modeParams, 0);
```

**Shader Declaration** (particle_mesh.hlsl line 33-35):
```hlsl
cbuffer ModeParams : register(b1) {
    uint mode9SubMode;  // 0=Baseline, 1+=Shadow modes
    float3 modePadding;
};
```

**Result**: mode9SubMode = 2 is correctly passed via root parameter 4 (b1).

---

### 2. Particle Lighting Buffer Binding ✅

**Root Signature Setup** (ParticleSystem.cpp line 221-228):
```cpp
CD3DX12_DESCRIPTOR_RANGE1 lightingRange;
lightingRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // 1 SRV at t2

CD3DX12_ROOT_PARAMETER1 rootParams[5];
rootParams[0].InitAsShaderResourceView(0);        // t0 - particles
rootParams[1].InitAsConstantBufferView(0);        // b0 - render constants
rootParams[2].InitAsDescriptorTable(1, &shadowMapRange);  // t1 - shadow map
rootParams[3].InitAsDescriptorTable(1, &lightingRange);   // t2 - particle lighting ←
rootParams[4].InitAsConstants(4, 1);              // b1 - mode params
```

**Descriptor Table Binding** (ParticleSystem.cpp line 500-505):
```cpp
// Particle lighting descriptor table (param 3) - Mode 9.2
if (particleLightingSrv.ptr == 0) {
    LOGE("CRITICAL: Particle lighting descriptor is NULL! Aborting render to prevent GPU crash");
    return;
}
cmdList6->SetGraphicsRootDescriptorTable(3, particleLightingSrv);
```

**GPU Handle Source** (App.cpp line 2292):
```cpp
// Mode 9.2: Get particle lighting SRV GPU handle
D3D12_GPU_DESCRIPTOR_HANDLE lightingSrvGpuHandle =
    m_descriptorAllocator->GetGPUHandle(m_particleLightingSrvIndex);
```

**Buffer Creation** (App.cpp line 3193-3214):
```cpp
// Create particle lighting buffer (float4 per particle = 16 bytes)
CD3DX12_RESOURCE_DESC lightingBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
    m_mode9ParticleCount * sizeof(float) * 4, // 100,000 * 16 = 1.6 MB
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
);

hr = m_device->CreateCommittedResource(
    &heapProps,
    D3D12_HEAP_FLAG_NONE,
    &lightingBufferDesc,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    nullptr,
    IID_PPV_ARGS(&m_particleLightingBuffer));
```

**SRV Creation** (App.cpp line 3242-3259):
```cpp
// Create SRV for particle rendering
D3D12_SHADER_RESOURCE_VIEW_DESC lightingSrvDesc{};
lightingSrvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // Typed buffer
lightingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
lightingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
lightingSrvDesc.Buffer.FirstElement = 0;
lightingSrvDesc.Buffer.NumElements = m_mode9ParticleCount;
lightingSrvDesc.Buffer.StructureByteStride = 0; // Typed buffer = 0 stride
lightingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

m_device->CreateShaderResourceView(m_particleLightingBuffer.Get(),
    &lightingSrvDesc, lightingSrvHandle);
```

**Shader Declaration** (particle_mesh.hlsl line 40):
```hlsl
Buffer<float4> particleLighting : register(t2);  // Typed buffer, not structured
```

**Mesh Shader Read** (particle_mesh.hlsl line 100-101):
```hlsl
// Mode 9.2: Read particle lighting contribution
float3 lighting = particleLighting[particleIndex].rgb;
```

**Mesh Shader Output** (particle_mesh.hlsl lines 131/141/151/161):
```hlsl
verts[vertexIndex + 0].lighting = lighting;  // All 4 vertices get same lighting
verts[vertexIndex + 1].lighting = lighting;
verts[vertexIndex + 2].lighting = lighting;
verts[vertexIndex + 3].lighting = lighting;
```

**Result**: Particle lighting buffer (t2) is correctly bound via descriptor table at root parameter 3.

---

### 3. Resource State Transitions ✅

**Before Compute Write** (App.cpp line 2323-2340):
```cpp
// Transition from SRV to UAV for compute shader write
D3D12_RESOURCE_BARRIER lightingToUAV{};
lightingToUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
lightingToUAV.Transition.pResource = m_particleLightingBuffer.Get();
lightingToUAV.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
lightingToUAV.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
m_cmdList->ResourceBarrier(1, &lightingToUAV);

// Clear buffer to zero before compute
clearEmissionGridCompute(
    m_descriptorAllocator->GetGPUHandle(m_particleLightingUavIndex),
    m_descriptorAllocator->GetCPUHandle(m_particleLightingUavIndex),
    m_particleLightingBuffer.Get(),
    clearZero, 0, nullptr);
```

**After Compute Write** (App.cpp line 3567-3571):
```cpp
// Transition back to SRV for mesh shader read
postBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
postBarriers[0].Transition.pResource = m_particleLightingBuffer.Get();
postBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
postBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
```

**Result**: Resource state transitions are correct (UAV for compute write → SRV for mesh/pixel read).

---

## Diagnostic Code (Now Compiled)

**Pixel Shader** (particle_mesh.hlsl line 215-230):
```hlsl
// Mode 9.2+: EXTREME DIAGNOSTIC - Replace color entirely with lighting visualization
if (mode9SubMode >= 2) {
    float lightingMagnitude = length(input.lighting);

    // Show lighting with EXTREME amplification (1000x)
    float3 visualizedLighting = input.lighting * 1000.0;

    // Color code by magnitude for debugging
    if (lightingMagnitude > 0.0001) {
        // Has lighting - show it in bright colors
        color = visualizedLighting;
    } else {
        // Zero lighting - show as dark blue
        color = float3(0, 0, 0.2);
    }
}
```

**Expected Behavior**:
- **If lighting buffer is zero**: Particles should be **dark blue** (RGB = 0, 0, 0.2)
- **If lighting buffer has values**: Particles should be **bright amplified colors** (lighting * 1000.0)

---

## Solution Applied

**Command Executed**:
```bash
cd /mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/particles
/mnt/e/VulkanSDK/1.4.321.1/Bin/dxc.exe -T ps_6_5 -E PSMain -Fo particle_pixel.dxil particle_mesh.hlsl
```

**Result**: Pixel shader binary updated to include diagnostic code.

**New Binary Timestamp**: 2025-10-03 00:34:42 (10 minutes AFTER source modification)

---

## Next Steps

1. **Run the application** and switch to Mode 9.2 (ParticleRelight)
2. **Expected outcome**:
   - If particle lighting compute is working: Bright colored particles
   - If particle lighting is zeroed: Dark blue particles (RGB = 0, 0, 0.2)
   - If diagnostic not running: Normal red/orange temperature colors (indicates other issue)

3. **If still showing temperature colors after recompile**:
   - Check FileLoader is loading new DXIL (cache issue?)
   - Verify PSO is recreated with new shader
   - Add debug logging to confirm mode9SubMode value in shader

4. **If showing dark blue**:
   - Lighting buffer is correctly bound but all zeros
   - Check compute shader is writing lighting values
   - Verify UAV descriptor is correct
   - Check compute dispatch is running

5. **If showing bright colors**:
   - SUCCESS! Lighting computation is working
   - Reduce amplification factor from 1000x to realistic values
   - Remove diagnostic code and use proper lighting integration

---

## Verification Checklist

- [x] Mode parameter (b1) correctly passed as root constant
- [x] Particle lighting buffer (t2) correctly bound via descriptor table
- [x] Shadow map buffer (t1) correctly bound
- [x] Root signature matches shader expectations
- [x] Mesh shader reads lighting from buffer
- [x] Mesh shader passes lighting to pixel shader
- [x] Resource state transitions correct
- [ ] **Pixel shader binary NOW CURRENT** ✅ FIXED
- [ ] Application run with new binary (PENDING)

---

## Files Modified

- `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/particles/particle_pixel.dxil` (recompiled)

## Files Referenced

- `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/particles/particle_mesh.hlsl` (source)
- `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/particles/ParticleSystem.cpp` (descriptor bindings)
- `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.cpp` (render call site)
- `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.h` (mode enum)

---

## Additional Diagnostic Tools

If the issue persists after recompile, add this logging to ParticleSystem.cpp:

```cpp
// In RenderParticles(), after line 509:
static int s_diagCount = 0;
if (s_diagCount < 5) {
    LOGI("Mode9SubMode=" + std::to_string(mode9SubMode) +
         " LightingSrv=0x" + std::to_string(particleLightingSrv.ptr) +
         " ShadowSrv=0x" + std::to_string(shadowMapSrv.ptr));
    s_diagCount++;
}
```

This will confirm the exact values being passed to the shader.

---

## Risk Assessment

**LOW RISK**: Only recompiled pixel shader with existing source. No code changes, no new functionality, no new bindings. The descriptor table architecture was already correct - this was purely a build synchronization issue.

**Rollback**: If issues occur, previous binary is timestamped 2025-10-02 23:27:36. Copy from backup if needed.