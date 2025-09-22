// Debug pattern compute shader for VOL_0001
// Writes a time-varying pattern to HDR texture to validate particle system execution

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

StructuredBuffer<Particle> particles : register(t0);
RWTexture2D<float4> hdrTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint2 coord = id.xy;

    // Get texture dimensions
    uint width, height;
    hdrTexture.GetDimensions(width, height);

    if (coord.x >= width || coord.y >= height) return;

    // Create time-varying color pattern
    float2 uv = float2(coord) / float2(width - 1, height - 1);

    // Animated gradient based on time and particle data
    float hue = frac(totalTime * 0.1f + uv.x * 0.3f + uv.y * 0.2f);

    // Convert HSV to RGB for vibrant colors
    float3 hsv = float3(hue, 0.8f, 0.9f);
    float4 k = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    float3 p = abs(frac(hsv.xxx + k.xyz) * 6.0f - k.www);
    float3 rgb = hsv.z * lerp(k.xxx, clamp(p - k.xxx, 0.0f, 1.0f), hsv.y);

    // Add particle influence (simple visualization of first few particles)
    float particleInfluence = 0.0f;
    if (particleCount > 0) {
        // Sample first few particles to add visual feedback
        uint sampleCount = min(particleCount, 8);
        for (uint i = 0; i < sampleCount; ++i) {
            Particle p = particles[i];

            // Project particle position to screen space [-1,1] -> [0,1]
            float2 particleUV = (p.position.xy / worldBounds) * 0.5f + 0.5f;

            // Distance from current pixel to particle
            float dist = length(uv - particleUV);
            float influence = exp(-dist * 10.0f) * p.life;
            particleInfluence += influence;
        }
    }

    // Combine gradient with particle influence
    rgb += particleInfluence * float3(1.0f, 0.5f, 0.2f);

    // Write to HDR texture
    hdrTexture[coord] = float4(rgb, 1.0f);
}