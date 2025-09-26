// Volumetric ray marching compute shader with directional lighting and self-shadowing
// Samples 3D density texture and applies Beer-Lambert absorption with lighting

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
    float g_padding;
};

// Trilinear sampling of density volume
float SampleDensity(float3 worldPos) {
    // Convert world position to volume UVW coordinates [0, 1]
    float3 uvw = (worldPos - g_volumeMin) / (g_volumeMax - g_volumeMin);

    // Clamp to valid range
    uvw = saturate(uvw);

    // Sample density
    float density = g_density.SampleLevel(g_trilinearSampler, uvw, 0);
    return density * g_densityScale;
}

// Ray-AABB intersection
bool IntersectAABB(float3 rayOrigin, float3 rayDir, float3 boxMin, float3 boxMax, out float tNear, out float tFar) {
    float3 invDir = 1.0 / rayDir;
    float3 t1 = (boxMin - rayOrigin) * invDir;
    float3 t2 = (boxMax - rayOrigin) * invDir;

    float3 tMin = min(t1, t2);
    float3 tMax = max(t1, t2);

    tNear = max(max(tMin.x, tMin.y), tMin.z);
    tFar = min(min(tMax.x, tMax.y), tMax.z);

    return tFar >= tNear && tFar > 0.0;
}

// Compute lighting with self-shadowing
float3 ComputeLighting(float3 worldPos, float density) {
    if (density <= 0.0001) {
        return float3(0, 0, 0);
    }

    // Self-shadowing: march toward light to accumulate occlusion
    float shadow = 1.0;
    float shadowStepSize = g_stepSize * 2.0; // Coarser steps for shadow rays
    uint shadowSteps = 8; // Fewer steps for performance

    float3 lightRayPos = worldPos + g_lightDirection * shadowStepSize;

    for (uint i = 0; i < shadowSteps; ++i) {
        // Check if we're still in volume bounds
        if (any(lightRayPos < g_volumeMin) || any(lightRayPos > g_volumeMax)) {
            break;
        }

        float shadowDensity = SampleDensity(lightRayPos);
        shadow *= exp(-shadowDensity * g_absorption * shadowStepSize);

        if (shadow < 0.01) {
            shadow = 0.0;
            break;
        }

        lightRayPos += g_lightDirection * shadowStepSize;
    }

    // Basic phase function (isotropic scattering)
    float phase = 1.0 / (4.0 * 3.14159265);

    // Apply lighting
    float3 scattering = g_lightColor * density * phase * shadow;
    return scattering;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(g_screenSize))) {
        return;
    }

    // Generate eye ray from screen coordinates
    float2 uv = (float2(id.xy) + 0.5) / g_screenSize;
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // Flip Y for D3D12

    // Reconstruct world space ray
    float4 nearPoint = mul(g_invViewProjMatrix, float4(ndc, 0.0, 1.0));
    float4 farPoint = mul(g_invViewProjMatrix, float4(ndc, 1.0, 1.0));

    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    float3 rayOrigin = nearPoint.xyz;
    float3 rayDir = normalize(farPoint.xyz - nearPoint.xyz);

    // Intersect ray with volume AABB
    float tNear, tFar;
    if (!IntersectAABB(rayOrigin, rayDir, g_volumeMin, g_volumeMax, tNear, tFar)) {
        g_hdrTarget[id.xy] = float4(0, 0, 0, 1);
        return;
    }

    // Ensure we start at or inside the volume
    tNear = max(tNear, 0.0);

    // Ray marching
    float3 color = float3(0, 0, 0);
    float transmittance = 1.0;
    float t = tNear;
    uint stepCount = 0;

    while (t < tFar && stepCount < g_maxSteps && transmittance > 0.01) {
        float3 worldPos = rayOrigin + rayDir * t;
        float density = SampleDensity(worldPos);

        if (density > 0.0001) {
            // Compute lighting at this sample point
            float3 lighting = ComputeLighting(worldPos, density);

            // Emission (could add fire/plasma color here)
            float3 emission = lighting;

            // Beer-Lambert absorption
            float absorption = density * g_absorption * g_stepSize;
            float stepTransmittance = exp(-absorption);

            // Accumulate color
            color += emission * transmittance * (1.0 - stepTransmittance);
            transmittance *= stepTransmittance;
        }

        t += g_stepSize;
        stepCount++;
    }

    // Apply some basic tonemapping and boost for HDR
    color = color / (color + 1.0); // Reinhard tone mapping
    color *= 2.0; // HDR boost

    // Blend with existing HDR content (additive)
    float4 existing = g_hdrTarget[id.xy];
    float4 result = float4(color + existing.rgb * 0.1, 1.0);

    g_hdrTarget[id.xy] = result;
}