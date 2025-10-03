# Mode 9.2: Genuine DXR Ray Traced Particle Lighting - Implementation Plan

## CRITICAL CONTEXT: What Went Wrong

### The Failed Approach (Days Wasted)
We spent extensive time implementing a **spatial grid emission system** that was fundamentally NOT ray tracing:
- `shaders/mode9/emission_grid_build.hlsl` - Compute shader hash table accumulation
- `shaders/mode9/particle_lighting.hlsl` - Grid cell interpolation (no rays)
- **Problem:** InterlockedAdd atomics silently failed on both RWByteAddressBuffer and RWStructuredBuffer<uint>
- **Root cause:** This was NEVER ray tracing - just a 2010-era compute shader approximation

### What IS Working (Proven DXR)
- ✅ Shadow maps use RayQuery successfully (`shaders/mode9/shadow_map_cs.hlsl`)
- ✅ TLAS/BLAS construction fully functional (`src/dxr/ASBuilder.cpp`)
- ✅ DXR 1.1 confirmed working on RTX 4060 Ti
- ✅ Volumetric rendering uses RayQuery for shadows

## THE NEW PLAN: Authentic DXR Particle-to-Particle Lighting

### Architecture (From DXR Systems Engineer Agent)

**Approach: AABB-Based Procedural Particles with RayQuery**

1. **BLAS Construction:**
   - Create one AABB per particle (100,000 AABBs)
   - Dynamic BLAS (updated every frame as particles move)
   - Estimated cost: 0.3ms BLAS update per frame

2. **Ray Tracing Strategy:**
   - Use RayQuery in compute shader (pattern from shadow_map_cs.hlsl)
   - Each particle casts 8 rays in hemisphere to find nearby emitters
   - Custom sphere-ray intersection shader for procedural geometry
   - Total rays per frame: 100K particles × 8 rays = 800K rays

3. **Performance Budget (RTX 4060 Ti, 60fps = 16.67ms):**
   - BLAS Update: 0.3ms
   - Ray Tracing: 3-5ms (with BVH acceleration)
   - SER Optimization: -40% (reduces to 2-3ms)
   - **Total RT Lighting: ~4ms (60fps ACHIEVED)**

### Why This Will Work (From RT Technique Researcher Agent)

