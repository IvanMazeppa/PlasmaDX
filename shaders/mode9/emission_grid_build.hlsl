// Mode 9.2 Milestone 2: Emission Grid Builder
// Reads emission buffer and accumulates emission data into 3D spatial grid
// This allows efficient spatial queries for particle-to-particle lighting

cbuffer GridConstants : register(b0)
{
    uint2 emissionTexSize;      // Emission buffer dimensions (e.g., 1920x1080)
    uint gridResolution;        // Grid cells per axis (e.g., 64 for 64^3 grid)
    float worldRadius;          // World space radius (e.g., 20.0 for [-20,20] bounds)

    float4x4 viewMatrix;        // Camera view matrix for unprojection
    float4x4 projMatrix;        // Camera projection matrix for unprojection
    float4x4 invViewProj;       // Inverse view-projection for screen-to-world
};

// Input: Emission buffer (R11G11B10_FLOAT) - written by particle pixel shader
Texture2D<float4> emissionBuffer : register(t0);

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

// Helper: Reconstruct world position from screen UV + depth
float3 ScreenToWorld(float2 uv, float depth)
{
    // Convert UV [0,1] to NDC [-1,1]
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y; // Flip Y for DX12

    // Unproject to world space
    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // Early exit if outside emission buffer bounds
    if (dispatchThreadID.x >= emissionTexSize.x || dispatchThreadID.y >= emissionTexSize.y)
        return;

    uint2 pixelCoord = dispatchThreadID.xy;

    // Read emission data from buffer
    float4 emission = emissionBuffer[pixelCoord];

    // Skip if no emission (w channel = emission strength)
    if (emission.w <= 0.0)
        return;

    // Calculate screen UV
    float2 uv = (float2(pixelCoord) + 0.5) / float2(emissionTexSize);

    // Reconstruct world position (assuming depth ~0.5 for mid-depth particles)
    // NOTE: This is approximate - ideally we'd have a depth buffer
    // For now, assume particles are at typical orbital distance
    float depth = 0.5; // Mid-depth approximation
    float3 worldPos = ScreenToWorld(uv, depth);

    // Convert world position to grid coordinates
    uint3 gridCoord = WorldPosToGridCoord(worldPos);
    uint gridIndex = GridCoordToIndex(gridCoord);

    // Accumulate emission into grid cell using atomic operations on uint representation of float
    // Each cell is 16 bytes (4 floats = 4 uints)
    uint baseAddr = gridIndex * 16;  // 16 bytes per float4

    // Atomic add for RGB channels (emission color weighted by intensity)
    float3 weightedEmission = emission.rgb * emission.w;

    uint originalValue;
    emissionGrid.InterlockedAdd(baseAddr + 0, asuint(weightedEmission.x), originalValue);
    emissionGrid.InterlockedAdd(baseAddr + 4, asuint(weightedEmission.y), originalValue);
    emissionGrid.InterlockedAdd(baseAddr + 8, asuint(weightedEmission.z), originalValue);

    // W channel: accumulate emission strength
    emissionGrid.InterlockedAdd(baseAddr + 12, asuint(emission.w), originalValue);
}