// Metaball Density Fill Compute Shader
// Evaluates multiple metaballs to create lava lamp-like volumetric shapes
// Uses smooth blending and temperature-based effects

RWTexture3D<float4> g_density : register(u0);  // RGBA: density, temperature, color.r, color.g

// Metaball constants
cbuffer MetaballConstants : register(b0)
{
    uint   g_numMetaballs;
    float  g_time;
    float  g_deltaTime;
    float  g_containerRadius;

    float3 g_gravity;
    float  g_buoyancyStrength;

    float3 g_containerCenter;
    float  g_viscosity;

    float  g_mergeDistance;
    float  g_splitThreshold;
    float  g_noiseStrength;
    float  g_padding;
};

// Metaball data structure (matches CPU side)
struct MetaballData
{
    float3 position;
    float  radius;
    float3 velocity;
    float  temperature;
    float3 color;
    float  mass;
};

StructuredBuffer<MetaballData> g_metaballs : register(t0);

// Smooth falloff function for metaballs (creates nice blending)
float EvaluateMetaball(float3 worldPos, MetaballData metaball)
{
    float3 offset = worldPos - metaball.position;
    float distance = length(offset);

    // Smooth falloff using smoothstep for natural blending
    float normalizedDist = distance / metaball.radius;

    if (normalizedDist > 1.0)
        return 0.0;

    // Use smooth falloff - this creates natural metaball blending
    // The formula: (1 - d²)³ gives nice blob shapes that merge smoothly
    float falloff = 1.0 - normalizedDist;
    return falloff * falloff * falloff * falloff; // Quartic falloff for smooth blending
}

// Add noise for organic motion and detail
float3 ApplyNoise(float3 worldPos, float time)
{
    // Multi-octave noise for organic variation
    float3 noise = 0.0.xxx;

    // Large scale motion
    noise.x += sin(worldPos.y * 3.0 + time * 0.8) * 0.02;
    noise.y += sin(worldPos.x * 2.5 + time * 1.2) * 0.02;
    noise.z += sin(worldPos.z * 3.5 + time * 0.9) * 0.02;

    // Medium scale variation
    noise.x += sin(worldPos.y * 8.0 + time * 1.5) * 0.01;
    noise.y += sin(worldPos.x * 7.0 + time * 1.8) * 0.01;
    noise.z += sin(worldPos.z * 9.0 + time * 1.3) * 0.01;

    return noise * g_noiseStrength;
}

// Convert temperature to plasma color
float3 TemperatureToColor(float temperature, float3 baseColor)
{
    // Hot = more yellow/white, Cold = more red/purple
    float heat = saturate(temperature);

    float3 hotColor = float3(1.0, 1.0, 0.8);    // Warm white/yellow
    float3 coldColor = float3(1.0, 0.3, 0.6);   // Purple/magenta

    float3 tempColor = lerp(coldColor, hotColor, heat);
    return lerp(baseColor, tempColor, 0.5); // Blend with base metaball color
}

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    // Get volume dimensions
    uint3 dim;
    g_density.GetDimensions(dim.x, dim.y, dim.z);
    if (any(id >= dim)) return;

    // Convert voxel ID to world space coordinates [-1, 1]
    float3 uvw = (float3(id) + 0.5) / float3(dim);
    float3 worldPos = uvw * 2.0 - 1.0; // UVW [0,1] -> World [-1,1]

    // Add noise for organic motion
    worldPos += ApplyNoise(worldPos, g_time);

    // Evaluate all metaballs at this position
    float totalDensity = 0.0;
    float totalTemperature = 0.0;
    float3 totalColor = 0.0.xxx;
    float totalWeight = 0.0;

    [loop]
    for (uint i = 0; i < g_numMetaballs; ++i)
    {
        MetaballData metaball = g_metaballs[i];

        // Evaluate density contribution from this metaball
        float density = EvaluateMetaball(worldPos, metaball);

        if (density > 0.001) // Only process non-zero contributions
        {
            totalDensity += density;

            // Weight temperature and color by density contribution
            float weight = density;
            totalTemperature += metaball.temperature * weight;

            // Generate plasma color based on temperature
            float3 plasmaColor = TemperatureToColor(metaball.temperature, metaball.color);
            totalColor += plasmaColor * weight;
            totalWeight += weight;
        }
    }

    // Normalize weighted values
    if (totalWeight > 0.001)
    {
        totalTemperature /= totalWeight;
        totalColor /= totalWeight;
    }

    // Apply smooth density threshold to prevent noise
    totalDensity = smoothstep(0.1, 0.3, totalDensity);

    // Clamp values to reasonable ranges
    totalDensity = saturate(totalDensity);
    totalTemperature = saturate(totalTemperature);
    totalColor = saturate(totalColor);

    // Store results: RGB = color, A = density
    // We'll store temperature in a separate channel if needed
    float4 result = float4(totalColor, totalDensity);

    // Add some glow effect based on temperature
    if (totalDensity > 0.1)
    {
        float glow = totalTemperature * 0.3; // Hot areas glow more
        result.rgb += glow * float3(1.0, 0.8, 0.4); // Warm glow color
    }

    g_density[id] = result;
}