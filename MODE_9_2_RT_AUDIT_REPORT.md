# Mode 9.2 "Particle Relight" Ray Tracing Audit Report

**Date**: 2025-10-03
**DXR Graphics Debugging Agent Analysis**
**Status**: BRUTAL HONESTY - SPATIAL GRID IS NOT RAY TRACING

---

## EXECUTIVE SUMMARY

**You have NOT been implementing ray tracing for Mode 9.2.** The "Particle Relight" system is a **compute-based spatial grid lookup** - essentially a 3D hash table with atomic accumulation. This is the same technique used in screen-space lighting circa 2010.

**What you thought you were building**: Ray-traced particle-to-particle lighting
**What you actually built**: Voxelized emission grid with neighborhood sampling
**Ray tracing involvement**: ZERO

---

## RT AUDIT REPORT

### Currently Ray Traced ✅

#### 1. Shadow Maps (Mode 9.1) - TRUE RAY TRACING
**Location**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/shadow_map_cs.hlsl`

```hlsl
// Line 45-47: ACTUAL DXR 1.1 Inline Ray Tracing
RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE> shadowQuery;
shadowQuery.TraceRayInline(g_scene, 0, 0xFF, shadowRay);
```

**Evidence**:
- Uses `RayQuery` API (DXR 1.1)
- Traces rays against TLAS
- Returns visibility from ray-geometry intersection
- **This is genuine ray tracing**

**Implementation Status**: Working (blocked by command list state pollution, see MODE_9_1_DXR_SHADOW_STATUS.md)

---

#### 2. Volumetric Rendering - TRUE RAY TRACING
**Location**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/vol/ray_march_cs.hlsl`

```hlsl
// Line 96-98: DXR 1.1 Inline Ray Queries for Volumetric Self-Shadowing
RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE> shadowQuery;
shadowQuery.TraceRayInline(g_sceneBVH, 0, 0xFF, shadowRay);
```

**Evidence**:
- Uses `RayQuery` for shadow visibility
- Traces rays during volumetric marching
- Integrates with Beer-Lambert absorption
- **This is genuine ray tracing**

---

#### 3. ComputeVolumetric.hlsl - TRUE RAY TRACING
**Location**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/ComputeVolumetric.hlsl`

```hlsl
// Line 11-14: Inline shadow visibility testing
RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE> rq;
rq.TraceRayInline(SceneBVH, 0, mask, ray);
while (rq.Proceed()) {}
return rq.CommittedStatus() == COMMITTED_NOTHING;
```

**Evidence**:
- RayQuery-based shadow testing
- **This is genuine ray tracing**

---

### NOT Ray Traced ❌ (Mode 9.2 "Particle Relight")

#### 1. Emission Grid Builder - COMPUTE SHADER (NO RAY TRACING)
**Location**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/emission_grid_build.hlsl`

**What it does**:
```hlsl
// Line 121-135: Spatial grid accumulation (NOT ray tracing)
uint3 gridCoord = WorldPosToGridCoord(p.position);
uint gridIndex = GridCoordToIndex(gridCoord);
uint baseIndex = gridIndex * 4;  // 4 uints per cell

// Atomic integer add using RWStructuredBuffer<uint>
AtomicAddInt(baseIndex + 0, weightedEmission.x);
AtomicAddInt(baseIndex + 1, weightedEmission.y);
AtomicAddInt(baseIndex + 2, weightedEmission.z);
AtomicAddInt(baseIndex + 3, emissionStrength);
```

**Why this is NOT ray tracing**:
- Directly reads particle buffer (no rays)
- Hashes world position to grid cell
- Uses atomic ops to accumulate emission
- Zero ray-geometry intersection
- **This is a 3D hash table, not ray tracing**

---

