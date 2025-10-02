// Simple compute shader to clear the emission grid buffer to zero
// This runs once per frame before accumulating emission data

RWByteAddressBuffer emissionGrid : register(u0);

cbuffer ClearConstants : register(b0)
{
    uint gridSizeInDWORDs;  // Total size of grid in 32-bit words
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint index = dispatchThreadID.x;

    // Early exit if beyond buffer size
    if (index >= gridSizeInDWORDs)
        return;

    // Write zero to this DWORD
    emissionGrid.Store(index * 4, 0);
}
