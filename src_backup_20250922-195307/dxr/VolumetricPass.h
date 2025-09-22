#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

struct VolumetricParams
{
    float    stepSize = 0.02f;
    uint32_t maxSteps = 96;
    uint32_t shadowStepInterval = 6;
    float    sigmaExtinction = 1.0f;
    float    densityScale = 1.0f;
    float    emissionScale = 1.0f;
    float    anisotropy = 0.7f;
    uint32_t instanceMask = 0xFF;
};

class VolumetricPass
{
public:
    bool Initialize(ID3D12Device* device);
    void Resize(uint32_t width, uint32_t height);
    void Dispatch(ID3D12GraphicsCommandList* cmd,
                  D3D12_GPU_VIRTUAL_ADDRESS tlas,
                  D3D12_GPU_DESCRIPTOR_HANDLE densitySrv,
                  D3D12_GPU_DESCRIPTOR_HANDLE outRadianceUav,
                  D3D12_GPU_DESCRIPTOR_HANDLE outHitDistUav,
                  const VolumetricParams& params,
                  const float* invViewProj4x4,
                  const float* cameraPos3,
                  const float* lightPos3,
                  const float* lightColor3,
                  float lightRange,
                  uint32_t frameIndex);

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};


