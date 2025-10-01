// GPU-Accelerated Volumetric Accretion Disk Shader
// High-quality version targeting 60+ fps with enhanced graphics
RWTexture3D<float> g_density : register(u0);
// Note: Temperature texture removed for simplicity - encoding color in density for now

cbuffer AccretionConstants : register(b0) {
    float g_time;
    float g_innerRadius;        // 0.2
    float g_outerRadius;        // 2.5
    float g_diskThickness;      // 0.3
    float3 g_diskCenter;        // (0,0,0)
    float g_rotationSpeed;      // 1.5
    float g_turbulence;         // 0.4
    float g_densityScale;       // 2.0
    float3 g_diskNormal;        // (0,1,0)
    float g_spiralArms;         // 3.0 (number of spiral arms)
    float g_magneticField;      // 0.8 (magnetic field strength)
    float g_accretionRate;      // 1.2 (material inflow rate)
    float g_coronaHeight;       // 0.8 (hot corona above disk)
}

// Enhanced 3D noise functions for high-quality turbulence
float hash31(float3 p) {
    p = frac(p * float3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return frac((p.x + p.y) * p.z);
}

float3 hash33(float3 p) {
    p = frac(p * float3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return frac(float3(p.x + p.y, p.y + p.z, p.z + p.x) * p.zxy);
}

// High-quality 3D noise with multiple octaves
float noise3D(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f); // Hermite smoothing

    return lerp(lerp(lerp(hash31(i), hash31(i + float3(1,0,0)), f.x),
                     lerp(hash31(i + float3(0,1,0)), hash31(i + float3(1,1,0)), f.x), f.y),
                lerp(lerp(hash31(i + float3(0,0,1)), hash31(i + float3(1,0,1)), f.x),
                     lerp(hash31(i + float3(0,1,1)), hash31(i + float3(1,1,1)), f.x), f.y), f.z);
}

// Fractal noise with multiple octaves
float fractalNoise(float3 p, int octaves) {
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise3D(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

// Curl noise for fluid-like motion
float3 curlNoise(float3 p) {
    float eps = 0.01;
    float3 curl;

    curl.x = (noise3D(p + float3(0, eps, 0)) - noise3D(p - float3(0, eps, 0))) / (2.0 * eps) -
             (noise3D(p + float3(0, 0, eps)) - noise3D(p - float3(0, 0, eps))) / (2.0 * eps);

    curl.y = (noise3D(p + float3(0, 0, eps)) - noise3D(p - float3(0, 0, eps))) / (2.0 * eps) -
             (noise3D(p + float3(eps, 0, 0)) - noise3D(p - float3(eps, 0, 0))) / (2.0 * eps);

    curl.z = (noise3D(p + float3(eps, 0, 0)) - noise3D(p - float3(eps, 0, 0))) / (2.0 * eps) -
             (noise3D(p + float3(0, eps, 0)) - noise3D(p - float3(0, eps, 0))) / (2.0 * eps);

    return curl;
}

// Calculate blackbody color from temperature
float3 blackbodyColor(float temperature) {
    // Temperature ranges from 0.1 (red) to 3.0 (blue-white)
    temperature = clamp(temperature, 0.1, 3.0);

    float3 color;
    if (temperature < 0.5) {
        // Deep red to red-orange
        color = float3(1.0, 0.1 + temperature * 0.8, 0.05);
    } else if (temperature < 1.0) {
        // Red-orange to yellow
        float t = (temperature - 0.5) * 2.0;
        color = float3(1.0, 0.5 + t * 0.5, 0.05 + t * 0.45);
    } else if (temperature < 2.0) {
        // Yellow to white
        float t = (temperature - 1.0);
        color = float3(1.0, 1.0, 0.5 + t * 0.5);
    } else {
        // White to blue-white
        float t = (temperature - 2.0);
        color = float3(1.0 - t * 0.2, 1.0 - t * 0.1, 1.0);
    }

    return color * temperature; // Brightness increases with temperature
}

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID) {
    uint3 dims;
    g_density.GetDimensions(dims.x, dims.y, dims.z);

    if (any(id >= dims)) return;

    // Convert to world space (-2 to 2)
    float3 uvw = (float3(id) + 0.5) / float3(dims);
    float3 worldPos = (uvw - 0.5) * 4.0;

    // Transform to disk space
    float3 localPos = worldPos - g_diskCenter;

    // Project onto disk plane
    float height = dot(localPos, g_diskNormal);
    float3 radialPos = localPos - height * g_diskNormal;
    float radius = length(radialPos);

    // Initialize outputs
    float density = 0.0;
    float temperature = 0.0;
    float3 color = float3(0, 0, 0);

    // === MAIN ACCRETION DISK ===
    if (radius >= g_innerRadius && radius <= g_outerRadius && abs(height) <= g_diskThickness) {

        // Calculate orbital angle with Keplerian rotation
        float angle = atan2(radialPos.z, radialPos.x);
        float orbitalSpeed = g_rotationSpeed / pow(radius, 0.5); // Keplerian profile
        float rotatedAngle = angle + orbitalSpeed * g_time;

        // === SPIRAL ARM STRUCTURE WITH NATURAL DISORDER ===
        float spiralAngle = rotatedAngle - log(radius / g_innerRadius) * 0.5; // Logarithmic spiral

        // Add natural variation to spiral arms to break up perfect ordering
        float3 spiralNoise = worldPos * 3.0 + float3(g_time * 0.1, 0, g_time * 0.07);
        float armBreakup = fractalNoise(spiralNoise, 3) * 0.4; // Break up perfect spiral
        float irregularArms = sin((spiralAngle + armBreakup) * g_spiralArms) * 0.5 + 0.5;

        // Add multiple spiral patterns with different phases for complexity
        float secondarySpiral = sin((spiralAngle * 1.7 + g_time * 0.2) * (g_spiralArms + 1)) * 0.3 + 0.7;
        float spiralDensity = 0.2 + 0.6 * irregularArms * secondarySpiral;

        // === RADIAL DENSITY PROFILE WITH NATURAL VARIATION ===
        // Higher density near center, following r^(-3/2) profile with disorder
        float radialFactor = (g_outerRadius - radius) / (g_outerRadius - g_innerRadius);

        // Add radial density waves to break up perfect falloff
        float radialWaves = sin(radius * 8.0 + g_time * 1.5) * 0.2 +
                           sin(radius * 15.0 - g_time * 0.8) * 0.1;
        float baseRadialDensity = pow(radialFactor, 1.5) * (1.0 + 0.3 / radius);
        float radialDensity = baseRadialDensity * (1.0 + radialWaves);

        // === VERTICAL PROFILE ===
        // Gaussian distribution in height
        float heightScale = g_diskThickness * (1.0 + 0.5 * radius); // Flares outward
        float verticalProfile = exp(-height * height / (heightScale * heightScale * 0.5));

        // === ENHANCED TURBULENCE AND FLUID MOTION ===
        float3 turbPos = worldPos * 1.5 + float3(g_time * 0.2, 0, g_time * 0.15);

        // Multi-scale turbulence with additional chaotic motion
        float turbulence1 = fractalNoise(turbPos, 4) * 0.4;
        float turbulence2 = fractalNoise(turbPos * 2.1, 3) * 0.3;
        float turbulence3 = fractalNoise(turbPos * 4.3, 2) * 0.2;

        // Add extra chaotic turbulence to break up patterns
        float3 chaoticPos = worldPos * 0.8 + float3(g_time * 0.33, g_time * 0.19, g_time * 0.27);
        float chaosNoise = fractalNoise(chaoticPos, 5) * 0.3;

        float combinedTurbulence = turbulence1 + turbulence2 + turbulence3 + chaosNoise;

        // Curl noise for fluid-like eddies
        float3 curl = curlNoise(turbPos) * g_turbulence;
        float eddyDensity = 1.0 + 0.4 * length(curl);

        // === MAGNETIC FIELD EFFECTS ===
        // Magnetic field lines create density channels
        float magneticAngle = rotatedAngle * 2.0 + radius * 3.0;
        float magneticDensity = 1.0 + g_magneticField * 0.3 * sin(magneticAngle);

        // === TEMPERATURE CALCULATION ===
        // Base temperature from gravitational potential energy
        float baseTemp = 2.0 / (radius + 0.1); // Hotter near center

        // Add shock heating from spiral arms
        temperature = baseTemp * (1.0 + irregularArms * 0.5);

        // Turbulent heating
        temperature += combinedTurbulence * 0.3;

        // === ACCRETION HEATING ===
        // Material falling inward heats up
        float accretionHeating = g_accretionRate * (1.0 / radius) *
                                sin(g_time * 2.0 + radius * 5.0) * 0.5 + 0.5;
        temperature += accretionHeating * 0.4;

        // === FINAL DENSITY ===
        density = radialDensity * verticalProfile * spiralDensity *
                 (0.6 + 0.4 * combinedTurbulence) * eddyDensity * magneticDensity;

        // Clamp and scale
        density = saturate(density * g_densityScale);
        temperature = clamp(temperature, 0.1, 3.0);
    }

    // === HOT CORONA ===
    // Hot, low-density gas above and below the disk
    if (radius < g_outerRadius * 1.5 && abs(height) > g_diskThickness &&
        abs(height) < g_coronaHeight) {

        float coronaDistance = abs(height) - g_diskThickness;
        float coronaDensity = exp(-coronaDistance / 0.2) * 0.1; // Very tenuous
        float coronaTemp = 2.5 + 0.5 * noise3D(worldPos * 3.0 + g_time); // Very hot

        // Add to existing density/temperature
        density += coronaDensity;
        temperature = max(temperature, coronaTemp);
    }

    // === JETS (Bipolar outflow) ===
    // High-velocity jets from the poles
    if (abs(height) > g_diskThickness && radius < 0.3) {
        float jetHeight = abs(height) - g_diskThickness;
        if (jetHeight < 2.0) {
            float jetDensity = exp(-jetHeight * 0.8) * exp(-radius * 10.0) * 0.3;
            float jetTemp = 2.8; // Very hot jets

            // Add turbulence to jets
            float3 jetTurb = curlNoise(worldPos * 4.0 + float3(0, g_time * 2.0, 0));
            jetDensity *= (1.0 + 0.3 * length(jetTurb));

            density += jetDensity;
            temperature = max(temperature, jetTemp);
        }
    }

    // === HOT SPOTS (Gravitational instabilities) WITH NATURAL MOTION ===
    // Multiple random hot spots that form, move, and dissipate naturally
    float spotPhase1 = sin(g_time * 1.3 + 12.5) * 0.5 + 0.5;
    float spotPhase2 = sin(g_time * 0.8 + 8.7) * 0.5 + 0.5;
    float spotPhase3 = sin(g_time * 2.1 + 3.2) * 0.5 + 0.5;

    // Hot spots with more natural, chaotic movement
    float3 spot1Pos = float3(0.8, 0, 0.3) + 0.2 * sin(g_time * float3(1.1, 0.7, 1.3));
    float3 spot2Pos = float3(-0.6, 0, 0.8) + 0.15 * sin(g_time * float3(0.9, 0.6, 1.1));
    float3 spot3Pos = float3(0.2, 0, -0.9) + 0.1 * sin(g_time * float3(1.7, 0.4, 0.8));

    float spot1 = exp(-length(localPos - spot1Pos) * 8.0) * spotPhase1 * 0.8;
    float spot2 = exp(-length(localPos - spot2Pos) * 6.0) * spotPhase2 * 0.6;
    float spot3 = exp(-length(localPos - spot3Pos) * 10.0) * spotPhase3 * 0.5;

    density += (spot1 + spot2 + spot3);
    temperature += (spot1 + spot2 + spot3) * 1.5;

    // === FINAL COLOR CALCULATION ===
    // Encode temperature-based color intensity into density for simplified output
    if (density > 0.001) {
        color = blackbodyColor(temperature);
        // Use color luminance to modulate density for visual richness
        float luminance = dot(color, float3(0.299, 0.587, 0.114));
        density *= (0.5 + 0.5 * luminance); // Brighten hot regions
    }

    // Write simplified output - density only for now
    g_density[id] = density;
}