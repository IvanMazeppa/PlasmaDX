// VOL_0003: Compute ray marcher with Beer-Lambert absorption
// Samples 3D density texture and applies proper volumetric absorption with lighting

Texture3D<float> g_density : register(t0);
RWTexture2D<float4> g_hdrTarget : register(u0);
SamplerState g_trilinearSampler : register(s0);

cbuffer CameraConstants : register(b0) {
    float4x4 g_viewMatrix;
    float4x4 g_projMatrix;
    float4x4 g_invViewProjMatrix;
    float3 g_cameraPosition;
    float g_nearPlane;
    float3 g_cameraForward;
    float g_farPlane;
};

cbuffer VolumeConstants : register(b1) {
    float3 g_volumeMin;      // Volume bounds min
    float g_densityScale;    // Global density multiplier
    float3 g_volumeMax;      // Volume bounds max
    float g_absorption;      // Beer-Lambert absorption coefficient
    float3 g_lightDirection; // Normalized directional light vector
    float g_stepSize;        // Ray marching step size
    float3 g_lightColor;     // Light color and intensity
    uint g_maxSteps;         // Maximum ray steps
    float2 g_screenSize;     // Screen resolution
    float g_time;            // Animation time
    float g_exposure;        // HDR exposure control
    // VOL_0003A/B: Debug mode controls
    // 0 = Off, 1 = RayDir visualization, 2 = Bounds/AABB visualization, 3 = Density probe (single sample at entry)
    uint g_debugMode;
    float3 g_debugPad; // padding for 16-byte alignment
};

// Trilinear sampling of density volume
float SampleDensity(float3 worldPos) {
    // Convert world position to volume UVW coordinates [0, 1]
    float3 uvw = (worldPos - g_volumeMin) / (g_volumeMax - g_volumeMin);

    // Clamp to valid range
    uvw = saturate(uvw);

    // Sample density with trilinear filtering
    float density = g_density.SampleLevel(g_trilinearSampler, uvw, 0);
    return density * g_densityScale;
}

// Ray-AABB intersection for volume bounds
bool IntersectAABB(float3 rayOrigin, float3 rayDir, float3 boxMin, float3 boxMax, out float tNear, out float tFar) {
    float3 invDir = 1.0 / (rayDir + 1e-8); // Avoid division by zero
    float3 t1 = (boxMin - rayOrigin) * invDir;
    float3 t2 = (boxMax - rayOrigin) * invDir;

    float3 tMin = min(t1, t2);
    float3 tMax = max(t1, t2);

    tNear = max(max(tMin.x, tMin.y), tMin.z);
    tFar = min(min(tMax.x, tMax.y), tMax.z);

    return tFar >= tNear && tFar > 0.0;
}

