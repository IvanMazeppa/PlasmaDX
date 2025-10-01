// Density fill compute shader - generates analytic density field
// Creates a noisy sphere/torus pattern that animates over time

RWTexture3D<float> g_density : register(u0);

cbuffer Constants : register(b0) {
    float g_time;
    float g_scale;
    float g_centerX;
    float g_centerY;
};

// Simple hash function for noise
float hash(float3 p) {
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// 3D value noise
float noise3D(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    return lerp(
        lerp(
            lerp(hash(i + float3(0, 0, 0)), hash(i + float3(1, 0, 0)), f.x),
            lerp(hash(i + float3(0, 1, 0)), hash(i + float3(1, 1, 0)), f.x),
            f.y
        ),
        lerp(
            lerp(hash(i + float3(0, 0, 1)), hash(i + float3(1, 0, 1)), f.x),
            lerp(hash(i + float3(0, 1, 1)), hash(i + float3(1, 1, 1)), f.x),
            f.y
        ),
        f.z
    );
}

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID) {
    uint3 dims;
    g_density.GetDimensions(dims.x, dims.y, dims.z);

    if (any(id >= dims)) {
        return;
    }

    // Normalized coordinates [0, 1]
    float3 uvw = (float3(id) + 0.5) / float3(dims);

    // Center in volume
    float3 center = float3(g_centerX, 0.5, g_centerY);
    float3 pos = uvw - center;

    // Animated torus parameters
    float time = g_time * 0.3;
    float majorRadius = 0.3 + 0.1 * sin(time);
    float minorRadius = 0.15 + 0.05 * cos(time * 1.3);

    // Torus distance field
    float2 q = float2(length(pos.xz) - majorRadius, pos.y);
    float torusDist = length(q) - minorRadius;

    // Sphere for additional detail
    float sphereDist = length(pos) - 0.25;

    // Combine shapes with smooth min
    float k = 0.1;
    float h = clamp(0.5 + 0.5 * (sphereDist - torusDist) / k, 0.0, 1.0);
    float dist = lerp(sphereDist, torusDist, h) - k * h * (1.0 - h);

    // Convert distance to density
    float density = saturate(1.0 - dist * 4.0);

    // Add noise for variation
    float3 noisePos = uvw * 8.0 + float3(0, time * 0.5, 0);
    float n = noise3D(noisePos) * 0.3 + noise3D(noisePos * 2.1) * 0.15;
    density = saturate(density + n * density);

    // Apply scale
    density *= g_scale;

    // Write to 3D texture
    g_density[id] = density;
}