// VolumetricCommon.hlsli - shared types and helpers for volumetric marching

cbuffer VolumetricCB : register(b0)
{
    float4x4 gInvViewProj;          // inverse of ViewProj
    float3   gCameraWorldPos;
    float    gStepSize;             // meters per step
    float3   gLightWorldPos;        // point light position
    float    gLightRange;           // max light distance
    float3   gLightColor;           // RGB intensity (radiance scale)
    float    gDensityScale;         // scale for density field
    float    gEmissionScale;        // emissive scale from density/temperature
    float    gSigmaExtinction;      // extinction coefficient (per meter)
    uint     gMaxSteps;             // primary march steps (64-128)
    uint     gShadowStepInterval;   // run visibility every N steps (e.g., 6)
    uint     gFrameIndex;           // for jitter/blue-noise rotation
    uint2    gOutputSize;           // dispatch target size
    float    gJitter;               // 0..1 subpixel jitter
    float    gAnisotropy;           // Henyey-Greenstein g (-0.9..0.9)
    uint     gInstanceMask;         // TLAS instance mask for visibility rays
};

RaytracingAccelerationStructure SceneBVH : register(t0);
Texture3D<float>                DensityTex : register(t1);
SamplerState                    LinearClamp : register(s0);

RWTexture2D<float4>             OutRadiance : register(u0);
RWTexture2D<float>              OutHitDist  : register(u1);

// Reconstruct a world-space ray from pixel coordinates using inverse view-projection
float3 ReconstructWorldDirection(uint2 pixel, uint2 size, float jitter)
{
    float2 uv = (float2(pixel) + jitter) / float2(size);
    float2 ndc = uv * 2.0 - 1.0;
    float4 p  = mul(gInvViewProj, float4(ndc, 0.0, 1.0));
    p.xyz    /= max(p.w, 1e-5);
    float3 dir = normalize(p.xyz - gCameraWorldPos);
    return dir;
}

// Simple Henyey-Greenstein phase function (scalar)
float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return (1.0 - g2) / max(denom * 4.0 * 3.14159265, 1e-6);
}

// Sample density field (expects 0..1 UVW), callers convert from world pos
float SampleDensity(float3 worldPos, float3 volumeMin, float3 volumeMax)
{
    float3 uvw = saturate((worldPos - volumeMin) / (volumeMax - volumeMin));
    return DensityTex.SampleLevel(LinearClamp, uvw, 0).r * gDensityScale;
}


