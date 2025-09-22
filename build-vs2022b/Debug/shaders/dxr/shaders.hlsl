// Combined DXR shaders for PlasmaDX
// This file contains all ray tracing shaders in one compilation unit

struct RayPayload {
    float4 color;
};

struct Attributes {
    float2 barycentrics;
};

// Global root signature resources
RaytracingAccelerationStructure g_accel : register(t0);
RWTexture2D<float4> g_output : register(u0);

[shader("raygeneration")]
void RayGen() {
    // Get dispatch dimensions and pixel ID
    uint3 dispatchDims = DispatchRaysDimensions();
    uint3 dispatchIdx = DispatchRaysIndex();

    // Calculate UV coordinates
    float2 uv = float2(dispatchIdx.xy) / float2(dispatchDims.xy);

    // Setup ray from camera
    RayDesc ray;
    float aspectRatio = float(dispatchDims.x) / float(dispatchDims.y);

    // Simple perspective projection
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y

    ray.Origin = float3(0, 0, -2);
    ray.Direction = normalize(float3(ndc.x * aspectRatio, ndc.y, 1.0));
    ray.TMin = 0.001;
    ray.TMax = 1000.0;

    // Trace ray
    RayPayload payload;
    payload.color = float4(0, 0, 0, 1);

    TraceRay(
        g_accel,
        RAY_FLAG_NONE,
        0xFF,  // Instance mask
        0,     // RayContributionToHitGroupIndex
        1,     // MultiplierForGeometryContributionToHitGroupIndex
        0,     // MissShaderIndex
        ray,
        payload);

    // Write result
    g_output[dispatchIdx.xy] = payload.color;
}

[shader("miss")]
void Miss(inout RayPayload payload) {
    // Dark blue gradient background based on ray direction
    float t = WorldRayDirection().y * 0.5 + 0.5;
    payload.color = float4(0.02, 0.02 + t * 0.02, 0.04 + t * 0.06, 1.0);
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in Attributes attrib) {
    // Calculate barycentric coordinates
    float3 barycentrics = float3(
        1.0 - attrib.barycentrics.x - attrib.barycentrics.y,
        attrib.barycentrics.x,
        attrib.barycentrics.y);

    // Use barycentric coordinates for vertex colors (RGB triangle)
    payload.color = float4(barycentrics, 1.0);
}