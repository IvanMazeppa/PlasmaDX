// GPU-Accelerated Metaball Physics
// Parallel physics updates with spatial grid optimization

#define MAX_METABALLS 512
#define GRID_SIZE 16
#define MAX_NEIGHBORS 64
#define THREADS_PER_GROUP 64

// Input/Output: Metaball data (ping-pong buffers)
StructuredBuffer<float4> g_metaballPositions : register(t0);    // xyz = position, w = radius
StructuredBuffer<float4> g_metaballVelocities : register(t1);   // xyz = velocity, w = mass
StructuredBuffer<float4> g_metaballProperties : register(t2);   // xyz = temperature/age/etc, w = target_radius

// Spatial grid data (from spatial_hash.hlsl)
StructuredBuffer<uint> g_gridCellCounts : register(t3);
StructuredBuffer<uint> g_gridCellOffsets : register(t4);
StructuredBuffer<uint> g_metaballIndices : register(t5);

// Output: Updated metaball data
RWStructuredBuffer<float4> g_outputPositions : register(u0);
RWStructuredBuffer<float4> g_outputVelocities : register(u1);
RWStructuredBuffer<float4> g_outputProperties : register(u2);

// Physics constants
cbuffer PhysicsConstants : register(b0) {
    float g_deltaTime;
    float g_viscosity;          // Fluid resistance
    float g_buoyancyStrength;   // Temperature-driven buoyancy
    float g_noiseStrength;      // Organic motion noise
    float3 g_gravity;           // Gravity force
    float3 g_containerCenter;   // Container bounds
    float g_containerRadius;
    float g_collisionRadius;    // Collision detection radius multiplier
    float g_time;              // Global time for noise
    float g_cellSize;          // Spatial grid cell size
    float3 g_gridMin;          // Spatial grid bounds
};

// Hash 3D grid position to 1D cell index
uint HashGridPosition(int3 gridPos) {
    gridPos = clamp(gridPos, int3(0, 0, 0), int3(GRID_SIZE - 1, GRID_SIZE - 1, GRID_SIZE - 1));
    return gridPos.z * GRID_SIZE * GRID_SIZE + gridPos.y * GRID_SIZE + gridPos.x;
}

// Convert world position to grid coordinates
int3 WorldToGrid(float3 worldPos) {
    float3 normalizedPos = (worldPos - g_gridMin) / (g_containerRadius * 2.0);
    return int3(normalizedPos * GRID_SIZE);
}

// Simple noise function for organic motion
float3 OrganiNoise(float3 position, float time) {
    float noiseX = sin(time * 1.3 + position.y * 5.0) * sin(time * 0.7 + position.z * 3.0);
    float noiseY = sin(time * 0.8 + position.x * 3.0) * sin(time * 1.1 + position.z * 4.0);
    float noiseZ = sin(time * 1.1 + position.x * 4.0) * sin(time * 0.9 + position.y * 2.0);
    return float3(noiseX, noiseY, noiseZ) * g_noiseStrength;
}

// Find neighboring metaballs using spatial grid
uint FindNeighbors(uint metaballIndex, float3 position, float radius, out uint neighbors[MAX_NEIGHBORS]) {
    int3 gridPos = WorldToGrid(position);
    uint neighborCount = 0;

    // Check 3x3x3 grid of adjacent cells
    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                int3 checkPos = gridPos + int3(x, y, z);
                uint cellIndex = HashGridPosition(checkPos);

                uint cellCount = g_gridCellCounts[cellIndex];
                uint cellOffset = g_gridCellOffsets[cellIndex];

                // Check all metaballs in this cell
                for (uint i = 0; i < cellCount && neighborCount < MAX_NEIGHBORS; i++) {
                    uint neighborIndex = g_metaballIndices[cellOffset + i];

                    // Don't include self
                    if (neighborIndex != metaballIndex) {
                        float3 neighborPos = g_metaballPositions[neighborIndex].xyz;
                        float distance = length(position - neighborPos);
                        float combinedRadius = radius + g_metaballPositions[neighborIndex].w;

                        // Add if within interaction range
                        if (distance < combinedRadius * g_collisionRadius) {
                            neighbors[neighborCount] = neighborIndex;
                            neighborCount++;
                        }
                    }
                }
            }
        }
    }

    return neighborCount;
}

