// Simple compute shader to clear the emission grid buffer to zero
// This runs once per frame before accumulating emission data

RWStructuredBuffer<uint> emissionGrid : register(u0);

cbuffer ClearConstants : register(b0)
{
    uint gridSizeInDWORDs;  // Total size of grid in uints
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint index = dispatchThreadID.x;

    // Early exit if beyond buffer size
    if (index >= gridSizeInDWORDs)
        return;

    // Write zero to this uint
    emissionGrid[index] = 0;
}
