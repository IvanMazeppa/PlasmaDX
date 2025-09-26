// DXR Raytracing Library - Complete shader for offline compilation
// Target: lib_6_3 (DXR shader model)

struct RayPayload {
    float4 color;
};

// Global resources
RaytracingAccelerationStructure g_scene : register(t0);
RWTexture2D<float4> g_output : register(u0);

[shader("raygeneration")]
void RayGen() {
    uint2 index = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;

    // Calculate UV coordinates
    float2 uv = float2(index) / float2(dimensions);

    // Setup ray for perspective projection
    RayDesc ray;
    float aspectRatio = float(dimensions.x) / float(dimensions.y);

    // Camera position and direction
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y for screen space

    ray.Origin = float3(0, 0, -3);
    ray.Direction = normalize(float3(ndc.x * aspectRatio, ndc.y, 1.0));
    ray.TMin = 0.001;
    ray.TMax = 1000.0;

    // Initialize payload
    RayPayload payload;
    payload.color = float4(uv, 0.5, 1.0);  // Default gradient

    // Trace ray against scene
    TraceRay(
        g_scene,
        RAY_FLAG_NONE,
        0xFF,        // Instance mask
        0,           // RayContributionToHitGroupIndex
        1,           // MultiplierForGeometryContributionToHitGroupIndex
        0,           // MissShaderIndex
        ray,
        payload);

    // Write result to output texture
    g_output[index] = payload.color;
}

[shader("miss")]
void Miss(inout RayPayload payload) {
    // Create a beautiful sky gradient based on ray direction
    float3 direction = WorldRayDirection();
    float t = 0.5 * (direction.y + 1.0);

    // Blue to light blue gradient
    float3 topColor = float3(0.5, 0.7, 1.0);
    float3 bottomColor = float3(0.1, 0.2, 0.4);
    float3 skyColor = lerp(bottomColor, topColor, t);

    payload.color = float4(skyColor, 1.0);
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    // Calculate barycentric coordinates
    float3 barycentrics = float3(
        1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y);

    // Create colorful RGB triangle using barycentric coordinates
    payload.color = float4(barycentrics, 1.0);
}