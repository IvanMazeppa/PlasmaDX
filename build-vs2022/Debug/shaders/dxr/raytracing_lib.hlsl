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

    // Setup simple camera ray (procedural)
    float aspectRatio = float(dimensions.x) / float(dimensions.y);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float3 ro = float3(0, 0, -3);
    float3 rd = normalize(float3(ndc.x * aspectRatio, ndc.y, 1.0));

    // Intersect ray with a unit box centered at origin
    float3 bmin = float3(-1, -1, -1);
    float3 bmax = float3( 1,  1,  1);
    float3 invD = 1.0 / rd;
    float3 t0 = (bmin - ro) * invD;
    float3 t1 = (bmax - ro) * invD;
    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);
    float tN = max(max(tmin.x, tmin.y), tmin.z);
    float tF = min(min(tmax.x, tmax.y), tmax.z);

    float3 color = float3(0,0,0);
    if (tF >= max(tN, 0.0)) {
        float t = max(tN, 0.0);
        float3 p = ro + rd * t;
        // Estimate normal by which slab contributed
        float3 n = 0;
        if (abs(p.x - bmin.x) < 1e-3) n = float3(-1,0,0);
        else if (abs(p.x - bmax.x) < 1e-3) n = float3(1,0,0);
        else if (abs(p.y - bmin.y) < 1e-3) n = float3(0,-1,0);
        else if (abs(p.y - bmax.y) < 1e-3) n = float3(0,1,0);
        else if (abs(p.z - bmin.z) < 1e-3) n = float3(0,0,-1);
        else n = float3(0,0,1);

        float3 L = normalize(float3(0.3, 0.8, 0.2));
        float diff = max(0.0, dot(n, L));
        color = diff * float3(1.0, 0.95, 0.85);
        // Add a subtle normal-based tint for readability
        color *= 0.7 + 0.3 * abs(n);
    }

    g_output[index] = float4(color, 1.0);
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