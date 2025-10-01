// Mode 9.2: RayQuery-Based Directional Shadow Map Generation
// Compute Shader Replacement for DispatchRays Implementation
// Uses Inline Raytracing (DXR 1.1) - No SBT, No separate raygen/miss shaders
// Output: R16_FLOAT shadow map (1.0 = lit, 0.0 = shadow)

//==============================================================================
// SHADER MODEL AND FLAGS
//==============================================================================
// Requires Shader Model 6.5+ for RayQuery support
// Compile with: dxc -T cs_6_5 -E CSMain shadow_map_rayquery.hlsl

//==============================================================================
// RESOURCE BINDINGS
//==============================================================================
// t0: TLAS (Top-Level Acceleration Structure)
RaytracingAccelerationStructure g_scene : register(t0, space0);

// u0: Shadow Map Output (1024x1024 R16_FLOAT)
RWTexture2D<float> g_shadowMap : register(u0, space0);

// b0: Shadow Parameters (32 bytes aligned)
cbuffer ShadowParams : register(b0, space0) {
    float3 g_lightDirection;  // Directional light (normalized)
    float g_shadowBias;       // Ray TMin offset (default: 0.01)
    float2 g_shadowMapSize;   // Resolution: (1024.0, 1024.0)
    float2 g_padding;         // Align to 16-byte boundary
};

//==============================================================================
// THREAD GROUP CONFIGURATION
//==============================================================================
// Thread Group Dimensions: 8x8x1 = 64 threads per group
// For 1024x1024 texture: Dispatch(128, 128, 1) thread groups
// Total threads: 128 * 8 = 1024 per dimension
// OPTIMAL: 64 threads balances occupancy and memory bandwidth
#define THREAD_GROUP_SIZE_X 8
#define THREAD_GROUP_SIZE_Y 8

//==============================================================================
// ORTHOGRAPHIC PROJECTION HELPER
//==============================================================================
// Converts pixel coordinate to orthographic ray (matching old raygen logic)
struct OrthographicRay {
    float3 origin;
    float3 direction;
};

OrthographicRay CreateOrthographicRay(uint2 pixelCoord, uint2 dimensions) {
    OrthographicRay result;

    // Map pixel to normalized UV [0, 1]
    float2 uv = (float2(pixelCoord) + 0.5) / float2(dimensions);
    uv = uv * 2.0 - 1.0;  // Remap to [-1, 1]

    // Ray direction: TOWARD the light (opposite of light direction)
    result.direction = normalize(g_lightDirection);

    // Create orthographic basis perpendicular to light direction
    float3 up = abs(result.direction.y) < 0.999
        ? float3(0, 1, 0)
        : float3(1, 0, 0);
    float3 right = normalize(cross(up, result.direction));
    up = cross(result.direction, right);

    // Position ray origin on orthographic plane, FAR from scene
    // Scene center: (0, 0, 0), coverage: 100 units, distance: 300 units
    result.origin = float3(0, 0, 0)
        + right * uv.x * 100.0
        + up * uv.y * 100.0
        - result.direction * 300.0;

    return result;
}

