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
        // Initialize accretion disk particles with randomization
        uint seed = particleIndex * 1103515245u + 12345u;
        uint seed2 = seed * 1664525u + 1013904223u;
        uint seed3 = seed2 * 22695477u + 1;

        // Fully randomize angle (break up rings completely)
        float angleRand = float((seed2 >> 16) & 0x7fff) / 32767.0;
        float angle = angleRand * 6.28318530718; // Random angle 0 to 2π

        // Randomize radius with more variation
        float radiusRand = float((seed3 >> 16) & 0x7fff) / 32767.0;
        float radius = lerp(constants.innerRadius, constants.outerRadius,
                            pow(radiusRand, 0.7)); // Power distribution for more inner particles

        // Random vertical offset for disk thickness
        float randZ = (float((seed >> 16) & 0x7fff) / 32767.0 - 0.5) * constants.diskThickness;

        p.position = float3(
            cos(angle) * radius,
            randZ,
            sin(angle) * radius
        );

        // COMPLETELY RANDOM velocity - no orbital mechanics at all
        uint seed4 = seed3 * 2654435761u;
        uint seed5 = seed4 * 48271u;
        uint seed6 = seed5 * 1103515245u;

        float3 randomVel = float3(
            (float((seed4 >> 16) & 0x7fff) / 32767.0 - 0.5),
            (float((seed5 >> 16) & 0x7fff) / 32767.0 - 0.5),
            (float((seed6 >> 16) & 0x7fff) / 32767.0 - 0.5)
        );
        p.velocity = randomVel * 15.0; // HUGE random velocities in all directions

        // Calculate distance for temperature
        float distance = length(p.position - constants.blackHolePosition);

        // Temperature based on distance (MUCH hotter near black hole for color variation)
        // Inner particles: ~10,000K (red/orange), Outer particles: ~800K (blue)
        p.temperature = 800.0 + 25000.0 / (distance + 1.0);
        p.density = 1.0;
    } else {
        // Physics update for existing particles
        float3 position = p.position;
        float3 velocity = p.velocity;

        // CURL NOISE TURBULENCE - Creates vortices to break up ribbon formations
        float turbulenceStrength = 8.0; // VERY strong turbulence for dramatic spreading
        float3 curlPos = position * 0.08 + float3(constants.totalTime * 0.03, 0, 0);
        float epsilon = 0.05;

        // Sample potential field at 6 points for curl calculation
        float px1 = sin(curlPos.x + epsilon) * cos(curlPos.y * 1.7) * sin(curlPos.z * 2.3);
        float px2 = sin(curlPos.x - epsilon) * cos(curlPos.y * 1.7) * sin(curlPos.z * 2.3);
        float py1 = sin(curlPos.x) * cos((curlPos.y + epsilon) * 1.7) * sin(curlPos.z * 2.3);
        float py2 = sin(curlPos.x) * cos((curlPos.y - epsilon) * 1.7) * sin(curlPos.z * 2.3);
        float pz1 = sin(curlPos.x) * cos(curlPos.y * 1.7) * sin((curlPos.z + epsilon) * 2.3);
        float pz2 = sin(curlPos.x) * cos(curlPos.y * 1.7) * sin((curlPos.z - epsilon) * 2.3);

        // Calculate curl (rotating flow field)
        float3 curl;
        curl.x = (pz1 - pz2) / (2.0 * epsilon) - (py1 - py2) / (2.0 * epsilon);
        curl.y = (px1 - px2) / (2.0 * epsilon) - (pz1 - pz2) / (2.0 * epsilon);
        curl.z = (py1 - py2) / (2.0 * epsilon) - (px1 - px2) / (2.0 * epsilon);

        // Add smaller scale eddies for detail
        float3 curlPos2 = position * 0.25 + float3(constants.totalTime * 0.08, 0, 0);
        curl += float3(
            sin(curlPos2.y * 5.1) * cos(curlPos2.z * 4.3),
            sin(curlPos2.z * 5.1) * cos(curlPos2.x * 4.3),
            sin(curlPos2.x * 5.1) * cos(curlPos2.y * 4.3)
        ) * 0.2;

        // Apply turbulence to velocity
        velocity += curl * turbulenceStrength * constants.deltaTime;

        // Add per-particle random noise that varies over time (breaks coherent motion)
        float randomPhase = float(particleIndex) * 0.1 + constants.totalTime * 0.5;
        float3 randomNoise = float3(
            sin(randomPhase * 1.7),
            sin(randomPhase * 2.3),
            sin(randomPhase * 3.1)
        ) * 3.0; // Strong random jitter
        velocity += randomNoise * constants.deltaTime;

        // Update position based on velocity
        position += velocity * constants.deltaTime;

        // Apply MINIMAL damping to preserve turbulent motion
        float dampingFactor = 0.99; // Very light damping (1% energy loss per frame)
        velocity *= dampingFactor;

        // DON'T force particles into perfect orbits - let turbulence dominate
        // (Removed orbital speed boost to allow cloud-like behavior)

        // Calculate distance for temperature and bounds check
        float distance = length(position - constants.blackHolePosition);

        // Keep temperature based on distance
        p.temperature = 800.0 + 25000.0 / (distance + 1.0);

        // Keep particles within a large sphere (soft boundary, don't force orbits)
        if (distance > constants.outerRadius * 2.0) {
            // Gently push back toward center without forcing orbit
            float3 pushBack = -normalize(position) * 2.0;
            velocity += pushBack * constants.deltaTime;
        }

        p.position = position;
        p.velocity = velocity;
    }

    particles[particleIndex] = p;
}