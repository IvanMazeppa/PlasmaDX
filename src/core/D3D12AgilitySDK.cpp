// D3D12 Agility SDK integration
// This file exports the required symbols to use the Agility SDK

extern "C" {
    // Agility SDK version - must match the SDK version (1.616.1 = 616)
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = 616;

    // Path to D3D12 DLLs relative to executable
    __declspec(dllexport) extern const char* D3D12SDKPath = "D3D12\\";
}