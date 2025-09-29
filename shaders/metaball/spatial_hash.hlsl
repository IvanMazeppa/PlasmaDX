// GPU-Accelerated Metaball Spatial Hashing
// Reduces collision detection from O(N²) to O(N) using 3D grid partitioning

#define MAX_METABALLS 512
#define GRID_SIZE 16
#define MAX_CELLS_PER_METABALL 8
#define THREADS_PER_GROUP 64

// Input: Metaball positions and radii
StructuredBuffer<float4> g_metaballPositions : register(t0); // xyz = position, w = radius
StructuredBuffer<float4> g_metaballData : register(t1);      // xyz = velocity, w = mass

// Output: Spatial grid data
RWStructuredBuffer<uint> g_gridCellCounts : register(u0);     // Count of metaballs per cell
RWStructuredBuffer<uint> g_gridCellOffsets : register(u1);    // Offset into metaball list per cell
RWStructuredBuffer<uint> g_metaballIndices : register(u2);    // Packed metaball indices by cell

// Constants
cbuffer SpatialHashConstants : register(b0) {
    uint g_numMetaballs;
    float g_cellSize;           // Size of each grid cell
    float3 g_gridMin;           // Minimum bounds of spatial grid
    float3 g_gridMax;           // Maximum bounds of spatial grid
    uint g_frameCounter;        // For debugging
};

// Reduced groupshared memory to stay within 32KB limit
// 16^3 = 4096 uints = 16KB (within 32KB limit)
groupshared uint s_cellCounts[GRID_SIZE * GRID_SIZE * GRID_SIZE];

// Hash 3D grid position to 1D cell index
uint HashGridPosition(int3 gridPos) {
    // Clamp to grid bounds
    gridPos = clamp(gridPos, int3(0, 0, 0), int3(GRID_SIZE - 1, GRID_SIZE - 1, GRID_SIZE - 1));
    return gridPos.z * GRID_SIZE * GRID_SIZE + gridPos.y * GRID_SIZE + gridPos.x;
}

// Convert world position to grid coordinates
int3 WorldToGrid(float3 worldPos) {
    float3 normalizedPos = (worldPos - g_gridMin) / (g_gridMax - g_gridMin);
    return int3(normalizedPos * GRID_SIZE);
}

// Get all grid cells that a metaball overlaps
void GetOverlappingCells(float3 position, float radius, out int3 cellList[MAX_CELLS_PER_METABALL], out uint cellCount) {
    // Calculate bounding box of metaball in grid space
    float3 minPos = position - float3(radius, radius, radius);
    float3 maxPos = position + float3(radius, radius, radius);

    int3 minGrid = WorldToGrid(minPos);
    int3 maxGrid = WorldToGrid(maxPos);

    cellCount = 0;

    // Add all overlapping grid cells
    for (int z = minGrid.z; z <= maxGrid.z && cellCount < MAX_CELLS_PER_METABALL; z++) {
        for (int y = minGrid.y; y <= maxGrid.y && cellCount < MAX_CELLS_PER_METABALL; y++) {
            for (int x = minGrid.x; x <= maxGrid.x && cellCount < MAX_CELLS_PER_METABALL; x++) {
                cellList[cellCount] = int3(x, y, z);
                cellCount++;
            }
        }
    }
}

[numthreads(THREADS_PER_GROUP, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID) {
    uint metaballIndex = id.x;
    uint localThreadId = groupThreadId.x;

    // Initialize shared memory (only some threads need to do this)
    uint totalCells = GRID_SIZE * GRID_SIZE * GRID_SIZE;
    for (uint i = localThreadId; i < totalCells; i += THREADS_PER_GROUP) {
        s_cellCounts[i] = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    // Process metaball if within bounds
    if (metaballIndex < g_numMetaballs) {
        float4 posRadius = g_metaballPositions[metaballIndex];
        float3 position = posRadius.xyz;
        float radius = posRadius.w;

        // Find all grid cells this metaball overlaps
        int3 cellList[MAX_CELLS_PER_METABALL];
        uint cellCount;
        GetOverlappingCells(position, radius, cellList, cellCount);

        // Increment count for each overlapping cell
        for (uint i = 0; i < cellCount; i++) {
            uint cellIndex = HashGridPosition(cellList[i]);

            // Atomic increment in shared memory
            InterlockedAdd(s_cellCounts[cellIndex], 1);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Write shared memory results to global memory
    for (uint i = localThreadId; i < totalCells; i += THREADS_PER_GROUP) {
        InterlockedAdd(g_gridCellCounts[i], s_cellCounts[i]);
    }
}