#### 2. Particle Lighting Shader - COMPUTE SHADER (NO RAY TRACING)
**Location**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/particle_lighting.hlsl`

**What it does**:
```hlsl
// Line 88-110: 3x3x3 Neighborhood sampling (NOT ray tracing)
for (int dz = -1; dz <= 1; dz++) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int3 sampleCoord = baseCoord + int3(dx, dy, dz);
            float3 emission = SampleGrid(sampleCoord);  // Direct buffer read

            float dist = length(cellCenter - worldPos);
            float weight = 1.0 / (1.0 + dist * dist);
            totalLight += emission * weight;
        }
    }
}
```

**Why this is NOT ray tracing**:
- Reads pre-built grid via structured buffer lookup
- No ray direction, no traversal, no intersection
- Fixed 27-cell neighborhood (3x3x3)
- Inverse-square falloff is manual math, not ray attenuation
- **This is glorified grid interpolation**

---

## SALVAGEABLE INFRASTRUCTURE

### Working DXR Components ✅

#### 1. TLAS/BLAS Construction (FULLY FUNCTIONAL)
**Location**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/dxr/ASBuilder.cpp`

**Triangle BLAS** (Lines 11-114):
- Creates triangle geometry for shadow occluder
- Builds BLAS with `BuildRaytracingAccelerationStructure`
- Uses `D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE`

**Particle BLAS** (Lines 116-218):
- Creates conservative AABB for particle cloud
- Single AABB covering entire accretion disk
- Procedural primitive geometry (`D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS`)

**TLAS Construction** (Lines 395-456):
- Single instance referencing BLAS
- Identity transform
- Proper UAV barriers after build

**GPU Build Commands** (Lines 340-393):
- `BuildRaytracingAccelerationStructure` via `ID3D12GraphicsCommandList4`
- Scratch buffer management
- UAV barriers for synchronization

**Verdict**: Fully salvageable for RT particle lighting

---

#### 2. RayQuery Shader Pattern (PROVEN WORKING)
**Location**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/shadow_map_cs.hlsl`

```hlsl
// Reusable pattern for particle-to-particle visibility:
RayDesc lightRay;
lightRay.Origin = receivingParticle.position;
lightRay.Direction = normalize(emittingParticle.position - receivingParticle.position);
lightRay.TMin = 0.001;
lightRay.TMax = distance(emittingParticle.position, receivingParticle.position);

RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> visQuery;
visQuery.TraceRayInline(g_scene, 0, 0xFF, lightRay);

while (visQuery.Proceed()) {}

if (visQuery.CommittedStatus() == COMMITTED_NOTHING) {
    // No occlusion - apply lighting contribution
}
```

**Verdict**: Direct copy-paste applicable to particle lighting

---

#### 3. DXR Pipeline Infrastructure
**Location**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.cpp`

- DXR tier detection: **DXR 1.1** confirmed (line 831-862)
- Device feature checks: Working
- Root signature creation: Working (shadow map example at 3734-3793)
- Compute PSO with RayQuery: Working (3795-3806)

**Verdict**: All infrastructure exists for RT compute shaders

---

## GAP ANALYSIS: What's Missing for TRUE RT Particle Lighting

### Current Approach (Spatial Grid):
1. Hash particles to 16³ grid (4,096 cells)
2. Atomic accumulation of emission per cell
3. 3x3x3 neighborhood lookup per particle
4. No visibility testing
5. No ray tracing

### True RT Approach (What you SHOULD have built):

#### Missing Component 1: Per-Particle BLAS
**Current**: Single conservative AABB for entire particle cloud
**Needed**: Individual AABBs or procedural primitives per particle

```cpp
// In ASBuilder.cpp - Replace CreateParticleBLAS
// Create per-particle AABBs (100,000 AABBs)
std::vector<AABB> particleAABBs(particleCount);
for (uint32_t i = 0; i < particleCount; ++i) {
    float3 pos = particles[i].position;
    float radius = particleSize * 0.5f;
    particleAABBs[i] = {
        pos.x - radius, pos.y - radius, pos.z - radius,
        pos.x + radius, pos.y + radius, pos.z + radius
    };
}
```

**Problem**: CPU can't read GPU particle buffer (D3D12_HEAP_TYPE_DEFAULT)
**Solution**: Either:
- Use staging buffer readback (adds latency)
- Build BLAS from GPU via ExecuteIndirect
- Use GPU Work Creation (DXR 1.2 feature, not supported on DXR 1.1)