//==============================================================================
// COMPUTE SHADER ENTRY POINT
//==============================================================================
[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void CSMain(
    uint3 dispatchThreadID : SV_DispatchThreadID,  // Global thread ID (0-1023, 0-1023, 0)
    uint3 groupThreadID : SV_GroupThreadID,        // Thread ID within group (0-7, 0-7, 0)
    uint3 groupID : SV_GroupID                     // Thread group ID (0-127, 0-127, 0)
) {
    // Boundary check: Skip threads outside shadow map dimensions
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= uint(g_shadowMapSize.x) ||
        pixelCoord.y >= uint(g_shadowMapSize.y)) {
        return;
    }

    //==========================================================================
    // STEP 1: CREATE ORTHOGRAPHIC RAY
    //==========================================================================
    uint2 dimensions = uint2(g_shadowMapSize.x, g_shadowMapSize.y);
    OrthographicRay orthoRay = CreateOrthographicRay(pixelCoord, dimensions);

    //==========================================================================
    // STEP 2: SETUP RAY DESCRIPTOR
    //==========================================================================
    RayDesc ray;
    ray.Origin = orthoRay.origin;
    ray.Direction = orthoRay.direction;
    ray.TMin = g_shadowBias;     // Avoid self-intersection
    ray.TMax = 500.0;            // Max distance to check for occluders

    //==========================================================================
    // STEP 3: INSTANTIATE RAYQUERY OBJECT
    //==========================================================================
    // Template Parameters:
    //   RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH: Shadow ray optimization
    //   RAY_FLAG_SKIP_CLOSEST_HIT_SHADER: No hit shader needed (inline)
    //   RAY_FLAG_CULL_BACK_FACING_TRIANGLES: Optional culling
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
             RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;

    //==========================================================================
    // STEP 4: TRACE RAY INLINE
    //==========================================================================
    // TraceRayInline Parameters:
    //   1. Acceleration Structure (TLAS)
    //   2. Ray Flags (redundant with template, but explicit)
    //   3. Instance Inclusion Mask (0xFF = all instances)
    //   4. Ray Descriptor
    query.TraceRayInline(
        g_scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,  // Instance mask: include all instances
        ray
    );

    //==========================================================================
    // STEP 5: TRAVERSE ACCELERATION STRUCTURE
    //==========================================================================
    // Proceed() returns true if there are candidate intersections to process
    // For shadow rays with ACCEPT_FIRST_HIT_AND_END_SEARCH, this typically
    // completes immediately after first hit
    while (query.Proceed()) {
        // For triangle geometry, no manual intersection handling needed
        // For procedural geometry, would call query.CommitProceduralPrimitiveHit()
    }

    //==========================================================================
    // STEP 6: CHECK COMMITTED HIT STATUS
    //==========================================================================
    // CommittedStatus() returns:
    //   COMMITTED_NOTHING: Ray missed all geometry (fully lit)
    //   COMMITTED_TRIANGLE_HIT: Ray hit triangle (shadowed)
    //   COMMITTED_PROCEDURAL_PRIMITIVE_HIT: Ray hit procedural (shadowed)
    uint committedStatus = query.CommittedStatus();

    // Visibility calculation:
    //   COMMITTED_NOTHING (0) = ray missed = 1.0 (lit)
    //   Any hit = 0.0 (shadow)
    float visibility = (committedStatus == COMMITTED_NOTHING) ? 1.0 : 0.0;

    // Optional: Debug info (enable if needed)
    // float rayT = query.CommittedRayT();  // Distance to hit
    // uint instanceIndex = query.CommittedInstanceIndex();  // Hit instance

    //==========================================================================
    // STEP 7: WRITE TO SHADOW MAP
    //==========================================================================
    g_shadowMap[pixelCoord] = visibility;
}

//==============================================================================
// ALTERNATIVE IMPLEMENTATIONS (OPTIONAL)
//==============================================================================

// VARIANT 1: Early-exit optimization (no Proceed loop needed for shadow rays)
#if 0
[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void CSMain_EarlyExit(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= uint(g_shadowMapSize.x) ||
        pixelCoord.y >= uint(g_shadowMapSize.y)) {
        return;
    }

    uint2 dimensions = uint2(g_shadowMapSize.x, g_shadowMapSize.y);
    OrthographicRay orthoRay = CreateOrthographicRay(pixelCoord, dimensions);

    RayDesc ray;
    ray.Origin = orthoRay.origin;
    ray.Direction = orthoRay.direction;
    ray.TMin = g_shadowBias;
    ray.TMax = 500.0;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(g_scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, ray);

    // With ACCEPT_FIRST_HIT_AND_END_SEARCH, Proceed() returns immediately
    query.Proceed();  // Single call sufficient for shadow rays

    float visibility = (query.CommittedStatus() == COMMITTED_NOTHING) ? 1.0 : 0.0;
    g_shadowMap[pixelCoord] = visibility;
}
#endif

// VARIANT 2: Soft shadows with multiple samples (future extension)
#if 0
[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void CSMain_SoftShadows(uint3 dispatchThreadID : SV_DispatchThreadID) {
    // Sample multiple rays with light area sampling
    // Average visibility for soft shadow penumbra
    // Requires random number generator and light area parameters
}
#endif