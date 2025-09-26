// Debug slice compute shader - renders a Z-slice of the density volume to HDR
// Visualizes the density field for validation

Texture3D<float> g_density : register(t0);
RWTexture2D<float4> g_hdrTarget : register(u0);

cbuffer Constants : register(b0) {
    uint g_sliceZ;
    uint g_volumeDim;
};

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint2 dims;
    g_hdrTarget.GetDimensions(dims.x, dims.y);

    if (any(id.xy >= dims)) {
        return;
    }

    // Map screen coordinates to volume coordinates
    float2 uv = float2(id.xy) / float2(dims);

    // Sample from the specified Z slice
    uint3 samplePos = uint3(
        uv.x * g_volumeDim,
        uv.y * g_volumeDim,
        min(g_sliceZ, g_volumeDim - 1)
    );

    float density = g_density[samplePos];

    // Color mapping: density to heat map
    float3 color;
    if (density < 0.25) {
        // Black to blue
        float t = density * 4.0;
        color = float3(0, 0, t);
    } else if (density < 0.5) {
        // Blue to cyan
        float t = (density - 0.25) * 4.0;
        color = float3(0, t, 1);
    } else if (density < 0.75) {
        // Cyan to yellow
        float t = (density - 0.5) * 4.0;
        color = float3(t, 1, 1 - t);
    } else {
        // Yellow to red
        float t = (density - 0.75) * 4.0;
        color = float3(1, 1 - t * 0.5, 0);
    }

    // Boost brightness for HDR
    color *= 2.0;

    // Write to HDR target (additive blend for now)
    float4 existing = g_hdrTarget[id.xy];
    g_hdrTarget[id.xy] = float4(color + existing.rgb * 0.1, 1.0);
}