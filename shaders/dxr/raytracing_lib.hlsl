// DXR Raytracing Library - Volumetric Sphere RT Lighting Demo
// Target: lib_6_3 (DXR shader model)

struct RayPayload {
    float4 color;
};

// Volumetric sphere intersection data
struct VolumeHitInfo {
    float tNear;       // Ray entry point
    float tFar;        // Ray exit point
    bool hit;          // Whether ray intersects volume
};

// Global resources
RaytracingAccelerationStructure g_scene : register(t0);
RWTexture2D<float4> g_output : register(u0);

// Global parameters via root constants (b0)
cbuffer GlobalParams : register(b0)
{
    float3 g_lightPos; float g_time;           // 0..3
    float3 g_lightDir; float g_innerCos;       // 4..7
    float3 g_lightColor; float g_outerCos;     // 8..11
    float g_mode; float g_bg; float2 g_pad;    // 12..15
}


// Forward declarations for volumetric rendering
void ExecuteVolumetricHit(inout RayPayload payload, float3 rayOrigin, float3 rayDir, float tNear, float tFar, float time);
void ExecuteMiss(inout RayPayload payload, float3 rayDir);

// Volumetric density sampling function with animation
float SampleDensity(float3 worldPos, float3 sphereCenter, float sphereRadius, float time) {
    float distFromCenter = distance(worldPos, sphereCenter);

    // Create a smooth density falloff from center to edge
    float normalizedDist = distFromCenter / sphereRadius;

    if (normalizedDist > 1.0) {
        return 0.0; // Outside sphere
    }

    // Smooth falloff using smoothstep for nice visual appearance
    float density = 1.0 - smoothstep(0.0, 1.0, normalizedDist);

    // Add animated variation for visual interest - rotating pattern
    float variation = sin(worldPos.x * 3.0 + time * 2.0) * sin(worldPos.y * 3.0 + time * 1.5) * sin(worldPos.z * 3.0 + time * 1.8);
    density += variation * 0.15;

    // Add pulsing effect to make density changes more visible
    float pulse = 0.8 + 0.3 * sin(time * 3.0);

    return max(0.1, density * pulse); // Ensure some minimum density for visibility
}

// Ray-sphere intersection for volumetric rendering
VolumeHitInfo IntersectSphere(float3 rayOrigin, float3 rayDir, float3 sphereCenter, float sphereRadius) {
    VolumeHitInfo result;
    result.hit = false;
    result.tNear = 0.0;
    result.tFar = 0.0;

    // Vector from ray origin to sphere center
    float3 oc = rayOrigin - sphereCenter;

    // Quadratic equation coefficients: ||rayDir||^2 * t^2 + 2*dot(oc,rayDir) * t + ||oc||^2 - r^2 = 0
    float a = dot(rayDir, rayDir);
    float b = 2.0 * dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;

    float discriminant = b * b - 4 * a * c;

    if (discriminant < 0.0) {
        return result; // No intersection
    }

    float sqrtDisc = sqrt(discriminant);
    float t1 = (-b - sqrtDisc) / (2.0 * a); // Near intersection
    float t2 = (-b + sqrtDisc) / (2.0 * a); // Far intersection

    // Ensure t1 <= t2
    if (t1 > t2) {
        float temp = t1;
        t1 = t2;
        t2 = temp;
    }

    // Check if intersection is in front of ray
    if (t2 > 0.001) { // Small epsilon to avoid self-intersection
        result.hit = true;
        result.tNear = max(0.001, t1); // Start marching from ray origin if inside sphere
        result.tFar = t2;
    }

    return result;
}

// Spotlight helper – soft cone with distance attenuation
float SpotlightTerm(float3 worldPos, float3 lightPos, float3 lightDir,
                    float innerCos, float outerCos) {
    float3 toPoint = normalize(worldPos - lightPos);
    float cosTheta = dot(toPoint, normalize(lightDir));
    // Soft edge between outer and inner cone
    float cone = saturate((cosTheta - outerCos) / max(1e-4, (innerCos - outerCos)));
    // Simple quadratic attenuation
    float d = distance(worldPos, lightPos);
    float atten = 1.0 / (1.0 + 0.4 * d + 0.15 * d * d);
    return cone * atten;
}

