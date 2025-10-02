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
struct Particle
{
    float3 position;
    float3 velocity;
    float3 color;
    float temperature;
    float mass;
    float lifetime;
    float _pad0;
    float _pad1;
};

StructuredBuffer<Particle> particles : register(t0);

// Input: Emission grid (output from emission_grid_build.hlsl)
// Reading as ByteAddressBuffer to match UAV type
ByteAddressBuffer emissionGrid : register(t1);

// Output: Lighting contribution per particle (RGB = additive light color, A = unused)
RWStructuredBuffer<float4> particleLighting : register(u0);

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
    int3 coord = (int3)(normalized * gridResolution);
    return coord;
}

// Helper: Sample emission from grid with bounds checking
float3 SampleGrid(int3 coord)
{
    // Bounds check
    if (any(coord < 0) || any(coord >= (int)gridResolution))
        return float3(0, 0, 0);

    uint index = GridCoordToIndex((uint3)coord);
    uint baseAddr = index * 16;  // 16 bytes per float4

    // Read float4 from ByteAddressBuffer
    float4 cell;
    cell.x = asfloat(emissionGrid.Load(baseAddr + 0));
    cell.y = asfloat(emissionGrid.Load(baseAddr + 4));
    cell.z = asfloat(emissionGrid.Load(baseAddr + 8));
    cell.w = asfloat(emissionGrid.Load(baseAddr + 12));

    // Normalize by particle count to get average emission in cell
    if (cell.w > 0.0)
        return cell.rgb / cell.w;
    else
        return float3(0, 0, 0);
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

    // Normalize and apply global strength
    if (totalWeight > 0.0)
        totalLight = (totalLight / totalWeight) * lightingStrength;

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