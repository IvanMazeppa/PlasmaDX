// Simple compute shader to clear a structured buffer
// Used for clearing particle lighting buffer (structured buffers can't use ClearUnorderedAccessViewFloat)

struct ParticleLighting {
    float4 color;  // RGBA lighting value
};

RWStructuredBuffer<ParticleLighting> outputBuffer : register(u0);

cbuffer ClearParams : register(b0) {
    uint elementCount;
    float clearR;
    float clearG;
    float clearB;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint index = dispatchThreadID.x;

    if (index >= elementCount)
        return;

    outputBuffer[index].color = float4(clearR, clearG, clearB, 0.0);
}