// Simple lighting calculation with Beer-Lambert absorption
float3 ComputeLighting(float3 worldPos, float density) {
    if (density <= 0.0001) {
        return float3(0, 0, 0);
    }

    // Basic isotropic scattering phase function
    float phase = 1.0 / (4.0 * 3.14159265);

    // In-scattering: light scattered toward the camera
    float3 scattering = g_lightColor * density * phase;

    return scattering;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    // Early exit for out-of-bounds threads
    if (any(id.xy >= uint2(g_screenSize))) {
        return;
    }

    // Generate eye ray from screen coordinates
    float2 uv = (float2(id.xy) + 0.5) / g_screenSize;
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // Flip Y for D3D12 coordinate system

    // Reconstruct world space ray using inverse view-projection matrix
    // Use mul(vector, matrix) because we supply transposed matrices; this avoids an extra implicit transpose.
    float4 nearPoint = mul(float4(ndc, 0.0, 1.0), g_invViewProjMatrix);
    float4 farPoint  = mul(float4(ndc, 1.0, 1.0), g_invViewProjMatrix);

    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    float3 rayOrigin = nearPoint.xyz;
    float3 rayDir = normalize(farPoint.xyz - nearPoint.xyz);

    // VOL_0003A: Ray direction visualization
    if (g_debugMode == 1) {
        float3 col = 0.5 + 0.5 * normalize(rayDir);
        g_hdrTarget[id.xy] = float4(col, 1.0);
        return;
    }

    // Intersect ray with volume AABB
    float tNear, tFar;
    bool hit = IntersectAABB(rayOrigin, rayDir, g_volumeMin, g_volumeMax, tNear, tFar);

    // VOL_0003A: Bounds/AABB visualization
    if (g_debugMode == 2) {
        if (!hit) {
            // Miss: magenta
            g_hdrTarget[id.xy] = float4(1.0, 0.0, 1.0, 1.0);
            return;
        }
        // Hit: distinguish inside vs outside start
        tNear = max(tNear, 0.0);
        float3 col = (tNear == 0.0) ? float3(1.0, 1.0, 0.0) : float3(0.0, 1.0, 0.0); // yellow if inside, green if outside
        g_hdrTarget[id.xy] = float4(col, 1.0);
        return;
    }

    if (!hit) {
        // Ray misses volume - output transparent
        g_hdrTarget[id.xy] = float4(0, 0, 0, 0);
        return;
    }

    // Ensure we start at or inside the volume
    tNear = max(tNear, 0.0);

    // VOL_0003B: Density probe mode — single sample at entry point for grayscale debug
    if (g_debugMode == 3) {
        float3 entryPos = rayOrigin + rayDir * tNear;
        float d = SampleDensity(entryPos);
        g_hdrTarget[id.xy] = float4(d.xxx, 1.0);
        return;
    }

    // VOL_0003C DEBUG: Mode 4 — UVW visualizer at entry point
    if (g_debugMode == 4) {
        float3 entryPos = rayOrigin + rayDir * tNear;
        float3 uvw = (entryPos - g_volumeMin) / (g_volumeMax - g_volumeMin);
        g_hdrTarget[id.xy] = float4(saturate(uvw), 1.0);
        return;
    }

    // VOL_0003C DEBUG: Mode 5 — Step-count heatmap (no shading)
    if (g_debugMode == 5) {
        float t = tNear;
        uint steps = 0;
        [loop] for (uint i = 0; i < g_maxSteps && t < tFar; ++i) {
            t += g_stepSize;
            steps++;
        }
        float s = steps / max(1.0, (float)g_maxSteps);
        g_hdrTarget[id.xy] = float4(s.xxx, 1.0);
        return;
    }

    // Ray marching with Beer-Lambert absorption
    float3 accumulatedLight = float3(0, 0, 0);
    float transmittance = 1.0;
    float t = tNear;
    uint stepCount = 0;

    // Early transmittance exit threshold
    const float MIN_TRANSMITTANCE = 0.01;

    while (t < tFar && stepCount < g_maxSteps && transmittance > MIN_TRANSMITTANCE) {
        float3 worldPos = rayOrigin + rayDir * t;
        float density = SampleDensity(worldPos);

        if (density > 0.0001) {
            // Compute in-scattering at this sample point
            float3 lighting = ComputeLighting(worldPos, density);

            // Beer-Lambert law: absorption = density * coefficient * step_size
            float absorption = density * g_absorption * g_stepSize;
            float stepTransmittance = exp(-absorption);

            // Volume rendering equation: accumulate light attenuated by transmittance
            // L = ∫ σ_s(t) * L_in(t) * T(0,t) dt
            accumulatedLight += lighting * transmittance * (1.0 - stepTransmittance);

            // Update transmittance: T(0,t+dt) = T(0,t) * exp(-σ_t * dt)
            transmittance *= stepTransmittance;
        }

        t += g_stepSize;
        stepCount++;
    }

    // Apply exposure control
    accumulatedLight *= g_exposure;

    // Write to HDR target with alpha = 1-transmittance (opacity)
    float alpha = 1.0 - transmittance;
    g_hdrTarget[id.xy] = float4(accumulatedLight, alpha);
}