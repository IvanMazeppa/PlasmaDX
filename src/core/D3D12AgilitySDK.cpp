// D3D12 Agility SDK integration
// This file exports the required symbols to use the Agility SDK
// Updated for DXR 1.2 support (requires Agility SDK 1.717.1-preview or newer)

extern "C" {
    // Agility SDK version - Updated to 1.717.1 for DXR 1.2 support
    // Version 717 includes support for:
    // - Shader Execution Reordering (SER)
    // - Opacity Micromaps (OMM)
    // - Enhanced GPU Work Creation
    // - D3D12_FEATURE_DATA_D3D12_OPTIONS7 and newer
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = 717;

    // Path to D3D12 DLLs relative to executable
    __declspec(dllexport) extern const char* D3D12SDKPath = "D3D12\\";
}