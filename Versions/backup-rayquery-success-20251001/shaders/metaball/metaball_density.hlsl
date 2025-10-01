// GPU-Accelerated Metaball Density Field Generation
// Evaluates metaball influence using spatial grid optimization

#define MAX_METABALLS 512
#define GRID_SIZE 16
#define MAX_NEARBY_METABALLS 32

// Input: Metaball data
StructuredBuffer<float4> g_metaballPositions : register(t0);    // xyz = position, w = radius
StructuredBuffer<float4> g_metaballProperties : register(t1);   // x = temperature, y = age, z = intensity, w = target_radius

// Spatial grid data for optimization
StructuredBuffer<uint> g_gridCellCounts : register(t2);
StructuredBuffer<uint> g_gridCellOffsets : register(t3);
StructuredBuffer<uint> g_metaballIndices : register(t4);

// Output: 3D density volume
RWTexture3D<float> g_densityVolume : register(u0);

// Constants
cbuffer DensityConstants : register(b0) {
    uint g_numMetaballs;
    uint g_volumeSize;          // Density volume resolution (e.g., 128)
    float g_voxelSize;          // Size of each voxel in world space
    float3 g_volumeMin;         // Minimum bounds of density volume
    float3 g_volumeMax;         // Maximum bounds of density volume
    float g_metaballStrength;   // Global metaball field strength
    float g_temperatureInfluence; // How much temperature affects density
    float g_blendThreshold;     // Minimum density to consider for blending
    float g_time;              // For animated effects
    float g_cellSize;          // Spatial grid cell size
    float3 g_gridMin;          // Spatial grid bounds
};

// Hash 3D grid position to 1D cell index
uint HashGridPosition(int3 gridPos) {
    gridPos = clamp(gridPos, int3(0, 0, 0), int3(GRID_SIZE - 1, GRID_SIZE - 1, GRID_SIZE - 1));
    return gridPos.z * GRID_SIZE * GRID_SIZE + gridPos.y * GRID_SIZE + gridPos.x;
}

// Convert world position to spatial grid coordinates
int3 WorldToSpatialGrid(float3 worldPos) {
    float3 normalizedPos = (worldPos - g_gridMin) / (g_volumeMax - g_volumeMin);
    return int3(normalizedPos * GRID_SIZE);
}

// Convert voxel coordinates to world position
float3 VoxelToWorld(uint3 voxelCoord) {
    float3 normalizedCoord = float3(voxelCoord) / float(g_volumeSize);
    return g_volumeMin + normalizedCoord * (g_volumeMax - g_volumeMin);
}

// Metaball density function with smooth falloff
float EvaluateMetaball(float3 worldPos, float3 metaballPos, float radius, float temperature, float intensity) {
    float distance = length(worldPos - metaballPos);

    if (distance >= radius) return 0.0;

    // Smooth polynomial falloff (C2 continuous)
    float t = distance / radius;
    float falloff = 1.0 - t * t * t * (t * (t * 6.0 - 15.0) + 10.0);

    // Temperature affects density (hotter = less dense for natural convection)
    float tempFactor = 1.0 + (0.5 - temperature) * g_temperatureInfluence;

    return falloff * intensity * tempFactor * g_metaballStrength;
}

// Find nearby metaballs using spatial grid
uint FindNearbyMetaballs(float3 worldPos, out uint nearbyMetaballs[MAX_NEARBY_METABALLS]) {
    int3 gridPos = WorldToSpatialGrid(worldPos);
    uint nearbyCount = 0;

    // Check 3x3x3 grid of adjacent cells for metaballs
    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                int3 checkPos = gridPos + int3(x, y, z);
                uint cellIndex = HashGridPosition(checkPos);

                uint cellCount = g_gridCellCounts[cellIndex];
                uint cellOffset = g_gridCellOffsets[cellIndex];

                // Add all metaballs in this cell
                for (uint i = 0; i < cellCount && nearbyCount < MAX_NEARBY_METABALLS; i++) {
                    uint metaballIndex = g_metaballIndices[cellOffset + i];

                    // Quick distance check to avoid unnecessary evaluations
                    float3 metaballPos = g_metaballPositions[metaballIndex].xyz;
                    float metaballRadius = g_metaballPositions[metaballIndex].w;
                    float distance = length(worldPos - metaballPos);

                    if (distance <= metaballRadius * 1.5) { // Include slightly beyond radius for smooth blending
                        nearbyMetaballs[nearbyCount] = metaballIndex;
                        nearbyCount++;
                    }
                }
            }
        }
    }

    return nearbyCount;
}

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID) {
    // Check bounds
    if (any(id >= g_volumeSize)) return;

    // Convert voxel coordinate to world position
    float3 worldPos = VoxelToWorld(id);

    // Find nearby metaballs using spatial optimization
    uint nearbyMetaballs[MAX_NEARBY_METABALLS];
    uint nearbyCount = FindNearbyMetaballs(worldPos, nearbyMetaballs);

    float totalDensity = 0.0;

    // Evaluate only nearby metaballs (not all metaballs!)
    for (uint i = 0; i < nearbyCount; i++) {
        uint metaballIndex = nearbyMetaballs[i];

        float4 posRadius = g_metaballPositions[metaballIndex];
        float4 properties = g_metaballProperties[metaballIndex];

        float3 metaballPos = posRadius.xyz;
        float radius = posRadius.w;
        float temperature = properties.x;
        float intensity = properties.z;

        // Evaluate metaball contribution
        float contribution = EvaluateMetaball(worldPos, metaballPos, radius, temperature, intensity);
        totalDensity += contribution;
    }

    // Apply smooth blending for organic look
    if (totalDensity > g_blendThreshold) {
        // Enhance density in high-concentration areas for blob-like appearance
        totalDensity = sqrt(totalDensity); // Compress high values slightly
    }

    // Add subtle noise for organic texture
    float3 noisePos = worldPos * 8.0 + float3(g_time * 0.1, 0, g_time * 0.07);
    float noise = sin(noisePos.x) * sin(noisePos.y) * sin(noisePos.z) * 0.05;
    totalDensity += noise;

    // DEBUG: Override with simple test pattern using normalized coordinates
    float3 normalizedCoord = float3(id) / float(g_volumeSize);
    float3 center = float3(0.5, 0.5, 0.5);
    float distFromCenter = length(normalizedCoord - center);

    // Large sphere (covers most of volume) - should be visible if working
    float testDensity = (distFromCenter < 0.4) ? 1.0 : 0.1; // Large sphere with background

    // Use test pattern instead of metaball density
    g_densityVolume[id] = testDensity;
}