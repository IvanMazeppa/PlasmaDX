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

    // Map pixel to world space position
    // For now, use a simple orthographic projection from light's POV
    // Assume scene is centered at origin, covers -100 to +100 in XZ
    float2 uv = (float2(pixelCoord) + 0.5) / float2(dimensions);
    uv = uv * 2.0 - 1.0;  // Map to [-1, 1]

    // Create ray origin above the scene, pointing down along light direction
    float3 rayOrigin = float3(uv.x * 100.0, 200.0, uv.y * 100.0);
    float3 rayDir = normalize(g_lightDirection);

    // Trace shadow ray
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin = g_shadowBias;
    ray.TMax = 500.0;  // Max distance to check for occluders

    ShadowPayload payload;
    payload.visibility = 1.0;  // Assume lit unless hit

    TraceRay(
        g_scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |  // Shadow ray optimization
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,            // We only need miss/any-hit
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

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attrib) {
    // Ray hit geometry - in shadow
    payload.visibility = 0.0;
    // Accept hit and end search (already set via ray flags)
}
