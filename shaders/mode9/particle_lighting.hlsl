// Mode 9.2 Milestone 3: Particle Lighting Compute Shader
// Applies emission-based lighting from spatial grid to particles
// Each particle queries nearby grid cells to receive illumination

cbuffer LightingConstants : register(b0)
{
    uint particleCount;         // Number of particles to light
    uint gridResolution;        // Grid cells per axis (must match grid builder)
    float worldRadius;          // World space radius (must match grid builder)
    float lightingStrength;     // Global lighting intensity multiplier (e.g., 1.0)

    float3 cameraPos;           // Camera position for view-dependent effects
    float falloffRadius;        // Distance at which lighting falls to zero (e.g., 3.0)
};

// Input: Particle positions (read from particle system buffer)
// CRITICAL: Must match src/particles/ParticleSystem.h Particle struct exactly (32 bytes)
struct Particle
{
    float3 position;    // Offset 0-11
    float temperature;  // Offset 12-15
    float3 velocity;    // Offset 16-27
    float density;      // Offset 28-31
    // Note: HLSL pads to 64 bytes for structured buffer alignment
};

StructuredBuffer<Particle> particles : register(t0);

// Input: Emission grid (output from emission_grid_build.hlsl)
// Reading as StructuredBuffer<uint> to match UAV type
StructuredBuffer<uint> emissionGrid : register(t1);

// Output: Lighting contribution per particle (RGB = additive light color, A = unused)
// CRITICAL FIX: Use RWBuffer (typed) to match the typed UAV descriptor created in App.cpp
// The buffer UAV is created with StructureByteStride=0, requiring typed buffer access
RWBuffer<float4> particleLighting : register(u0);

// Helper: Convert 3D grid coordinates to linear buffer index
uint GridCoordToIndex(uint3 coord)
{
    return coord.z * gridResolution * gridResolution + coord.y * gridResolution + coord.x;
}

// Helper: Convert world position to grid coordinates
int3 WorldPosToGridCoord(float3 worldPos)
{
    // Map from [-worldRadius, +worldRadius] to [0, gridResolution]
    float3 normalized = (worldPos + worldRadius) / (2.0 * worldRadius);
    int3 coord = (int3)(saturate(normalized) * (gridResolution - 1));
    // Clamp to valid range [0, gridResolution-1]
    coord = clamp(coord, int3(0,0,0), int3(gridResolution-1, gridResolution-1, gridResolution-1));
    return coord;
}

// Helper: Sample emission from grid with bounds checking
float3 SampleGrid(int3 coord)
{
    // Bounds check
    if (any(coord < 0) || any(coord >= (int)gridResolution))
        return float3(0, 0, 0);

    uint index = GridCoordToIndex((uint3)coord);
    uint baseIndex = index * 4;  // 4 uints per cell (RGBCount)

    // Read fixed-point integers and convert to float (divide by 256)
    float4 cell;
    cell.x = float(asint(emissionGrid[baseIndex + 0])) / 256.0;
    cell.y = float(asint(emissionGrid[baseIndex + 1])) / 256.0;
    cell.z = float(asint(emissionGrid[baseIndex + 2])) / 256.0;
    cell.w = float(asint(emissionGrid[baseIndex + 3])) / 256.0;

    // Return total accumulated emission (don't normalize by count)
    // We want brighter regions where more hot particles are concentrated
    return cell.rgb;
}

// Compute lighting contribution from nearby grid cells
float3 ComputeLighting(float3 worldPos)
{
    // Get particle's base grid cell
    int3 baseCoord = WorldPosToGridCoord(worldPos);

    // Accumulate lighting from 3x3x3 neighborhood (27 cells)
    float3 totalLight = float3(0, 0, 0);
    float totalWeight = 0.0;

    // Sample neighborhood cells
    for (int dz = -1; dz <= 1; dz++)
    {
        for (int dy = -1; dy <= 1; dy++)
        {
            for (int dx = -1; dx <= 1; dx++)
            {
                int3 sampleCoord = baseCoord + int3(dx, dy, dz);
                float3 emission = SampleGrid(sampleCoord);

                // Calculate distance weight (inverse square falloff)
                float3 cellCenter = (float3(sampleCoord) + 0.5) / gridResolution * (2.0 * worldRadius) - worldRadius;
                float dist = length(cellCenter - worldPos);
                float weight = 1.0 / (1.0 + dist * dist);

                // Apply falloff radius
                if (dist < falloffRadius)
                {
                    totalLight += emission * weight;
                    totalWeight += weight;
                }
            }
        }
    }

    // Apply global strength (SampleGrid already normalized by particle count)
    if (totalWeight > 0.0)
        totalLight = totalLight * lightingStrength;

    return totalLight;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint particleIndex = dispatchThreadID.x;

    // Early exit if outside particle count
    if (particleIndex >= particleCount)
        return;

    // Read particle world position
    Particle p = particles[particleIndex];
    float3 worldPos = p.position;

    // Compute lighting from emission grid
    float3 lighting = ComputeLighting(worldPos);

    // Write lighting contribution (will be added to particle color in rendering)
    particleLighting[particleIndex] = float4(lighting, 1.0);
}