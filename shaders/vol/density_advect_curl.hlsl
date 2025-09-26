// VOL_0004: Curl-like advection baseline (compute)
// Reads from src density (t0), writes to dst density (u0). No sampler required; uses manual trilinear via Load.

Texture3D<float> g_src : register(t0);
RWTexture3D<float> g_dst : register(u0);

cbuffer AdvectParams : register(b0)
{
    float g_deltaTime;   // seconds
    float g_time;        // seconds
    float g_curlSpeed;   // advection speed scale
    float g_flowScale;   // spatial frequency scale
    float g_decay;       // exponential decay per frame (e.g., 0.995)
    float g_injectRate;  // density injection rate near center
    float g_injectRadius;// injection radius in [0,1]
    float g_gridDim;     // float(grid dimension)
};

static const uint GROUP_SIZE = 8;

float3 pseudoCurl(float3 p)
{
    // Cheap, periodic pseudo-velocity field with some curl-like behavior
    float3 q = p * g_flowScale + float3(0.0, g_time, g_time * 0.7);
    float vx = sin(q.y) - cos(q.z * 1.3);
    float vy = sin(q.z) - cos(q.x * 1.7);
    float vz = sin(q.x) - cos(q.y * 1.1);
    return normalize(float3(vx, vy, vz) + 1e-5);
}

float sampleTrilinear(Texture3D<float> tex, float3 uvw, uint dim)
{
    uvw = saturate(uvw);
    float3 coord = uvw * (float(dim) - 1.0);
    float3 base = floor(coord);
    float3 f = coord - base;

    int3 p000 = int3(base);
    int3 p111 = int3(min(base + 1.0, float3(dim - 1, dim - 1, dim - 1)));

    int3 p100 = int3(p111.x, p000.y, p000.z);
    int3 p010 = int3(p000.x, p111.y, p000.z);
    int3 p001 = int3(p000.x, p000.y, p111.z);
    int3 p110 = int3(p111.x, p111.y, p000.z);
    int3 p101 = int3(p111.x, p000.y, p111.z);
    int3 p011 = int3(p000.x, p111.y, p111.z);

    float c000 = tex.Load(int4(p000, 0));
    float c100 = tex.Load(int4(p100, 0));
    float c010 = tex.Load(int4(p010, 0));
    float c001 = tex.Load(int4(p001, 0));
    float c110 = tex.Load(int4(p110, 0));
    float c101 = tex.Load(int4(p101, 0));
    float c011 = tex.Load(int4(p011, 0));
    float c111 = tex.Load(int4(p111, 0));

    float c00 = lerp(c000, c100, f.x);
    float c01 = lerp(c001, c101, f.x);
    float c10 = lerp(c010, c110, f.x);
    float c11 = lerp(c011, c111, f.x);
    float c0 = lerp(c00, c10, f.y);
    float c1 = lerp(c01, c11, f.y);
    return lerp(c0, c1, f.z);
}

[numthreads(GROUP_SIZE, GROUP_SIZE, GROUP_SIZE)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint dim = (uint)g_gridDim;
    if (any(id >= dim)) return;

    float3 uvw = (float3(id) + 0.5) / float(dim);

    // Backtrace along pseudo-curl field
    float3 v = pseudoCurl(uvw);
    float3 uvwPrev = uvw - v * g_curlSpeed * g_deltaTime;

    float d = sampleTrilinear(g_src, uvwPrev, dim);

    // Central injection
    float r = distance(uvw, 0.5.xxx);
    float inject = saturate((g_injectRadius - r) / max(g_injectRadius, 1e-4));
    d += g_injectRate * inject;

    // Decay
    d *= g_decay;

    g_dst[id] = d;
}