// Apply collision response between two metaballs
void HandleCollision(inout float3 position, inout float3 velocity, float mass, float radius,
                    float3 otherPos, float3 otherVel, float otherMass, float otherRadius) {
    float3 diff = position - otherPos;
    float distance = length(diff);
    float minDistance = (radius + otherRadius) * 0.9; // Allow slight overlap

    if (distance < minDistance && distance > 0.001) {
        // Normalize difference vector
        float3 normal = diff / distance;

        // Separate overlapping metaballs
        float overlap = minDistance - distance;
        float totalMass = mass + otherMass;
        float separation = overlap * (otherMass / totalMass);

        position += normal * separation;

        // Exchange velocity (simplified elastic collision)
        float3 relativeVel = velocity - otherVel;
        float velAlongNormal = dot(relativeVel, normal);

        if (velAlongNormal > 0) return; // Objects moving apart

        float restitution = 0.6; // Bounce factor
        float impulse = -(1.0 + restitution) * velAlongNormal;
        impulse /= (1.0/mass + 1.0/otherMass);

        velocity += impulse * normal / mass;
    }
}

[numthreads(THREADS_PER_GROUP, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint metaballIndex = id.x;

    if (metaballIndex >= MAX_METABALLS) return;

    // Load current metaball state
    float4 posRadius = g_metaballPositions[metaballIndex];
    float4 velMass = g_metaballVelocities[metaballIndex];
    float4 properties = g_metaballProperties[metaballIndex];

    float3 position = posRadius.xyz;
    float radius = posRadius.w;
    float3 velocity = velMass.xyz;
    float mass = velMass.w;
    float temperature = properties.x;
    float age = properties.y;
    float targetRadius = properties.w;

    // === FORCE CALCULATION ===

    // Gravity force
    float3 totalForce = g_gravity * mass;

    // Buoyancy (temperature-driven)
    float buoyancy = (temperature - 0.5) * g_buoyancyStrength;
    totalForce += float3(0, buoyancy, 0);

    // Organic noise for natural motion
    totalForce += OrganiNoise(position, g_time);

    // === COLLISION DETECTION WITH SPATIAL OPTIMIZATION ===
    uint neighbors[MAX_NEIGHBORS];
    uint neighborCount = FindNeighbors(metaballIndex, position, radius, neighbors);

    // Handle collisions with neighbors only (not all metaballs!)
    for (uint i = 0; i < neighborCount; i++) {
        uint neighborIndex = neighbors[i];
        float4 neighborPosRadius = g_metaballPositions[neighborIndex];
        float4 neighborVelMass = g_metaballVelocities[neighborIndex];

        HandleCollision(position, velocity, mass, radius,
                       neighborPosRadius.xyz, neighborVelMass.xyz,
                       neighborVelMass.w, neighborPosRadius.w);
    }

    // === PHYSICS INTEGRATION ===

    // Update velocity (F = ma)
    velocity += totalForce * (1.0 / mass) * g_deltaTime;

    // Apply viscosity (fluid resistance)
    velocity *= (1.0 - g_viscosity * g_deltaTime);

    // Update position
    position += velocity * g_deltaTime;

    // === CONTAINER CONSTRAINTS ===
    float3 centerOffset = position - g_containerCenter;
    float distanceFromCenter = length(centerOffset);
    float maxDistance = g_containerRadius - radius;

    if (distanceFromCenter > maxDistance) {
        // Push back inside container
        float3 normal = centerOffset / distanceFromCenter;
        position = g_containerCenter + normal * maxDistance;

        // Bounce velocity
        float velAlongNormal = dot(velocity, normal);
        if (velAlongNormal > 0) {
            velocity -= normal * velAlongNormal * 1.5; // Bounce with energy loss
        }
    }

    // === RADIUS ANIMATION ===
    if (abs(radius - targetRadius) > 0.01) {
        radius = lerp(radius, targetRadius, 2.0 * g_deltaTime);
    }

    // Update age
    age += g_deltaTime;

    // === OUTPUT ===
    g_outputPositions[metaballIndex] = float4(position, radius);
    g_outputVelocities[metaballIndex] = float4(velocity, mass);
    g_outputProperties[metaballIndex] = float4(temperature, age, properties.z, targetRadius);
}