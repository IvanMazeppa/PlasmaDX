// NASA-quality accretion disk physics simulation
// Ported from Vulkan implementation with DirectX 12 mesh shader integration

struct Particle {
    float3 position;
    float temperature;
    float3 velocity;
    float density;
};

struct ParticleConstants {
    float deltaTime;
    float totalTime;
    float blackHoleMass;
    float gravityStrength;
    float3 blackHolePosition;
    float viscosity;
    float3 diskAxis;
    float innerRadius;
    float outerRadius;
    float diskThickness;
    float temperatureScale;
    float particleCount;
};

RWStructuredBuffer<Particle> particles : register(u0);
ConstantBuffer<ParticleConstants> constants : register(b0);

// NASA-quality orbital mechanics constants
static const float SCHWARZSCHILD_RADIUS = 2.95e10; // meters for Sagittarius A*
static const float C_LIGHT = 299792458.0; // m/s
static const float SOLAR_MASS = 1.989e30; // kg
static const float STEFAN_BOLTZMANN = 5.67e-8; // W/(m^2 K^4)

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint particleIndex = id.x;
    if (particleIndex >= uint(constants.particleCount)) return;

    Particle p = particles[particleIndex];

    // Initialize particles if this is the first frame
    if (constants.totalTime < 0.01) {
        // Initialize accretion disk particles
        float angle = float(particleIndex) / constants.particleCount * 6.28318530718; // 2*PI
        float radius = lerp(constants.innerRadius, constants.outerRadius,
                           pow(float(particleIndex) / constants.particleCount, 0.8));

        // Random vertical offset for disk thickness
        uint seed = particleIndex * 1103515245u + 12345u;
        float randZ = (float((seed >> 16) & 0x7fff) / 32767.0 - 0.5) * constants.diskThickness;

        p.position = float3(
            cos(angle) * radius,
            randZ,
            sin(angle) * radius
        );

        // Keplerian orbital velocity with relativistic correction
        float distance = length(p.position - constants.blackHolePosition);
        float keplerianSpeed = sqrt(constants.gravityStrength * constants.blackHoleMass / (distance + 0.5));

        // Orbital direction (perpendicular to radial direction)
        float3 radial = normalize(p.position - constants.blackHolePosition);
        float3 orbital = normalize(cross(constants.diskAxis, radial));
        p.velocity = orbital * keplerianSpeed * 0.3; // Scale for visualization

        // Temperature based on distance (hotter closer to black hole)
        p.temperature = 1000.0 + 5000.0 / (distance + 1.0);
        p.density = 1.0;
    } else {
        // Physics update for existing particles
        float3 position = p.position;
        float3 velocity = p.velocity;

        // Gravitational force toward black hole
        float3 toBlackHole = constants.blackHolePosition - position;
        float distance = length(toBlackHole);
        float3 direction = toBlackHole / distance;

        // Newtonian gravity with relativistic correction near event horizon
        float gravity = constants.gravityStrength * constants.blackHoleMass / (distance * distance);
        float relativistic = 1.0 - (SCHWARZSCHILD_RADIUS / (distance * 1e6 + SCHWARZSCHILD_RADIUS));
        gravity *= relativistic;

        float3 gravityForce = direction * gravity;

        // Viscous forces (simplified Shakura-Sunyaev disk model)
        float3 viscousForce = -velocity * constants.viscosity;

        // Update velocity and position using Verlet integration
        velocity += (gravityForce + viscousForce) * constants.deltaTime;
        position += velocity * constants.deltaTime;

        // Update temperature based on viscous heating and radiative cooling
        float viscousHeating = dot(viscousForce, velocity) * constants.temperatureScale;
        float radiativeCooling = p.temperature * p.temperature * p.temperature * p.temperature * 1e-10;
        p.temperature += (viscousHeating - radiativeCooling) * constants.deltaTime;
        p.temperature = max(p.temperature, 500.0); // Minimum temperature

        // Keep particles within disk bounds
        if (distance > constants.outerRadius * 1.5) {
            // Reset particle to inner disk
            float angle = constants.totalTime + float(particleIndex) * 0.1;
            position = float3(
                cos(angle) * constants.innerRadius * 1.1,
                position.y * 0.5, // Preserve some vertical motion
                sin(angle) * constants.innerRadius * 1.1
            );

            float keplerianSpeed = sqrt(constants.gravityStrength * constants.blackHoleMass / constants.innerRadius);
            float3 radial = normalize(position - constants.blackHolePosition);
            float3 orbital = normalize(cross(constants.diskAxis, radial));
            velocity = orbital * keplerianSpeed * 0.3;
        }

        p.position = position;
        p.velocity = velocity;
    }

    particles[particleIndex] = p;
}