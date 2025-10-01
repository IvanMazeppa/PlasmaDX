// Mode 9.1: Directional Shadow Map Generation (DXR 1.1 Inline Ray Tracing)
// RayQuery compute shader - replaces DispatchRays raygeneration shader
// Output: R16_FLOAT shadow map (1.0 = lit, 0.0 = shadow)

// Resources (same layout as old DispatchRays version)
RaytracingAccelerationStructure g_scene : register(t0);
RWTexture2D<float> g_shadowMap : register(u0);

cbuffer ShadowParams : register(b0) {
    float3 g_lightDirection;
    float g_shadowBias;
    float2 g_shadowMapSize;
    float2 g_padding;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;

    // Bounds check (thread groups may exceed shadow map dimensions)
    if (pixelCoord.x >= (uint)g_shadowMapSize.x || pixelCoord.y >= (uint)g_shadowMapSize.y) {
        return;
    }

    // Map pixel to world space (orthographic projection from light POV)
    float2 uv = (float2(pixelCoord) + 0.5) / g_shadowMapSize;
    uv = uv * 2.0 - 1.0;

    // Create orthographic basis perpendicular to light direction
    float3 rayDir = normalize(g_lightDirection);
    float3 up = abs(rayDir.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, rayDir));
    up = cross(rayDir, right);

    // Position ray origin on orthographic plane, far from scene
    float3 rayOrigin = float3(0, 0, 0) + right * uv.x * 100.0 + up * uv.y * 100.0 - rayDir * 300.0;

    // Configure shadow ray
    RayDesc shadowRay;
    shadowRay.Origin = rayOrigin;
    shadowRay.Direction = rayDir;
    shadowRay.TMin = g_shadowBias;
    shadowRay.TMax = 500.0;

    // Inline ray tracing (DXR 1.1+)
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE> shadowQuery;
    shadowQuery.TraceRayInline(g_scene, 0, 0xFF, shadowRay);

    // Process query - default to shadow (0.0)
    float visibility = 0.0;
    while (shadowQuery.Proceed()) {
        // RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH handles early termination
    }

    // Check committed state
    if (shadowQuery.CommittedStatus() == COMMITTED_NOTHING) {
        visibility = 1.0; // No hit - fully lit
    }

    g_shadowMap[pixelCoord] = visibility;
}