// Mode 9.2 Milestone 2: Emission Grid Builder
// Reads particle buffer directly and accumulates emission data into 3D spatial grid
// This allows efficient spatial queries for particle-to-particle lighting

cbuffer GridConstants : register(b0)
{
    uint particleCount;         // Number of particles
    uint gridResolution;        // Grid cells per axis (e.g., 16 for 16^3 grid)
    float worldRadius;          // World space radius (e.g., 20.0 for [-20,20] bounds)
    float emissionThreshold;    // Temperature threshold for emission (e.g., 10000K)
};

// Input: Particle buffer (direct access to 3D positions)
// Particle structure matching ParticleSystem layout (32 bytes + padding to 64)
// CRITICAL: Must match src/particles/ParticleSystem.h Particle struct exactly
struct Particle
{
    float3 position;    // Offset 0-11
    float temperature;  // Offset 12-15
    float3 velocity;    // Offset 16-27
    float density;      // Offset 28-31
    // Note: HLSL pads to 64 bytes for structured buffer alignment
};

StructuredBuffer<Particle> particles : register(t0);

// Output: 3D spatial grid accumulating emission color + particle count
// Using RWStructuredBuffer<uint> for working atomic operations (int-based)
RWStructuredBuffer<uint> emissionGrid : register(u0);

// Helper: Convert 3D grid coordinates to linear buffer index
uint GridCoordToIndex(uint3 coord)
{
    return coord.z * gridResolution * gridResolution + coord.y * gridResolution + coord.x;
}

// Helper: Convert world position to grid coordinates
uint3 WorldPosToGridCoord(float3 worldPos)
{
    // Map from [-worldRadius, +worldRadius] to [0, gridResolution-1]
    float3 normalized = (worldPos + worldRadius) / (2.0 * worldRadius);
    uint3 coord = (uint3)(saturate(normalized) * (gridResolution - 1));
    // Clamp to valid range [0, gridResolution-1]
    coord = min(coord, uint3(gridResolution - 1, gridResolution - 1, gridResolution - 1));
    return coord;
}

// Helper: Calculate emission color from temperature
float3 TemperatureToEmissionColor(float temperature)
{
    // Normalize temperature to 0-1 range (800K to 26000K)
    float t = saturate((temperature - 800.0) / 25200.0);

    // Same color gradient as particle rendering
    float3 color;
    if (t < 0.25) {
        float blend = t / 0.25;
        color = lerp(float3(0.5, 0.1, 0.05), float3(1.0, 0.3, 0.1), blend);
    } else if (t < 0.5) {
        float blend = (t - 0.25) / 0.25;
        color = lerp(float3(1.0, 0.3, 0.1), float3(1.0, 0.6, 0.2), blend);
    } else if (t < 0.75) {
        float blend = (t - 0.5) / 0.25;
        color = lerp(float3(1.0, 0.6, 0.2), float3(1.0, 0.95, 0.7), blend);
    } else {
        float blend = (t - 0.75) / 0.25;
        color = lerp(float3(1.0, 0.95, 0.7), float3(1.0, 1.0, 1.0), blend);
    }
    return color;
}

// Helper: Atomic integer addition (converted from float)
// Store as fixed-point integer (multiply by 256) to allow atomic operations
// This avoids GPU timeout from float atomic compare-exchange loops
void AtomicAddInt(uint index, float value)
{
    // Convert float to fixed-point integer (8.24 format: 256 = 1.0)
    int intValue = int(value * 256.0);
    // CRITICAL FIX: Don't capture originalValue - causes dead code elimination
    InterlockedAdd(emissionGrid[index], intValue);  // 2-parameter version
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint particleIndex = dispatchThreadID.x;

    // DIAGNOSTIC: First thread writes test pattern to verify shader execution
    if (particleIndex == 0) {
        emissionGrid[0] = asuint(999.0f * 256.0f);   // Cell[0].R
        emissionGrid[1] = asuint(888.0f * 256.0f);   // Cell[0].G
        emissionGrid[2] = asuint(777.0f * 256.0f);   // Cell[0].B
        emissionGrid[3] = asuint(666.0f * 256.0f);   // Cell[0].Count
    }

    // DIAGNOSTIC: Test atomics on cell[1] - every thread adds 1
    // With 100,000 particles, cell[1] should accumulate to ~25,600,000 (100000 * 256)
    AtomicAddInt(4, 1.0f);  // Cell[1].R (cell 1 starts at index 4)
    AtomicAddInt(5, 1.0f);  // Cell[1].G
    AtomicAddInt(6, 1.0f);  // Cell[1].B
    AtomicAddInt(7, 1.0f);  // Cell[1].Count

    // Early exit if outside particle count
    if (particleIndex >= particleCount)
        return;

    // Read particle data
    Particle p = particles[particleIndex];

    // DIAGNOSTIC: Force ALL particles to emit light (bypass temperature check entirely)
    // Original check: if (p.temperature < emissionThreshold) return;
    // Temporarily disabled to diagnose zero-emission issue

    // DIAGNOSTIC: Force constant emission strength to verify pipeline works
    float emissionStrength = 10.0;  // Constant bright emission for all particles

    // DIAGNOSTIC: Force bright white emission color
    float3 emissionColor = float3(1.0, 1.0, 1.0);  // Pure white for visibility

    // Convert particle world position to grid coordinates
    uint3 gridCoord = WorldPosToGridCoord(p.position);
    uint gridIndex = GridCoordToIndex(gridCoord);

    // Accumulate emission into grid cell using integer atomics
    // Each cell is 4 uints storing fixed-point floats (RGBCount)
    uint baseIndex = gridIndex * 4;  // 4 uints per cell

    // Emission color weighted by intensity
    float3 weightedEmission = emissionColor * emissionStrength;

    // Atomic integer add using RWStructuredBuffer<uint>
    AtomicAddInt(baseIndex + 0, weightedEmission.x);
    AtomicAddInt(baseIndex + 1, weightedEmission.y);
    AtomicAddInt(baseIndex + 2, weightedEmission.z);
    AtomicAddInt(baseIndex + 3, emissionStrength);
}