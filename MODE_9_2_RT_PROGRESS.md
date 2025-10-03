# Mode 9.2 Ray Traced Particle Lighting - Implementation Progress

## What's Been Completed (Phase 1 - Partial)

### 1. ASBuilder Per-Particle BLAS Support ✅
**Files Modified:**
- [src/dxr/ASBuilder.h:23-29](src/dxr/ASBuilder.h#L23-L29) - Added `CreatePerParticleBLAS()` declaration
- [src/dxr/ASBuilder.h:41-44](src/dxr/ASBuilder.h#L41-L44) - Added `BuildPerParticleBLAS()` declaration
- [src/dxr/ASBuilder.h:62-69](src/dxr/ASBuilder.h#L62-L69) - Added member variables for per-particle geometry
- [src/dxr/ASBuilder.cpp:258-364](src/dxr/ASBuilder.cpp#L258-L364) - Implemented `CreatePerParticleBLAS()`
- [src/dxr/ASBuilder.cpp:503-532](src/dxr/ASBuilder.cpp#L503-L532) - Implemented `BuildPerParticleBLAS()`

**Key Features:**
- Creates AABB buffer for 100,000 individual particles (GPU-writable, DEFAULT heap)
- Uses `ALLOW_UPDATE` flag for dynamic BLAS rebuilds every frame
- Uses `PREFER_FAST_BUILD` for sub-millisecond updates
- BLAS prebuild info calculates proper buffer sizes automatically

**CRITICAL DIFFERENCE from existing shadow map BLAS:**
- **Shadow map BLAS**: 1 conservative AABB for ALL particles
- **RT lighting BLAS**: 100,000 individual AABBs (one per particle)

### 2. AABB Generation Compute Shader ✅
**File Created:**
- [shaders/dxr/generate_particle_aabbs.hlsl](shaders/dxr/generate_particle_aabbs.hlsl) - Compiled to `.dxil`

**What It Does:**
- Reads particle positions from particle buffer
- Generates D3D12_RAYTRACING_AABB structs (6 floats: minXYZ, maxXYZ)
- Writes to GPU buffer (will be used by BLAS build)
- Runs every frame before BLAS update (256 threads/group)

**Shader Model:** CS 6.5

### 3. Ray Traced Lighting Compute Shader ✅
**File Created:**
- [shaders/dxr/particle_raytraced_lighting_cs.hlsl](shaders/dxr/particle_raytraced_lighting_cs.hlsl) - Compiled to `.dxil`

**What It Does:**
- Uses **RayQuery (DXR 1.1 inline ray tracing)** - GENUINE ray tracing
- Each particle casts 8 rays in hemisphere (Fibonacci sampling)
- Traces rays against per-particle BLAS
- Accumulates lighting from hit particles (temperature-based emission)
- Outputs float3 lighting contribution per particle
- Uses inverse square distance falloff

**Key Code:**
```hlsl
RaytracingAccelerationStructure g_particleBVH : register(t1);
RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
query.TraceRayInline(g_particleBVH, RAY_FLAG_NONE, 0xFF, ray);
query.Proceed();

if (query.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
    uint hitParticleIdx = query.CommittedPrimitiveIndex();
    // ... calculate lighting from emitter particle
}
```

**Shader Model:** CS 6.5 (RayQuery support)

### 4. Intersection Shader ✅
**File Created:**
- [shaders/dxr/particle_intersection.hlsl](shaders/dxr/particle_intersection.hlsl) - Compiled to `.dxil`

**What It Does:**
- Procedural sphere-ray intersection for AABB primitives
- Runs when ray hits particle AABB bounding box
- Performs analytical intersection test (sphere equation)
- Reports hit distance to DXR runtime if intersection found

**Shader Model:** Shader Library 6.3 (intersection shader support)

**CRITICAL NOTE:** This shader is NOT needed for RayQuery! RayQuery in compute shaders uses built-in AABB intersection. This shader is for future TraceRay() pipeline if we switch to ray generation shaders.

### 5. App.h Member Variables ✅
**File Modified:**
- [src/core/App.h:352-369](src/core/App.h#L352-L369) - Added RT lighting resources

**Added:**
```cpp
// Method declarations
bool createPerParticleBLASResources();
bool createRTLightingPipeline();
void updateParticleAABBs();
void computeRTLighting();

// BLAS resources
Microsoft::WRL::ComPtr<ID3D12Resource> m_perParticleBLAS;
Microsoft::WRL::ComPtr<ID3D12Resource> m_perParticleBLASScratch;
Microsoft::WRL::ComPtr<ID3D12Resource> m_particleAABBBuffer;
UINT m_particleBVHSrvIndex = UINT_MAX;

// AABB generation pipeline
Microsoft::WRL::ComPtr<ID3D12PipelineState> m_aabbGenPSO;
Microsoft::WRL::ComPtr<ID3D12RootSignature> m_aabbGenRootSig;
Microsoft::WRL::ComPtr<ID3D12Resource> m_aabbConstantsBuffer;

// RT lighting pipeline
Microsoft::WRL::ComPtr<ID3D12PipelineState> m_rtLightingPSO;
Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rtLightingRootSig;
Microsoft::WRL::ComPtr<ID3D12Resource> m_rtLightingConstantsBuffer;
```

---

## What Still Needs Implementation

### Phase 1 Remaining: App.cpp Integration

#### Task 1: Create Per-Particle BLAS Resources (`createPerParticleBLASResources()`)
**Location:** Add to App.cpp around line 3100 (near `createEmissionGridResources()`)

**Implementation:**
```cpp
bool App::createPerParticleBLASResources() {
    LOGI("Creating per-particle BLAS resources for RT lighting...");

    uint32_t particleCount = m_particleSystem->GetParticleCount();
    float particleRadius = m_particleSystem->GetParticleSize(); // Use particle render size

    // Create per-particle BLAS (100K AABBs)
    if (!m_asBuilder->CreatePerParticleBLAS(
        m_perParticleBLAS,
        m_perParticleBLASScratch,
        m_particleAABBBuffer,
        particleCount,
        particleRadius)) {
        LOGE("Failed to create per-particle BLAS");
        return false;
    }

    // Create SRV for BLAS (acceleration structure SRV)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_perParticleBLAS->GetGPUVirtualAddress();

    m_particleBVHSrvIndex = allocateDescriptor(); // Allocate from descriptor heap
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = getCPUDescriptorHandle(m_particleBVHSrvIndex);
    m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);

    LOGI("Per-particle BLAS created with " + std::to_string(particleCount) + " AABBs");
    return true;
}
```

**Call Site:** In `Initialize()` after `createEmissionGridResources()` (around line 1675)

#### Task 2: Create AABB Generation Pipeline (`createRTLightingPipeline()` - part 1)
**Location:** Add to App.cpp around line 3800 (near `createLightingComputePipelines()`)

**Implementation:**
```cpp
bool App::createRTLightingPipeline() {
    LOGI("Creating RT lighting compute pipelines...");

    // 1. Create AABB generation pipeline
    // Root signature: CBV(b0), SRV(t0)=particles, UAV(u0)=aabbBuffer
    CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0

    CD3DX12_ROOT_PARAMETER1 rootParams[2];
    rootParams[0].InitAsConstantBufferView(0); // b0
    rootParams[1].InitAsDescriptorTable(2, ranges); // t0, u0

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = 2;
    rootSigDesc.Desc_1_1.pParameters = rootParams;

    // Create root signature (similar to existing grid shaders)
    // ... serialize and create m_aabbGenRootSig

    // Load AABB generation shader
    std::ifstream aabbShaderFile("shaders/dxr/generate_particle_aabbs.dxil", std::ios::binary);
    // ... create m_aabbGenPSO

    // Create constants buffer
    D3D12_HEAP_PROPERTIES uploadProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(256); // 256 bytes for CBV
    m_device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_aabbConstantsBuffer));

    LOGI("AABB generation pipeline created");
    return true;
}
```

#### Task 3: Create RT Lighting Pipeline (`createRTLightingPipeline()` - part 2)
**Add to same function:**

```cpp
    // 2. Create RT lighting pipeline
    // Root signature: CBV(b0), SRV(t0)=particles, SRV(t1)=BLAS, UAV(u0)=lighting
    CD3DX12_DESCRIPTOR_RANGE1 rtRanges[3];
    rtRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 particles
    rtRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1 BLAS
    rtRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0 lighting output

    // Similar root signature creation for RT lighting shader
    // ... create m_rtLightingRootSig and m_rtLightingPSO

    LOGI("RT lighting pipeline created");
    return true;
}
```

**Call Site:** In `Initialize()` after `createLightingComputePipelines()` (around line 1683)

#### Task 4: Update Particle AABBs Every Frame (`updateParticleAABBs()`)
**Location:** Add to App.cpp (new method)

**Implementation:**
```cpp
void App::updateParticleAABBs() {
    // Dispatch AABB generation compute shader
    m_cmdList->SetPipelineState(m_aabbGenPSO.Get());
    m_cmdList->SetComputeRootSignature(m_aabbGenRootSig.Get());

    // Bind constants (particleCount, particleRadius)
    struct AABBConstants {
        uint32_t particleCount;
        float particleRadius;
        float2 padding;
    } constants = {
        m_particleSystem->GetParticleCount(),
        m_particleSystem->GetParticleSize(),
        {0, 0}
    };

    // Map and write constants
    void* pConstantsData;
    m_aabbConstantsBuffer->Map(0, nullptr, &pConstantsData);
    memcpy(pConstantsData, &constants, sizeof(constants));
    m_aabbConstantsBuffer->Unmap(0, nullptr);

    m_cmdList->SetComputeRootConstantBufferView(0, m_aabbConstantsBuffer->GetGPUVirtualAddress());

    // Bind particle SRV and AABB UAV (descriptor table)
    // ... set descriptor table for t0 and u0

    // Dispatch (100,000 particles / 256 threads = 391 groups)
    uint32_t numGroups = (m_particleSystem->GetParticleCount() + 255) / 256;
    m_cmdList->Dispatch(numGroups, 1, 1);

    // UAV barrier for AABB buffer
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_particleAABBBuffer.Get();
    m_cmdList->ResourceBarrier(1, &uavBarrier);

    // Rebuild BLAS (ALLOW_UPDATE flag makes this fast ~0.3ms)
    m_asBuilder->BuildPerParticleBLAS(m_cmdList.Get(), m_perParticleBLAS.Get(), m_perParticleBLASScratch.Get());
}
```

**Call Site:** In `renderFrame()` AFTER particle physics, BEFORE RT lighting (around line 2310)

#### Task 5: Compute RT Lighting (`computeRTLighting()`)
**Location:** Add to App.cpp (new method)

**Implementation:**
```cpp
void App::computeRTLighting() {
    // Dispatch RT lighting compute shader
    m_cmdList->SetPipelineState(m_rtLightingPSO.Get());
    m_cmdList->SetComputeRootSignature(m_rtLightingRootSig.Get());

    // Bind constants (particleCount, raysPerParticle=8, maxDistance=20, intensity=1.0)
    struct RTLightingConstants {
        uint32_t particleCount;
        uint32_t raysPerParticle;
        float maxLightingDistance;
        float lightingIntensity;
    } constants = {
        m_particleSystem->GetParticleCount(),
        8,      // Start with 8 rays for high quality
        20.0f,  // Max lighting distance
        1.0f    // Intensity multiplier
    };

    // Map and write constants
    // ... (similar to updateParticleAABBs)

    // Bind particle SRV (t0), BLAS SRV (t1), lighting UAV (u0)
    // ... set descriptor table

    // Dispatch (100,000 particles / 64 threads = 1563 groups)
    uint32_t numGroups = (m_particleSystem->GetParticleCount() + 63) / 64;
    m_cmdList->Dispatch(numGroups, 1, 1);

    // UAV barrier for lighting buffer
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_particleLightingBuffer.Get();
    m_cmdList->ResourceBarrier(1, &uavBarrier);
}
```

**Call Site:** In `renderFrame()` AFTER `updateParticleAABBs()`, BEFORE particle rendering (around line 2312)

**CRITICAL CHANGE:** The existing `computeParticleLighting()` for spatial grid should be replaced with `computeRTLighting()`.

---

## Integration Order (renderFrame)

**Current Order:**
1. Particle physics update
2. `computeEmissionGrid()` - SPATIAL GRID (to be removed)
3. `computeParticleLighting()` - SPATIAL GRID (to be removed)
4. Particle rendering

**New Order:**
1. Particle physics update
2. **`updateParticleAABBs()`** - Generate AABBs and rebuild BLAS
3. **`computeRTLighting()`** - Ray traced lighting
4. Particle rendering (with RT lighting buffer)

---

## Testing Strategy

### Phase 1 Testing: 10K Particles
**Change in ParticleSystem.h:**
```cpp
// Temporarily reduce particle count for testing
uint32_t particleCount = 10000; // Down from 100,000
```

**Expected Results:**
- BLAS creation successful (check logs for prebuild info)
- AABB gen dispatch: 10K / 256 = 39 groups
- RT lighting dispatch: 10K / 64 = 157 groups
- Performance: ~13-14ms frame time (60fps+)

**Validation:**
- PIX Graphics Debugger: Inspect BLAS structure
- Check AABB buffer contents (first 10 particles)
- Check lighting buffer (should have non-zero RGB values)

### Phase 2 Testing: 100K Particles
**Restore original particle count**

**Expected Results:**
- BLAS creation successful (larger prebuild size)
- AABB gen dispatch: 100K / 256 = 391 groups
- RT lighting dispatch: 100K / 64 = 1563 groups
- Performance: ~13-16ms frame time (60fps minimum)

**Performance Breakdown:**
- Particle physics: 2ms
- AABB update: 0.3ms
- BLAS rebuild: 0.3ms
- RT lighting: 4ms (800K rays)
- Particle rendering: 3ms
- Shadow maps: 2ms
- **Total: ~13ms (75fps)**

---

## Shader Execution Reordering (SER) - Phase 3

**When to Add:** After 100K particles at 60fps is validated

**Code Change in particle_raytraced_lighting_cs.hlsl:**
```hlsl
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint particleIdx = dispatchThreadID.x;
    if (particleIdx >= particleCount) return;

    Particle receiver = g_particles[particleIdx];

    // SHADER EXECUTION REORDERING: Group by temperature bucket
    uint tempBucket = uint(receiver.temperature / 5000.0); // 0-5 buckets (800K-26000K / 5000)
    ReorderThread(tempBucket, 2);

    // ... rest of lighting code
}
```

**Expected Speedup:** 40%+ on RTX 4060 Ti
- Before SER: 4ms RT lighting
- After SER: 2.4ms RT lighting
- **New total: ~11ms (90fps)**

**Check SER Support in App.cpp:**
```cpp
D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12));
if (options12.RelaxedFormatCastingSupported) {
    LOGI("Shader Execution Reordering (SER) supported");
} else {
    LOGW("SER not available - RT lighting will be slower");
}
```

---

## Files to Delete (After RT Working)

**Non-RT Spatial Grid Shaders:**
- ❌ `shaders/mode9/emission_grid_build.hlsl`
- ❌ `shaders/mode9/emission_grid_build.dxil`
- ❌ `shaders/mode9/particle_lighting.hlsl`
- ❌ `shaders/mode9/particle_lighting.dxil`
- ❌ `shaders/mode9/grid_clear.hlsl`
- ❌ `shaders/mode9/grid_clear.dxil`

**App.cpp Code to Remove:**
- Methods: `createEmissionGridResources()`, `createLightingComputePipelines()`, `computeEmissionGrid()`, `computeParticleLighting()`
- Member variables: All `m_emission*` and `m_gridClear*` variables (App.h:326-350)

**Keep:**
- ✅ `shaders/mode9/shadow_map_cs.hlsl` - Working RayQuery reference
- ✅ `src/dxr/ASBuilder.cpp` - BLAS/TLAS infrastructure
- ✅ `m_emissionTexture` - Still used for visualization

---

## Success Criteria

**Mode 9.2 "Particle Relight" RT is successful when:**
1. ✅ Uses genuine ray tracing (RayQuery with BLAS traversal)
2. ✅ Particles illuminate nearby particles based on temperature
3. ✅ 60fps with 100,000 particles on RTX 4060 Ti
4. ✅ Visually distinct from baseline (hot particles glow, cool particles dim)
5. ✅ No atomic operation hacks or spatial grid approximations
6. ✅ PIX Graphics Debugger shows BLAS structure with 100K AABBs

**Visual Validation:**
- Hot particles (25,000K) should appear bright white/yellow
- Medium particles (15,000K) should appear orange
- Cool particles (5,000K) should be dimly lit by nearby hot particles
- Overall accretion disk should have realistic temperature-based glow

---

## Known Issues and Solutions

### Issue: RayQuery Self-Intersection
**Symptom:** Particles illuminate themselves (incorrect)
**Solution:** Already implemented in shader - ray origin offset by 0.01 units

### Issue: BLAS Update Too Slow
**Symptom:** Frame rate drops to 30fps
**Solution:** Verify `ALLOW_UPDATE` flag is set (already done in ASBuilder.cpp:303)

### Issue: No Lighting Visible
**Symptom:** Particles render but no RT lighting effect
**Solution:**
1. Check lighting buffer SRV binding in particle mesh shader
2. Verify particle temperatures are non-zero (check particle buffer)
3. Add diagnostic readback to check lighting buffer contents

### Issue: Black Screen After RT Integration
**Symptom:** Particles disappear completely
**Solution:**
1. Check resource barriers (AABB UAV barrier, BLAS UAV barrier)
2. Verify SRV indices are correctly allocated
3. Check PIX for D3D errors

---

## Next Immediate Steps

1. **Implement `createPerParticleBLASResources()`** in App.cpp
2. **Implement `createRTLightingPipeline()`** in App.cpp (AABB gen + RT lighting PSOs)
3. **Implement `updateParticleAABBs()`** in App.cpp
4. **Implement `computeRTLighting()`** in App.cpp
5. **Wire up calls** in `Initialize()` and `renderFrame()`
6. **Test with 10K particles** first
7. **Scale to 100K** after validation

**Estimated Time:** 2-3 hours for integration, 1 hour for testing/debugging

---

*Generated on 2025-10-03 during Mode 9.2 RT implementation*
*Based on MODE_9_2_RT_IMPLEMENTATION_PLAN.md created by specialized DXR agents*
