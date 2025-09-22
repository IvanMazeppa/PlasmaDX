// Particle update compute shader for VOL_0001
// Updates particle positions and velocities with simple physics

struct Particle {
    float3 position;
    float life;
    float3 velocity;
    float padding;
};

cbuffer ParticleConstants : register(b0) {
    float deltaTime;
    float totalTime;
    float worldBounds;
    uint particleCount;

    float3 gravity;
    float damping;
};

RWStructuredBuffer<Particle> particles : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint index = id.x;
    if (index >= particleCount) return;

    Particle p = particles[index];

    // Simple physics update
    // Apply gravity
    p.velocity += gravity * deltaTime;

    // Apply damping
    p.velocity *= damping;

    // Update position
    p.position += p.velocity * deltaTime;

    // Bounce off world bounds (simple boundary condition)
    if (p.position.x < -worldBounds || p.position.x > worldBounds) {
        p.velocity.x = -p.velocity.x;
        p.position.x = clamp(p.position.x, -worldBounds, worldBounds);
    }
    if (p.position.y < -worldBounds || p.position.y > worldBounds) {
        p.velocity.y = -p.velocity.y;
        p.position.y = clamp(p.position.y, -worldBounds, worldBounds);
    }
    if (p.position.z < -worldBounds || p.position.z > worldBounds) {
        p.velocity.z = -p.velocity.z;
        p.position.z = clamp(p.position.z, -worldBounds, worldBounds);
    }

    // Update life (optional - for particle system lifecycle)
    p.life = max(0.0f, p.life - deltaTime * 0.1f); // Slow decay
    if (p.life <= 0.0f) {
        // Respawn particle at random location
        // Use thread ID and time for pseudo-randomness
        float3 seed = float3(index * 73.0f + totalTime, index * 37.0f + totalTime * 2.0f, index * 89.0f + totalTime * 3.0f);
        p.position = (frac(sin(seed) * 43758.5453) * 2.0f - 1.0f) * worldBounds * 0.5f;
        p.velocity = (frac(sin(seed + 1.0f) * 43758.5453) * 2.0f - 1.0f) * 2.0f;
        p.life = 1.0f;
    }

    // Write back
    particles[index] = p;
}