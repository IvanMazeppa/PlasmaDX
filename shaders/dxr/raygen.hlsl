// Raygen shader - entry point for each pixel
// Outputs a gradient to verify DXR pipeline is working

struct RayPayload {
    float4 color;
};

// Global root signature
RaytracingAccelerationStructure g_accel : register(t0);
RWTexture2D<float4> g_output : register(u0);

[shader("raygeneration")]
void RayGen() {
    // Get dispatch dimensions and pixel ID
    uint3 dispatchDims = DispatchRaysDimensions();
    uint3 dispatchIdx = DispatchRaysIndex();

    // Calculate UV coordinates
    float2 uv = float2(dispatchIdx.xy) / float2(dispatchDims.xy);

    // Test: Distinctive green/red checkboard pattern to verify raygen is running
    bool checkX = (dispatchIdx.x / 32) % 2 == 0;
    bool checkY = (dispatchIdx.y / 32) % 2 == 0;
    float4 color = (checkX ^ checkY) ? float4(0.0, 1.0, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);

    // Trace a ray for testing (optional, can be enabled later)
    if (false) {  // Disabled for initial test
        // Setup ray
        RayDesc ray;
        ray.Origin = float3(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, -1.0);
        ray.Direction = float3(0, 0, 1);
        ray.TMin = 0.001;
        ray.TMax = 1000.0;

        // Trace
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

        color = payload.color;
    }

    // Write to output
    g_output[dispatchIdx.xy] = color;
}