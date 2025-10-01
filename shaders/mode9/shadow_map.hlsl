// Mode 9.1: Directional Shadow Map Generation
// Simple raygen shader that casts shadow rays from light direction
// Output: R16_FLOAT shadow map (1.0 = lit, 0.0 = shadow)

struct ShadowPayload {
    float visibility;  // 1.0 = hit nothing (lit), 0.0 = hit something (shadow)
};

// Global resources
RaytracingAccelerationStructure g_scene : register(t0);
RWTexture2D<float> g_shadowMap : register(u0);  // Shadow map output (1024x1024)

// Shadow parameters via root constants
cbuffer ShadowParams : register(b0) {
    float3 g_lightDirection;  // Directional light (normalized)
    float g_shadowBias;
    float2 g_shadowMapSize;   // 1024, 1024
    float2 g_padding;
};

[shader("raygeneration")]
void ShadowRayGen() {
    uint2 pixelCoord = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;

    // Map pixel to world space position (orthographic projection from light POV)
    // Shadow map covers -100 to +100 in XZ plane
    float2 uv = (float2(pixelCoord) + 0.5) / float2(dimensions);
    uv = uv * 2.0 - 1.0;  // Map to [-1, 1]

    // Create orthographic plane perpendicular to light direction
    // Ray origin is BEHIND the scene (opposite of light direction), rays shoot TOWARD light
    float3 rayDir = normalize(g_lightDirection);

    // Create orthographic basis (perpendicular to light direction)
    float3 up = abs(rayDir.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, rayDir));
    up = cross(rayDir, right);

    // Position ray origin on orthographic plane, FAR from scene along -lightDir
    float3 rayOrigin = float3(0, 0, 0) + right * uv.x * 100.0 + up * uv.y * 100.0 - rayDir * 300.0;

    // Trace shadow ray
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin = g_shadowBias;
    ray.TMax = 500.0;  // Max distance to check for occluders

    ShadowPayload payload;
    payload.visibility = 0.0;  // Assume shadow unless miss shader runs (GREEN DIAMOND approach)

    TraceRay(
        g_scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,  // Shadow ray optimization
        0xFF,  // Instance inclusion mask
        0,     // Ray contribution to hit group index
        0,     // Multiplier for geometry contribution
        0,     // Miss shader index
        ray,
        payload
    );

    // Write visibility to shadow map
    g_shadowMap[pixelCoord] = payload.visibility;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload) {
    // Ray didn't hit anything - fully lit
    payload.visibility = 1.0;
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attrib) {
    // This shader is skipped by RAY_FLAG_SKIP_CLOSEST_HIT_SHADER
    // But D3D12 requires it in the hit group definition
    payload.visibility = 0.0;
}

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attrib) {
    // Ray hit geometry - in shadow
    payload.visibility = 0.0;
    // Accept hit and end search (already set via ray flags)
}