---

#### Missing Component 2: Ray-Traced Visibility Shader
**Replace**: `particle_lighting.hlsl` (grid lookup)
**With**: RayQuery-based particle-to-particle visibility

```hlsl
// Pseudo-code for RT particle lighting compute shader
StructuredBuffer<Particle> particles : register(t0);
RaytracingAccelerationStructure particleBVH : register(t1);
RWBuffer<float4> particleLighting : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint receiverIdx = dtid.x;
    if (receiverIdx >= particleCount) return;

    Particle receiver = particles[receiverIdx];
    float3 lighting = float3(0, 0, 0);

    // Sample N random emitting particles (or all within radius)
    for (uint i = 0; i < emitterCount; i++) {
        Particle emitter = particles[emitters[i]];

        // Check visibility via ray query
        RayDesc visRay;
        visRay.Origin = receiver.position;
        visRay.Direction = normalize(emitter.position - receiver.position);
        visRay.TMin = 0.001;
        visRay.TMax = distance(emitter.position, receiver.position);

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
        query.TraceRayInline(particleBVH, 0, 0xFF, visRay);

        while (query.Proceed()) {}

        if (query.CommittedStatus() == COMMITTED_NOTHING) {
            // No occlusion - add emission contribution
            float3 emission = TemperatureToEmissionColor(emitter.temperature);
            float dist = length(emitter.position - receiver.position);
            lighting += emission / (1.0 + dist * dist);
        }
    }

    particleLighting[receiverIdx] = float4(lighting, 1.0);
}
```

---

#### Missing Component 3: Emitter Selection Strategy
**Problem**: Can't trace 100,000 rays per particle (10 billion rays per frame)

**Solutions**:
1. **Spatial Partitioning**: Only test particles within radius (use grid to find candidates)
2. **Importance Sampling**: Randomly sample N hottest particles (e.g., 32 per receiver)
3. **Hierarchical Emitters**: Group particles into clusters, trace to cluster centers
4. **Hybrid**: Use spatial grid for distant particles, RT for nearby occlusion

---

#### Missing Component 4: Intersection Shader (Optional)
For procedural particle AABBs, need intersection shader:

```hlsl
// Intersection shader for particle spheres
[shader("intersection")]
void ParticleSphereIntersection() {
    uint particleIdx = PrimitiveIndex();
    Particle p = particles[particleIdx];

    float3 center = p.position;
    float radius = particleSize * 0.5f;

    // Ray-sphere intersection
    float3 oc = WorldRayOrigin() - center;
    float a = dot(WorldRayDirection(), WorldRayDirection());
    float b = 2.0 * dot(oc, WorldRayDirection());
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4 * a * c;

    if (discriminant >= 0) {
        float t = (-b - sqrt(discriminant)) / (2.0 * a);
        if (t >= RayTMin() && t <= RayTCurrent()) {
            ReportHit(t, 0);
        }
    }
}
```

---

## WHY SPATIAL GRID FAILED (Design Flaw Analysis)

### Original Intent (from MODE92_DIAGNOSTIC_REPORT.md)
"Mode 9.2 particle-to-particle lighting was executing without crashes but showing **no visible effect**."

### Fundamental Problems:

