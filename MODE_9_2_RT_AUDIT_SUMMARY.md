# Mode 9.2 RT Audit - Quick Summary

**Date**: 2025-10-03
**Status**: Spatial Grid is NOT Ray Tracing

---

## RT AUDIT REPORT

### Currently Ray Traced ✅

| System | File | Evidence | Status |
|--------|------|----------|--------|
| **Shadow Maps** | `shaders/mode9/shadow_map_cs.hlsl` | RayQuery API (line 46-47) | Working (blocked by cmd list issue) |
| **Volumetric** | `shaders/vol/ray_march_cs.hlsl` | RayQuery shadows (line 96-98) | Working |
| **Compute Vol** | `shaders/ComputeVolumetric.hlsl` | RayQuery visibility (line 11-14) | Working |

### NOT Ray Traced ❌

| System | File | Why NOT RT | What It Actually Is |
|--------|------|------------|-------------------|
| **Emission Grid** | `shaders/mode9/emission_grid_build.hlsl` | Direct buffer read, atomics (line 121-135) | 3D hash table with atomic accumulation |
| **Particle Lighting** | `shaders/mode9/particle_lighting.hlsl` | Grid interpolation, no rays (line 88-110) | 27-cell neighborhood sampling |

---

## Salvageable Infrastructure

### Working DXR Components
1. **TLAS/BLAS Construction** - `src/dxr/ASBuilder.cpp:340-456`
   - Triangle BLAS (working)
   - Particle BLAS (single AABB, needs per-particle AABBs)
   - GPU build commands (working)

2. **RayQuery Pattern** - `shaders/mode9/shadow_map_cs.hlsl:38-60`
   - Reusable for particle visibility
   - Proven working in shadow maps

3. **Compute PSO** - `src/core/App.cpp:3795-3806`
   - RayQuery-enabled compute shader setup

---

## Missing for RT Particle Lighting

1. **Per-Particle BLAS**
   - Current: Single conservative AABB
   - Need: 100,000 individual AABBs (one per particle)
   - Problem: CPU can't read GPU particle buffer
   - Solution: Staging buffer readback OR GPU-based AABB generation

2. **RT Visibility Shader**
   - Replace grid lookup with RayQuery
   - Pattern: Copy from shadow_map_cs.hlsl
   - Trace rays between emitter and receiver particles

3. **Emitter Selection**
   - Can't trace 10B rays (100K particles × 100K emitters)
   - Use spatial grid to find nearby candidates
   - Trace rays only to candidates (hybrid approach)

---

## DXR 1.2 Feature Status

**Actual DXR Tier**: 1.1 (NOT 1.2)

| Feature | Header | Runtime | Used |
|---------|--------|---------|------|
| RayQuery (DXR 1.1) | ✅ | ✅ | ✅ |
| OMM | ✅ | ❌ | ❌ |
| SER | ✅ | ❌ | ❌ (placeholders only) |

**Verdict**: DXR 1.1 fully functional, DXR 1.2 features unavailable

---

## Action Plan

### Phase 1: Build Per-Particle BLAS
**File**: `src/dxr/ASBuilder.cpp:116`

```cpp
// Option A: CPU-based (simpler)
1. Copy particle positions to staging buffer
2. Map staging on CPU
3. Generate AABBs
4. Upload and build BLAS

// Option B: GPU-based (faster)
1. Compute shader generates AABBs from particles
2. ExecuteIndirect builds BLAS
```

### Phase 2: RT Visibility Shader
**File**: Create `shaders/mode9/particle_lighting_rt.hlsl`

```hlsl
// Copy pattern from shadow_map_cs.hlsl
RayQuery<FLAGS> query;
query.TraceRayInline(g_particleBVH, 0, 0xFF, visRay);
while (query.Proceed()) {}

if (query.CommittedStatus() == COMMITTED_NOTHING) {
    // Unoccluded - add emission
}
```

### Phase 3: Hybrid Emitter Selection
- Spatial grid stores emitter indices (not emission)
- RT traces rays only to nearby candidates
- Best of both: grid reduces rays, RT provides accuracy

---

## Files to Modify

### Delete/Replace:
- `shaders/mode9/emission_grid_build.hlsl` (spatial grid, NOT RT)
- `shaders/mode9/particle_lighting.hlsl` (grid lookup, NOT RT)

### Modify:
- `src/dxr/ASBuilder.cpp:116-218` - Per-particle BLAS
- `src/core/App.cpp:3470-3722` - Replace grid with RT

### Create:
- `shaders/mode9/particle_lighting_rt.hlsl` - RayQuery visibility
- `shaders/mode9/particle_aabb_gen.hlsl` - GPU AABB generation

---

## Key Takeaway

**Mode 9.2 is NOT ray tracing.** It's a compute-based spatial voxel grid (2010-era technique). You have fully functional DXR 1.1 infrastructure but only used it for shadows. Rebuild particle lighting with genuine ray tracing using existing TLAS/BLAS patterns.

**Evidence**: See `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/MODE_9_2_RT_AUDIT_REPORT.md`