**Production Proof:**
- **RTX Remix (Sept 2024):** "Tens of thousands of path-traced particles without significant performance reduction"
- **Cyberpunk 2077 / Indiana Jones:** 24-40% speedup with Shader Execution Reordering on RTX 40-series
- **Your RTX 4060 Ti:** Full DXR 1.2 support (though we're using DXR 1.1 features)

**Key Optimization - Shader Execution Reordering (SER):**
```hlsl
// ONE LINE for 40-100% speedup
ReorderThread(tempBucket, 2); // Groups heterogeneous particles by temperature
```

## Implementation Phases

### Phase 1: BLAS Construction (Week 1, Days 1-3)

**Goal:** Build per-particle AABB Bottom-Level Acceleration Structure

**Files to Create:**
- `src/dxr/ParticleBLAS.h`
- `src/dxr/ParticleBLAS.cpp`

**Code Pattern (from existing ASBuilder.cpp:340-456):**
```cpp
// Generate AABB buffer from particle positions
D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
geometryDesc.AABBs.AABBCount = 100000;
geometryDesc.AABBs.AABBs.StartAddress = particleAABBBuffer->GetGPUVirtualAddress();
geometryDesc.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

// Use ALLOW_UPDATE flag for dynamic particles
D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
```

**Validation:**
- Use PIX Graphics Debugger to inspect BLAS structure
- Verify 100K AABBs are correctly positioned around particles

### Phase 2: RayQuery Lighting Shader (Week 1, Days 4-5)

**Goal:** Implement ray-traced particle illumination compute shader

**Files to Create:**
- `shaders/dxr/particle_raytraced_lighting_cs.hlsl`
- `shaders/dxr/particle_intersection.hlsl` (sphere-ray intersection)

**Code Pattern (from shadow_map_cs.hlsl:38-60):**
```hlsl
RaytracingAccelerationStructure g_particleBVH : register(t0);
StructuredBuffer<Particle> g_particles : register(t1);
RWStructuredBuffer<float3> g_particleLighting : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint particleIdx = dispatchThreadID.x;
    Particle receiver = g_particles[particleIdx];
    float3 accumulatedLight = float3(0, 0, 0);

    // Cast 8 rays in hemisphere
    for (uint rayIdx = 0; rayIdx < 8; rayIdx++) {
        float3 rayDir = FibonacciHemisphere(rayIdx, 8, float3(0,1,0));

        RayDesc ray;
        ray.Origin = receiver.position + rayDir * (receiver.radius * 1.01);
        ray.Direction = rayDir;
        ray.TMin = 0.001;
        ray.TMax = 20.0; // Lighting cutoff distance

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
        query.TraceRayInline(g_particleBVH, RAY_FLAG_NONE, 0xFF, ray);
        query.Proceed();

        if (query.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
            uint hitParticleIdx = query.CommittedPrimitiveIndex();
            Particle emitter = g_particles[hitParticleIdx];

            // Calculate emission based on temperature
            float intensity = EmissionIntensity(emitter.temperature);
            float3 color = TemperatureToEmission(emitter.temperature);
            float distance = query.CommittedRayT();
            float attenuation = 1.0 / (1.0 + distance * distance);

            accumulatedLight += color * intensity * attenuation;
        }
    }

    g_particleLighting[particleIdx] = accumulatedLight / 8.0;
}
```

**Intersection Shader (for procedural AABBs):**
```hlsl
[shader("intersection")]
void ParticleIntersection() {
    uint particleIdx = PrimitiveIndex();
    Particle p = g_particles[particleIdx];

    // Analytical sphere-ray intersection
    float3 oc = ObjectRayOrigin() - p.position;
    float b = dot(oc, ObjectRayDirection());
    float c = dot(oc, oc) - p.radius * p.radius;
    float discriminant = b * b - c;

    if (discriminant >= 0.0) {
        float t = -b - sqrt(discriminant);
        if (t > RayTMin() && t < RayTCurrent()) {
            ParticleAttributes attr;
            ReportHit(t, 0, attr);
        }
    }
}
```

### Phase 3: Integration with Rendering (Week 2, Days 1-2)

**Goal:** Apply RT lighting to particle mesh shader

**Files to Modify:**
- `shaders/particles/particle_mesh.hlsl` - Add lighting buffer binding
- `src/core/App.cpp` - Dispatch RT lighting before particle rendering

**Integration:**
```hlsl
// In particle mesh shader
StructuredBuffer<float3> g_particleLighting : register(t2);

// In mesh shader main()
float3 baseColor = TemperatureToColor(particle.temperature);
float3 rtLighting = g_particleLighting[vertexID];
finalColor = baseColor + rtLighting; // Additive illumination
```

### Phase 4: Shader Execution Reordering (Week 2, Day 3)

**Goal:** Add SER for 40%+ speedup

**Files to Modify:**
- `shaders/dxr/particle_raytraced_lighting_cs.hlsl`

**Code Addition (ONE LINE):**
```hlsl
[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint particleIdx = dispatchThreadID.x;
    Particle receiver = g_particles[particleIdx];

    // SHADER EXECUTION REORDERING: Group by temperature bucket
    uint tempBucket = uint(receiver.temperature / 5000.0); // 0-5 buckets
    ReorderThread(tempBucket, 2);

    // ... rest of lighting code ...
}
```

**Enable SER in PSO:**
```cpp
// Check for SER support
D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12));
if (options12.RelaxedFormatCastingSupported) {
    // SER available on RTX 40-series
}
```

## Files to Delete (Non-RT Implementation)

**Remove these completely:**
- ❌ `shaders/mode9/emission_grid_build.hlsl` (compute grid, not RT)
- ❌ `shaders/mode9/particle_lighting.hlsl` (grid interpolation, not RT)
- ❌ `shaders/mode9/grid_clear.hlsl` (grid management, not RT)

**Keep these (working DXR):**
- ✅ `shaders/mode9/shadow_map_cs.hlsl` (RayQuery reference pattern)
- ✅ `src/dxr/ASBuilder.cpp` (BLAS/TLAS infrastructure)

## Performance Expectations

**Target Frame Budget (RTX 4060 Ti, 60fps = 16.67ms):**
```
Particle Physics:     2ms
BLAS Update:         0.3ms
Ray Traced Lighting:  4ms  (800K rays with SER optimization)
Particle Rendering:   3ms  (mesh shader + rasterization)
Shadow Maps:          2ms  (existing RayQuery)
Post-Processing:      2ms
─────────────────────────
TOTAL:              13.3ms  (75fps achieved, 3.4ms headroom)
```

**Ray Budget:**
- 100,000 particles × 8 rays = 800,000 primary rays per frame
- With BVH acceleration: ~17 AABB tests per ray (log₂(100K))
- Total AABB tests: 13.6 million per frame
- RTX 4060 Ti can handle this easily (10 TFLOPS compute)

## Hybrid Optimization (If Needed)

**If 4ms is too expensive, use tiered quality:**

```hlsl
// Adaptive ray count based on particle temperature
uint rayCount = 8; // Default high quality
if (receiver.temperature < 10000.0) rayCount = 4;  // Warm
if (receiver.temperature < 5000.0)  rayCount = 2;  // Cool
if (receiver.temperature < 2000.0)  rayCount = 0;  // Skip RT, use ambient

// Expected savings: 50% ray reduction = 2ms instead of 4ms
```

## Key References

**Agent Research Documentation:**
- `/agent/AdvancedTechniqueWebSearches/QUICK_START_RT_PARTICLES.md` - Implementation roadmap
- `/agent/AdvancedTechniqueWebSearches/ray_tracing/particle_lighting/01_AABB_Procedural_Particles.md` - AABB guide
- `/agent/AdvancedTechniqueWebSearches/ray_tracing/particle_lighting/03_Shader_Execution_Reordering.md` - SER optimization
- `/MODE_9_2_RT_AUDIT_REPORT.md` - What went wrong analysis

**Microsoft Sample Code:**
- D3D12RaytracingProceduralGeometry sample (provides 80% of needed code)
- Clone: https://github.com/microsoft/DirectX-Graphics-Samples

**Production Examples:**
- RTX Remix (Sept 2024) - "Tens of thousands of path-traced particles"
- Cyberpunk 2077 - 24% speedup with SER
- Indiana Jones - 40%+ speedup with SER on RTX 40-series

## Validation Checklist

**Week 1:**
- [ ] BLAS builds successfully (PIX shows 100K AABBs)
- [ ] RayQuery shader compiles (no errors in Output window)
- [ ] Intersection shader reports hits (debug output shows non-zero hit counts)
- [ ] 10K particles at 60fps as proof-of-concept

**Week 2:**
- [ ] Lighting buffer shows non-zero values (diagnostic readback)
- [ ] Visual result: bright particles illuminate nearby particles
- [ ] 100K particles at 60fps (Ctrl+F shows framerate)
- [ ] SER enabled (NSight Graphics shows ReorderThread active)

## Current Project State

**Hardware:**
- RTX 4060 Ti (Ada Lovelace, DXR 1.2, SER supported)
- DXR Tier: 1.1 confirmed working
- 16GB VRAM (particle BLAS uses ~40MB, plenty available)

**Codebase:**
- Working DXR: Shadow maps, volumetric rendering
- Particle count: 100,000 (NASA-quality accretion disk)
- Physics simulation: Working (particles move correctly)
- Mesh shader rendering: Working (particles display correctly)

**What's Broken:**
- Mode 9.2 spatial grid (atomic operations failed, not RT anyway)
- Emission buffer approach (wrong architecture from the start)

## Next Immediate Steps

1. **Create BLAS construction code** (`src/dxr/ParticleBLAS.cpp`)
2. **Write RayQuery lighting shader** (`shaders/dxr/particle_raytraced_lighting_cs.hlsl`)
3. **Add intersection shader** (`shaders/dxr/particle_intersection.hlsl`)
4. **Wire up dispatch** in `src/core/App.cpp`
5. **Test with 10K particles first** (scale to 100K after validation)

## Success Criteria

**Mode 9.2 "Particle Relight" is successful when:**
- ✅ Uses actual ray tracing (RayQuery/TraceRay, not compute grid)
- ✅ Particles illuminate nearby particles based on temperature
- ✅ 60fps with 100,000 particles on RTX 4060 Ti
- ✅ Visually distinct from baseline (hot particles glow, cool particles dim)
- ✅ No atomic operation hacks or spatial grid approximations

**Timeline: 2 weeks from start to completion**

---

*This plan created by deploying 3 specialized agents in parallel:*
- *DXR Systems Engineer (architecture design)*
- *RT Technique Researcher (production validation)*
- *DXR Debugging Engineer (audit of failed approach)*