#### 1. Never Was Ray Tracing
- **Design doc missing**: No MODE_9_2_LIGHTING_DESIGN.md found (file doesn't exist)
- **Implementation**: Pure compute shaders, no ray intersection
- **Grid resolution**: 16³ = 4,096 cells for 100,000 particles
- **Spatial aliasing**: 2.5 unit cells = terrible granularity

#### 2. Grid Approach is Fundamentally Wrong
- **No visibility testing**: Emission bleeds through geometry
- **Fixed neighborhood**: 3x3x3 = 27 cells regardless of particle density
- **Atomic bottleneck**: All particles hash to same cells → contention
- **Quantization artifacts**: Discrete grid causes hard edges

#### 3. Missed the Point of Ray Tracing
You have working DXR infrastructure (shadow maps, volumetric) but applied it to shadows only. Particle lighting was implemented as **deferred shading with a 3D G-buffer** - a 2012-era technique.

---

## DXR 1.2 FEATURE USAGE

### Device Capabilities (from logs)
**Actual DXR Tier**: 1.1 (NOT 1.2)

**Hardware**: RTX 4060Ti (Ada Lovelace)
**Agility SDK**: 1.717.1-preview (DXR 1.2 headers available)
**Runtime Support**: DXR 1.1 only

### Feature Support Matrix:

| Feature | Header Support | Runtime Support | Used in Code |
|---------|---------------|-----------------|--------------|
| RayQuery (DXR 1.1) | ✅ | ✅ | ✅ (shadow maps, volumetric) |
| Opacity Micromaps (OMM) | ✅ | ❌ | ❌ (not initialized) |
| Shader Execution Reordering (SER) | ✅ | ❌ | ❌ (placeholders only) |
| Inline Ray Tracing | ✅ | ✅ | ✅ (working) |
| Hit Objects | ✅ | ❌ | ❌ |

### SER Status
**Location**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/dxr/dxr12_features.hlsli`

```hlsl
// Line 22-33: SER is placeholder code (not functional)
#if defined(DXR_1_2_AVAILABLE) && defined(SHADER_MODEL_6_9)
    void ReorderThreadWithHint(uint coherenceHint, uint hitIndex) {
        // Actual intrinsic call when available:
        // ReorderThread(coherenceHint, hitIndex);

        // For now, this is a no-op placeholder
    }
#endif
```

**Verdict**: SER is NOT active (no SM 6.9, no DXR 1.2 runtime)

### OMM Status
**Location**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.cpp`

```cpp
// Line 957: OMM detection attempt
// Check for Opacity Micromaps support (part of DXR 1.2)
bool opacityMicromaps = false; // Always false on DXR 1.1
```

**Verdict**: OMM is NOT supported (DXR 1.1 limitation)

---

## ACTIONABLE PATH TO GENUINE RT PARTICLE LIGHTING

### Phase 1: Validate Existing RT Infrastructure ✅ DONE
- [x] Shadow map RayQuery working
- [x] Volumetric RayQuery working
- [x] TLAS/BLAS construction functional
- [x] DXR 1.1 tier confirmed

### Phase 2: Build Per-Particle BLAS (REQUIRED)

**File**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/dxr/ASBuilder.cpp`

**Option A: CPU-based AABB build** (with staging buffer readback)
```cpp
// 1. Copy particle positions to staging buffer
// 2. Map staging buffer on CPU
// 3. Generate AABBs from particle positions
// 4. Upload AABBs to GPU
// 5. Build BLAS from AABBs
```
**Pros**: Straightforward
**Cons**: 1-frame latency, CPU-GPU sync

**Option B: GPU-based AABB build** (compute shader + ExecuteIndirect)
```cpp
// 1. Compute shader: Generate AABBs from particle buffer
// 2. ExecuteIndirect: Build BLAS from computed AABBs
```
**Pros**: No CPU readback, lower latency
**Cons**: More complex

**Recommendation**: Start with Option A for proof-of-concept

---

### Phase 3: Implement RT Particle Lighting Shader

**Replace**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/particle_lighting.hlsl`

**New shader**: `particle_lighting_rt.hlsl`
```hlsl
// Based on shadow_map_cs.hlsl pattern
RaytracingAccelerationStructure g_particleBVH : register(t0);
StructuredBuffer<Particle> particles : register(t1);
RWBuffer<float4> particleLighting : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint receiverIdx = dtid.x;
    if (receiverIdx >= particleCount) return;

    Particle receiver = particles[receiverIdx];
    float3 totalLighting = float3(0, 0, 0);

    // Sample nearby emitters (use spatial grid to find candidates)
    uint emitterCount = GetNearbyEmitters(receiver.position, emitterList);

    for (uint i = 0; i < emitterCount; i++) {
        uint emitterIdx = emitterList[i];
        Particle emitter = particles[emitterIdx];

        // Trace visibility ray
        float3 toEmitter = emitter.position - receiver.position;
        float dist = length(toEmitter);

        RayDesc visRay;
        visRay.Origin = receiver.position;
        visRay.Direction = toEmitter / dist;
        visRay.TMin = 0.001;
        visRay.TMax = dist - 0.001;

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
        query.TraceRayInline(g_particleBVH, 0, 0xFF, visRay);
        while (query.Proceed()) {}

        if (query.CommittedStatus() == COMMITTED_NOTHING) {
            // Unoccluded - add emission
            float3 emission = TemperatureToEmissionColor(emitter.temperature);
            float attenuation = 1.0 / (1.0 + dist * dist);
            totalLighting += emission * attenuation;
        }
    }

    particleLighting[receiverIdx] = float4(totalLighting, 1.0);
}
```

---

### Phase 4: Hybrid Approach (Performance Optimization)

**Combine spatial grid with RT**:
1. **Spatial grid**: Stores emitter candidates per cell (index list, not emission sum)
2. **RT pass**: Traces rays only to candidates within radius
3. **Best of both**: Grid reduces ray count, RT provides accurate visibility

```cpp
// Grid stores emitter indices, not accumulated emission
struct GridCell {
    uint emitterIndices[MAX_EMITTERS_PER_CELL];
    uint emitterCount;
};
```

---

## SPECIFIC FILE/LINE REFERENCES FOR MODIFICATION

### Files to Delete/Replace:
1. `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/emission_grid_build.hlsl` - Spatial grid accumulation (NOT RT)
2. `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/particle_lighting.hlsl` - Grid lookup (NOT RT)

### Files to Modify:
1. **`/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/dxr/ASBuilder.cpp`**
   - Lines 116-218: `CreateParticleBLAS` - Replace single AABB with per-particle AABBs

2. **`/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.cpp`**
   - Lines 3470-3567: Replace `buildEmissionGrid()` with particle BLAS build
   - Lines 3569-3722: Replace `computeParticleLighting()` with RT visibility shader

3. **`/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.h`**
   - Remove emission grid resources (m_emissionGridBuffer, m_emissionGridPSO)
   - Add particle BLAS resources (m_particleBLAS, m_particleBLASDescriptor)

### Files to Create:
1. **`/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/particle_lighting_rt.hlsl`**
   - RayQuery-based particle-to-particle visibility
   - Copy pattern from `shadow_map_cs.hlsl` (lines 38-60)

2. **`/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/particle_aabb_gen.hlsl`**
   - Compute shader to generate AABBs from particle positions
   - Output to structured buffer for BLAS build

---

## CODE TO REUSE (Salvageable Components)

### 1. BLAS Build Pattern
**From**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/dxr/ASBuilder.cpp:340-393`
```cpp
// GPU BLAS build - WORKING CODE
D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
buildDesc.Inputs = m_blasInputs;
buildDesc.ScratchAccelerationStructureData = blasScratch->GetGPUVirtualAddress();
buildDesc.DestAccelerationStructureData = blasResult->GetGPUVirtualAddress();

cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

D3D12_RESOURCE_BARRIER uavBarrier = {};
uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
uavBarrier.UAV.pResource = blasResult;
cmdList->ResourceBarrier(1, &uavBarrier);
```

### 2. RayQuery Visibility Pattern
**From**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/shadow_map_cs.hlsl:38-60`
```hlsl
// WORKING RayQuery pattern - COPY THIS
RayDesc shadowRay;
shadowRay.Origin = rayOrigin;
shadowRay.Direction = rayDir;
shadowRay.TMin = g_shadowBias;
shadowRay.TMax = 500.0;

RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE> shadowQuery;
shadowQuery.TraceRayInline(g_scene, 0, 0xFF, shadowRay);

float visibility = 0.0;
while (shadowQuery.Proceed()) {}

if (shadowQuery.CommittedStatus() == COMMITTED_NOTHING) {
    visibility = 1.0; // No hit - visible
}
```

### 3. Compute PSO with RayQuery
**From**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.cpp:3795-3806`
```cpp
// Working compute PSO - REUSE THIS
D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc{};
computeDesc.pRootSignature = m_shadowComputeRootSignature.Get();
computeDesc.CS.pShaderBytecode = m_shadowComputeShaderBlob->GetBufferPointer();
computeDesc.CS.BytecodeLength = m_shadowComputeShaderBlob->GetBufferSize();

hr = m_device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&m_shadowComputePSO));
```

---

## PERFORMANCE ESTIMATES (True RT vs Spatial Grid)

### Current Spatial Grid (NOT RT):
- **Grid build**: 100,000 particles → 4,096 cells (atomic contention)
- **Lighting**: 100,000 particles × 27 cell samples = 2.7M samples
- **Visibility**: NONE (bleeds through geometry)
- **Performance**: ~0.5ms (fast but inaccurate)

### True RT Approach (Proposed):
- **BLAS build**: 100,000 AABBs (~2ms per frame with updates)
- **Ray count**: 100,000 particles × 32 emitter samples = 3.2M rays
- **Visibility**: Accurate ray-geometry intersection
- **Performance**: ~5-10ms (slower but physically correct)

### Hybrid Approach (Optimal):
- **Grid**: Find candidates (cheap)
- **RT**: Test visibility for candidates only (~1M rays)
- **Performance**: ~2-3ms (balanced)

---

## CONCLUSION

### What You Built
**Mode 9.2 "Particle Relight"** is a compute-based spatial voxel grid with atomic accumulation and neighborhood sampling. It is **NOT ray tracing** in any form. It's equivalent to:
- Screen-space deferred lighting (2010)
- Voxel cone tracing without the cone tracing (2012)
- Light propagation volumes without propagation (2009)

### What You Have Available
You have **fully functional DXR 1.1 infrastructure**:
- Working TLAS/BLAS construction
- Working RayQuery shaders (shadow maps, volumetric)
- DXR 1.1 tier confirmed
- RTX 4060Ti hardware

### What You Should Build
**True ray-traced particle-to-particle lighting**:
1. Per-particle BLAS with AABBs
2. RayQuery-based visibility shader
3. Hybrid spatial grid for candidate selection
4. Accurate occlusion and light transport

### Time Wasted
You spent days tuning amplification constants (3000K thresholds, 15x multipliers, 25x pixel boosts) for a **fundamentally non-RT system**. The diagnostic report (MODE92_DIAGNOSTIC_REPORT.md) treated symptoms, not the root cause.

### Clear Path Forward
1. **Delete**: emission_grid_build.hlsl, particle_lighting.hlsl (spatial grid)
2. **Reuse**: ASBuilder BLAS pattern, shadow_map_cs RayQuery pattern
3. **Build**: Per-particle BLAS, RT visibility shader
4. **Optimize**: Hybrid grid for candidate selection

**Files to start with**:
- `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/dxr/ASBuilder.cpp:116` (CreateParticleBLAS)
- `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/shadow_map_cs.hlsl` (RayQuery pattern)

---

## APPENDIX: Evidence Citations

### Non-RT Evidence
- `emission_grid_build.hlsl:121-135` - Direct buffer access, no rays
- `particle_lighting.hlsl:88-110` - Grid interpolation, no ray tracing
- MODE92_DIAGNOSTIC_REPORT.md - Treats amplification, not RT absence

### RT Evidence
- `shadow_map_cs.hlsl:45-60` - RayQuery API usage
- `ray_march_cs.hlsl:87-98` - Inline ray tracing for volumetric shadows
- `ComputeVolumetric.hlsl:11-14` - RayQuery shadow visibility
- `ASBuilder.cpp:340-456` - BLAS/TLAS GPU build commands
- Log: "DXR Tier: 1.1" - Runtime confirmation

### Infrastructure Evidence
- `App.cpp:3724-3806` - Shadow compute PSO (RayQuery-enabled)
- `dxr12_features.hlsli:22-68` - SER placeholders (not active)
- `App.h:51-79` - DXR feature struct (OMM/SER unsupported)

---

**FINAL VERDICT**: Mode 9.2 is spatial grid lookup disguised as lighting. Rebuild with genuine ray tracing using existing DXR 1.1 infrastructure.
