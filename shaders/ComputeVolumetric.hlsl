#include "VolumetricCommon.hlsli"

// Optional: Inline RayQuery-based shadow test
bool ShadowVisible(float3 P, float3 L, float tMax, uint mask)
{
    RayDesc ray;
    ray.Origin = P;
    ray.Direction = L;
    ray.TMin = 0.01;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE> rq;
    rq.TraceRayInline(SceneBVH, 0, mask, ray);
    while (rq.Proceed()) {}
    return rq.CommittedStatus() == COMMITTED_NOTHING;
}

[numthreads(8,8,1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= gOutputSize)) return;

    uint2 pix = dtid.xy;
    float3 V = ReconstructWorldDirection(pix, gOutputSize, gJitter);

    // Define a bounding volume for the medium in world space (customize)
    // For scaffolding, use a unit cube centered at origin
    float3 volMin = float3(-1.0, -1.0, -1.0);
    float3 volMax = float3( 1.0,  1.0,  1.0);

    // Ray-box intersection (slab method)
    float3 invDir = 1.0 / max(abs(V), 1e-6) * sign(V);
    float3 t0 = (volMin - gCameraWorldPos) * (1.0 / V);
    float3 t1 = (volMax - gCameraWorldPos) * (1.0 / V);
    float tmin = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    float tmax = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    if (tmax < max(tmin, 0.0)) { OutRadiance[pix] = 0; OutHitDist[pix] = 0; return; }
    tmin = max(tmin, 0.0);

    float stepLen = gStepSize;
    uint  maxSteps = gMaxSteps;
    float t = tmin;
    float3 radiance = 0.0;
    float transmittance = 1.0;

    // March through the medium
    [loop]
    for (uint i = 0; i < maxSteps && t <= tmax; ++i)
    {
        float3 P = gCameraWorldPos + V * t;
        float density = SampleDensity(P, volMin, volMax);
        float sigma_t = gSigmaExtinction * density;

        // Self-emission (blackbody-like placeholder)
        float3 emission = gEmissionScale * density * float3(1.0, 0.8, 0.6);
        radiance += transmittance * emission * stepLen;

        // Single scattering from point light
        float3 toL = gLightWorldPos - P;
        float distL = length(toL);
        float3 L = toL / max(distL, 1e-5);
        float cosTheta = dot(L, -V);
        float phase = PhaseHG(cosTheta, gAnisotropy);

        bool vis = true;
        if ((i % max(gShadowStepInterval,1u)) == 0)
        {
            vis = ShadowVisible(P, L, min(distL, gLightRange), gInstanceMask);
        }
        float3 inScatter = 0.0;
        if (vis)
        {
            float att = 1.0 / max(distL * distL, 1e-4);
            inScatter = gLightColor * att * phase * density;
        }
        radiance += transmittance * inScatter * stepLen;

        // Beer-Lambert attenuation
        float extinction = sigma_t * stepLen;
        transmittance *= exp(-extinction);
        if (transmittance < 1e-3) break;

        t += stepLen;
    }

    OutRadiance[pix] = float4(radiance, 1.0);
    OutHitDist[pix] = tmax;
}