[shader("raygeneration")]
void RayGen() {
    uint2 index = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;

    // Calculate UV coordinates
    float2 uv = float2(index) / float2(dimensions);

    // Setup ray for perspective projection
    float aspectRatio = float(dimensions.x) / float(dimensions.y);

    // Camera position and direction
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y for screen space

    float3 rayOrigin = float3(0, 0, -3);
    float3 rayDir = normalize(float3(ndc.x * aspectRatio, ndc.y, 1.0));

    // Initialize payload
    RayPayload payload;
    payload.color = float4(0, 0, 0, 1.0);

    // Create simple animation using pixel index hash - this will create scrolling patterns
    float animTime = float((index.x * 1919 + index.y * 2019) % 10000) * 0.001;

    // Define a volumetric sphere in world space (centered at origin)
    float3 sphereCenter = float3(0.0, 0.0, 0.0);
    float sphereRadius = 1.2;

    // Test ray-sphere intersection
    VolumeHitInfo volInfo = IntersectSphere(rayOrigin, rayDir, sphereCenter, sphereRadius);

    if (volInfo.hit) {
        // Call volumetric hit logic manually with animation time
        ExecuteVolumetricHit(payload, rayOrigin, rayDir, volInfo.tNear, volInfo.tFar, animTime);
    } else {
        // Call Miss logic manually
        ExecuteMiss(payload, rayDir);
    }

    // Write result to output texture
    g_output[index] = payload.color;
}

[shader("miss")]
void Miss(inout RayPayload payload) {
    // Dark room background to emphasize spotlight
    float3 direction = WorldRayDirection();
    float t = 0.5 * (direction.y + 1.0);
    float3 topColor = float3(0.02, 0.02, 0.025) * g_bg;
    float3 bottomColor = float3(0.0, 0.0, 0.0) * g_bg;
    float3 skyColor = lerp(bottomColor, topColor, t);
    payload.color = float4(skyColor, 1.0);
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    // Calculate barycentric coordinates
    float3 barycentrics = float3(
        1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y);

    // Get world position using ray equation
    float3 worldPos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    // Calculate surface normal for triangle (facing camera)
    // For a simple triangle, we can use a fixed normal or derive from vertices
    float3 normal = float3(0, 0, -1); // Face toward camera

    // Simple directional lighting
    float3 lightDir = normalize(float3(0.3, 0.8, -0.5));
    float3 lightColor = float3(1.0, 0.9, 0.7); // Warm white

    // Lambertian diffuse
    float NdotL = max(0.0, dot(normal, lightDir));
    float3 diffuse = lightColor * NdotL;

    // Add some ambient
    float3 ambient = float3(0.1, 0.1, 0.15);

    // Combine lighting with barycentric coloring for visual feedback
    float3 surfaceColor = lerp(float3(0.8, 0.2, 0.2), float3(0.2, 0.8, 0.2), barycentrics.y);
    surfaceColor = lerp(surfaceColor, float3(0.2, 0.2, 0.8), barycentrics.z);

    float3 finalColor = surfaceColor * (diffuse + ambient);

    payload.color = float4(finalColor, 1.0);
}

// Manual execution of bright pulsing volumetric sphere
void ExecuteVolumetricHit(inout RayPayload payload, float3 rayOrigin, float3 rayDir, float tNear, float tFar, float time) {
    // Volume properties
    float3 sphereCenter = float3(0.0, 0.0, 0.0);
    float sphereRadius = 1.2;

    // Spotlight setup from root constants (animated in C++)
    float3 lightPos  = g_lightPos;
    float3 lightDir  = normalize(g_lightDir);
    float  innerCos  = g_innerCos;
    float  outerCos  = g_outerCos;
    float3 lightCol  = g_lightColor;
    float3 ambient   = float3(0.03, 0.03, 0.035);

    // March through the sphere segment [tNear, tFar]
    const int   kSteps   = 64;
    float       t        = tNear;
    float       dt       = (tFar - tNear) / kSteps;
    float3      accum    = 0.0.xxx;
    float       trans    = 1.0;
    const float sigmaA   = 1.2;   // absorption
    const float sigmaS   = 2.0;   // scattering

    [loop]
    for (int i = 0; i < kSteps; ++i) {
        float3 p = rayOrigin + (t + 0.5 * dt) * rayDir;

        // Animated density inside sphere
        float dens = SampleDensity(p, sphereCenter, sphereRadius, time);

        // Spotlight contribution
        float spot = SpotlightTerm(p, lightPos, lightDir, innerCos, outerCos);

        // Simple single-scatter model
        float3 Li = lightCol * spot;
        float3 scatter = Li * (dens * sigmaS) * trans * dt;
        accum += scatter;

        // Beer-Lambert absorption
        trans *= exp(-dens * sigmaA * dt);
        t += dt;
    }

    float3 color = accum + ambient * 0.2;
    payload.color = float4(color, 1.0);
}

// Manual execution of Miss logic (called from RayGen)
void ExecuteMiss(inout RayPayload payload, float3 rayDir) {
    // DEBUG: Yellow miss shader - easy to distinguish from lit triangle
    float3 direction = rayDir;
    float t = 0.5 * (direction.y + 1.0);

    // Yellow gradient instead of blue
    float3 topColor = float3(1.0, 1.0, 0.5);   // Light yellow
    float3 bottomColor = float3(0.8, 0.6, 0.0); // Dark yellow
    float3 skyColor = lerp(bottomColor, topColor, t);

    payload.color = float4(skyColor, 1.0);
}