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

    // MCP Librarian Debug: Add bounds checking and coordinate verification
    uint2 launchIndex = dispatchIdx.xy;
    uint2 launchDim = dispatchDims.xy;

    // Debug: Color code based on position to verify coordinate system
    float2 uv = float2(launchIndex) / float2(launchDim);
    float4 color = float4(uv.x, uv.y, 1.0, 1.0); // Gradient from black to cyan

    // Bounds check (critical for debugging)
    if (launchIndex.x >= launchDim.x || launchIndex.y >= launchDim.y) {
        color = float4(1.0, 0.0, 0.0, 1.0); // Red for out-of-bounds
        return;
    }

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

    // Write to output using proper coordinate variables
    g_output[launchIndex] = color;
}