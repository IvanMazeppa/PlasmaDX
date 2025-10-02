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

// Output: 3D spatial grid accumulating emission color + particle count
// Using RWByteAddressBuffer for atomic float operations
RWByteAddressBuffer emissionGrid : register(u0);

// Helper: Convert 3D grid coordinates to linear buffer index
uint GridCoordToIndex(uint3 coord)
{
    return coord.z * gridResolution * gridResolution + coord.y * gridResolution + coord.x;
}

// Helper: Convert world position to grid coordinates
uint3 WorldPosToGridCoord(float3 worldPos)
{
    // Map from [-worldRadius, +worldRadius] to [0, gridResolution]
    float3 normalized = (worldPos + worldRadius) / (2.0 * worldRadius);
    uint3 coord = (uint3)(saturate(normalized) * gridResolution);
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
void AtomicAddInt(RWByteAddressBuffer buffer, uint address, float value)
{
    // Convert float to fixed-point integer (8.24 format: 256 = 1.0)
    int intValue = int(value * 256.0);
    buffer.InterlockedAdd(address, intValue);
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint particleIndex = dispatchThreadID.x;

    // Early exit if outside particle count
    if (particleIndex >= particleCount)
        return;

    // Read particle data
    Particle p = particles[particleIndex];

    // Calculate emission strength based on temperature
    // Only hot particles (>emissionThreshold) emit light
    if (p.temperature < emissionThreshold)
        return;

    // Calculate emission strength (exponential above threshold)
    float normalizedTemp = saturate((p.temperature - emissionThreshold) / (26000.0 - emissionThreshold));
    float emissionStrength = pow(normalizedTemp, 1.2) * 10.0;  // Stronger emission, less falloff (was 1.5, 3.0 - DIAGNOSTIC FIX)

    // Get emission color from temperature
    float3 emissionColor = TemperatureToEmissionColor(p.temperature);

    // Convert particle world position to grid coordinates
    uint3 gridCoord = WorldPosToGridCoord(p.position);
    uint gridIndex = GridCoordToIndex(gridCoord);

    // Accumulate emission into grid cell using integer atomics
    // Each cell is 16 bytes (4 ints storing fixed-point floats)
    uint baseAddr = gridIndex * 16;

    // Emission color weighted by intensity
    float3 weightedEmission = emissionColor * emissionStrength;

    // Atomic integer add (hardware-optimized, no infinite loop)
    AtomicAddInt(emissionGrid, baseAddr + 0, weightedEmission.x);
    AtomicAddInt(emissionGrid, baseAddr + 4, weightedEmission.y);
    AtomicAddInt(emissionGrid, baseAddr + 8, weightedEmission.z);
    AtomicAddInt(emissionGrid, baseAddr + 12, emissionStrength);
}