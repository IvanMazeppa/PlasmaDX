// VOL_0003B: Analytic sphere density fill (compute)
// Writes a solid sphere into the 3D density volume in UVW space

RWTexture3D<float> g_density : register(u0);

cbuffer SphereParams : register(b0)
{
    float3 g_centerUVW;  // Center in [0,1]
    float  g_radiusUVW;  // Radius in [0,1]
    float  g_densityValue; // Density value to write inside sphere
    float3 g_pad;        // 16-byte alignment
};

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint3 dim;
    g_density.GetDimensions(dim.x, dim.y, dim.z);
    if (any(id >= dim)) return;

    float3 uvw = (float3(id) + 0.5) / float3(dim);
    float d = distance(uvw, g_centerUVW);
    float inside = d <= g_radiusUVW ? 1.0 : 0.0;

    // Write density (solid sphere)
    g_density[id] = g_densityValue * inside;
}


