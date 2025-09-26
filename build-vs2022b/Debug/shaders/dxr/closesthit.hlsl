// Closest hit shader - executed when ray hits geometry
// Returns a simple color based on hit attributes

struct RayPayload {
    float4 color;
};

struct Attributes {
    float2 barycentrics;
};

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in Attributes attrib) {
    // Calculate barycentric coordinates
    float3 barycentrics = float3(
        1.0 - attrib.barycentrics.x - attrib.barycentrics.y,
        attrib.barycentrics.x,
        attrib.barycentrics.y);

    // Use barycentric coordinates for color (RGB triangle)
    payload.color = float4(barycentrics, 1.0);
}