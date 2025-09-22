# Enabling DXR Ray Tracing in PlasmaDX

**Date**: September 22, 2025
**Author**: Claude Code
**Purpose**: Document the transition from compute-only to DXR ray tracing for enhanced lighting

## Current State

### What's Working
- ✅ Volume rendering via compute shader (beautiful!)
- ✅ Self-shadowing in compute path (lines 73-93 in ray_march_cs.hlsl)
- ✅ Debug modes (F4 cycling through Off/RayDir/Bounds/DensityProbe)
- ✅ PIX capture automation system

### Compute Path Limitations
- Single directional light only
- No geometry shadows on volume
- No reflections or GI
- Limited to volume-only lighting

## DXR Benefits We'll Get

1. **Hard shadows** from geometry onto volume
2. **Multiple light sources** with proper occlusion
3. **Reflections** and inter-reflections
4. **Global illumination** approximation
5. **Better performance** for complex lighting (hardware acceleration)

## Implementation Plan

### Step 1: Enable DXR in App.cpp
**File**: `src/core/App.cpp:1180`
```cpp
// BEFORE (compute-only by default):
bool dxrDisabled = Env::GetBool("PLASMADX_DISABLE_DXR", true);

// AFTER (DXR enabled by default):
bool dxrDisabled = Env::GetBool("PLASMADX_DISABLE_DXR", false);
```

### Step 2: Verify DXR Pipeline Components

The DXR path requires these components to be valid:
- `m_dxrPipeline` - Ray tracing pipeline state object
- `m_dxrPipeline->GetPSO()` - Valid pipeline state
- `m_sbt` - Shader Binding Table
- `m_tlasResult` - Top Level Acceleration Structure

Check in App.cpp:1181:
```cpp
bool canDoDXR = (!dxrDisabled && m_dxrPipeline &&
                 m_dxrPipeline->GetPSO() && m_sbt && m_tlasResult);
```

### Step 3: DXR Shaders Overview

**Raygen Shader** (`shaders/dxr/raygen.hlsl`):
- Generates primary rays from camera
- Invokes TraceRay for each pixel
- Writes to HDR target

**Miss Shader** (`shaders/dxr/miss.hlsl`):
- Handles rays that miss all geometry
- Returns background/sky color

**ClosestHit Shader** (`shaders/dxr/closesthit.hlsl`):
- Handles ray-geometry intersections
- Samples volume at hit point
- Computes lighting with shadows

### Step 4: Integration Points

The DXR path will integrate with volume rendering at these points:

1. **In raygen.hlsl**:
   - Sample volume along primary ray
   - Accumulate volumetric contribution

2. **In closesthit.hlsl**:
   - Cast shadow rays through volume
   - Attenuate light by volume density

3. **Shadow rays**:
   - Use TraceRay with RAY_FLAG_SKIP_CLOSEST_HIT_SHADER
   - Accumulate volume opacity along shadow ray

## Code Changes

### 1. Enable DXR (App.cpp:1180)
```cpp
bool dxrDisabled = Env::GetBool("PLASMADX_DISABLE_DXR", false); // Enable DXR
```

### 2. Add Volume Sampling to Raygen (raygen.hlsl)
```hlsl
[shader("raygeneration")]
void RayGen() {
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 uv = (launchIndex.xy + 0.5) / dims;

    // Generate ray
    float3 rayOrigin = g_viewPos;
    float3 rayDir = GetRayDirection(uv);

    // Volume marching along primary ray
    float3 volumeColor = MarchVolume(rayOrigin, rayDir);

    // Trace for geometry
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    Payload payload;
    TraceRay(g_tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    // Composite volume over geometry
    float3 finalColor = lerp(payload.color, volumeColor, payload.volumeAlpha);
    g_hdrTarget[launchIndex] = float4(finalColor, 1.0);
}
```

### 3. Add Shadow Ray Volume Accumulation
```hlsl
float TraceShadowRay(float3 origin, float3 lightDir) {
    RayDesc shadowRay;
    shadowRay.Origin = origin;
    shadowRay.Direction = lightDir;
    shadowRay.TMin = 0.001;
    shadowRay.TMax = 100.0;

    ShadowPayload shadow;
    TraceRay(g_tlas, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
             0xFF, 1, 0, 1, shadowRay, shadow);

    return shadow.visibility;
}
```

## Testing Plan

### Phase 1: Basic DXR Enable
1. Change default in App.cpp to enable DXR
2. Build and run
3. Verify no crashes or device lost
4. Check logs for DXR pipeline creation

### Phase 2: Performance Comparison
1. Capture PIX with compute path (PLASMADX_DISABLE_DXR=1)
2. Capture PIX with DXR path (default)
3. Compare frame times and GPU utilization

### Phase 3: Visual Quality
1. Screenshot compute path
2. Screenshot DXR path
3. Compare shadow quality and lighting

## Rollback Plan

If DXR causes issues, rollback by:
1. Set environment variable: `PLASMADX_DISABLE_DXR=1`
2. Or revert App.cpp:1180 to `true`

## Expected Issues & Solutions

### Issue 1: Device Lost
- **Cause**: Invalid TLAS or SBT
- **Solution**: Check TLAS build, verify SBT alignment

### Issue 2: Black Screen
- **Cause**: Shaders not bound correctly
- **Solution**: Verify shader compilation, check root signatures

### Issue 3: Performance Drop
- **Cause**: Too many rays or bounces
- **Solution**: Reduce max recursion depth, optimize shaders

## Success Criteria

1. ✅ DXR path renders without errors
2. ✅ Visual output matches or exceeds compute quality
3. ✅ Performance within 20% of compute path
4. ✅ PIX captures show valid ray dispatch
5. ✅ Can toggle between compute/DXR with env var

## Next Steps After DXR

Once DXR is working:
1. Add multiple light sources
2. Implement area lights
3. Add reflective surfaces
4. Global illumination probe grid
5. Temporal denoising for cleaner output

---

**Note**: This document tracks the transition from compute-only to full DXR ray tracing. Updates will be added as implementation progresses.