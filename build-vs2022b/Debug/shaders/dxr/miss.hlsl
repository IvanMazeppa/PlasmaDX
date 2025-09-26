// Miss shader - executed when ray doesn't hit any geometry
// Returns a dark background color

struct RayPayload {
    float4 color;
};

[shader("miss")]
void Miss(inout RayPayload payload) {
    // Dark blue background
    payload.color = float4(0.02, 0.02, 0.04, 1.0);
}