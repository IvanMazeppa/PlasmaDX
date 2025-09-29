// DirectX 12 Mesh Shader for NASA-quality accretion disk particles
// Creates camera-facing billboards for each particle with temperature-based coloring

struct Particle {
    float3 position;
    float temperature;
    float3 velocity;
    float density;
};

struct RenderConstants {
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float3 cameraPos;
    float particleSize;
    float temperatureScale;
    float3 padding;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float3 color : COLOR0;
    float alpha : COLOR1;
};

StructuredBuffer<Particle> particles : register(t0);
ConstantBuffer<RenderConstants> renderConstants : register(b0);

// Temperature to color mapping (NASA-style plasma visualization)
float3 TemperatureToColor(float temperature) {
    // Normalize temperature to 0-1 range (500K to 6000K)
    float t = saturate((temperature - 500.0) / 5500.0);

    // NASA-style color mapping: blue (cold) -> cyan -> yellow -> orange -> red (hot)
    float3 color;
    if (t < 0.25) {
        // Blue to cyan
        float blend = t / 0.25;
        color = lerp(float3(0.2, 0.4, 1.0), float3(0.0, 1.0, 1.0), blend);
    } else if (t < 0.5) {
        // Cyan to yellow
        float blend = (t - 0.25) / 0.25;
        color = lerp(float3(0.0, 1.0, 1.0), float3(1.0, 1.0, 0.0), blend);
    } else if (t < 0.75) {
        // Yellow to orange
        float blend = (t - 0.5) / 0.25;
        color = lerp(float3(1.0, 1.0, 0.0), float3(1.0, 0.6, 0.0), blend);
    } else {
        // Orange to red
        float blend = (t - 0.75) / 0.25;
        color = lerp(float3(1.0, 0.6, 0.0), float3(1.0, 0.2, 0.0), blend);
    }

    return color * (0.5 + t * 1.5); // Increase brightness with temperature
}

[NumThreads(32, 1, 1)]
[OutputTopology("triangle")]
void main(
    uint3 groupId : SV_GroupID,
    uint3 localId : SV_GroupThreadID,
    out vertices VertexOutput verts[128],    // 32 particles * 4 vertices = 128
    out indices uint3 tris[64]               // 32 particles * 2 triangles = 64
) {
    uint particleIndex = groupId.x * 32 + localId.x;
    uint vertexIndex = localId.x * 4;  // 4 vertices per particle
    uint triangleIndex = localId.x * 2; // 2 triangles per particle

    // Check if we have a valid particle
    if (particleIndex >= 100000) { // Hardcoded particle count for now
        return;
    }

    SetMeshOutputCounts(128, 64);

    Particle p = particles[particleIndex];

    // Calculate camera-facing billboard vectors
    float3 worldPos = p.position;
    float3 viewDir = normalize(renderConstants.cameraPos - worldPos);
    float3 right = normalize(cross(float3(0, 1, 0), viewDir));
    float3 up = cross(viewDir, right);

    // Scale based on particle size and temperature (hotter = larger)
    float scale = renderConstants.particleSize * (1.0 + p.temperature / 5000.0 * 0.5);
    right *= scale;
    up *= scale;

    // Generate temperature-based color
    float3 color = TemperatureToColor(p.temperature);
    float alpha = saturate(p.density * 0.8); // Alpha based on density

    // Create 4 vertices for the billboard quad
    float4x4 viewProj = mul(renderConstants.viewMatrix, renderConstants.projMatrix);

    // Bottom-left
    float3 pos0 = worldPos - right - up;
    verts[vertexIndex + 0].position = mul(float4(pos0, 1.0), viewProj);
    verts[vertexIndex + 0].texCoord = float2(0.0, 1.0);
    verts[vertexIndex + 0].color = color;
    verts[vertexIndex + 0].alpha = alpha;

    // Bottom-right
    float3 pos1 = worldPos + right - up;
    verts[vertexIndex + 1].position = mul(float4(pos1, 1.0), viewProj);
    verts[vertexIndex + 1].texCoord = float2(1.0, 1.0);
    verts[vertexIndex + 1].color = color;
    verts[vertexIndex + 1].alpha = alpha;

    // Top-left
    float3 pos2 = worldPos - right + up;
    verts[vertexIndex + 2].position = mul(float4(pos2, 1.0), viewProj);
    verts[vertexIndex + 2].texCoord = float2(0.0, 0.0);
    verts[vertexIndex + 2].color = color;
    verts[vertexIndex + 2].alpha = alpha;

    // Top-right
    float3 pos3 = worldPos + right + up;
    verts[vertexIndex + 3].position = mul(float4(pos3, 1.0), viewProj);
    verts[vertexIndex + 3].texCoord = float2(1.0, 0.0);
    verts[vertexIndex + 3].color = color;
    verts[vertexIndex + 3].alpha = alpha;

    // Create 2 triangles for the quad
    // Triangle 1: bottom-left, bottom-right, top-left
    tris[triangleIndex + 0] = uint3(
        vertexIndex + 0,
        vertexIndex + 1,
        vertexIndex + 2
    );

    // Triangle 2: bottom-right, top-right, top-left
    tris[triangleIndex + 1] = uint3(
        vertexIndex + 1,
        vertexIndex + 3,
        vertexIndex + 2
    );
}

// Pixel shader for particle rendering
float4 PSMain(VertexOutput input) : SV_Target {
    // Create circular particle shape using texture coordinates
    float2 center = input.texCoord - 0.5;
    float distance = length(center);

    // Smooth circular falloff
    float alpha = 1.0 - smoothstep(0.3, 0.5, distance);
    alpha *= input.alpha;

    // Apply temperature-based color with glow effect
    float3 color = input.color;
    float glow = 1.0 - distance * 2.0;
    color *= (0.8 + glow * 0.4);

    return float4(color, alpha);
}