#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class DescriptorHeap;
class DensityVolume;
class Camera;

struct VolumeRenderParams {
    XMFLOAT3 volumeMin = { -1.0f, -1.0f, -1.0f };
    float densityScale = 1.0f;
    XMFLOAT3 volumeMax = { 1.0f, 1.0f, 1.0f };
    float absorption = 1.0f;
    XMFLOAT3 lightDirection = { 0.3f, 0.8f, 0.2f }; // Normalized
    float stepSize = 0.02f;
    XMFLOAT3 lightColor = { 1.0f, 0.9f, 0.7f };
    uint32_t maxSteps = 128;
    XMFLOAT2 screenSize = { 1920.0f, 1080.0f };
    float time = 0.0f;
    float padding = 0.0f;
};

class VolumeRenderer {
public:
    VolumeRenderer();
    ~VolumeRenderer();

    bool Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap);
    void Shutdown();

    void RenderVolume(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                     DensityVolume* densityVolume,
                     ComPtr<ID3D12Resource> hdrTarget,
                     D3D12_GPU_DESCRIPTOR_HANDLE hdrUav,
                     Camera* camera,
                     float time);

    // Parameter controls
    void SetDensityScale(float scale) { m_params.densityScale = scale; }
    void SetAbsorption(float absorption) { m_params.absorption = absorption; }
    void SetStepSize(float stepSize) { m_params.stepSize = stepSize; }
    void SetMaxSteps(uint32_t maxSteps) { m_params.maxSteps = maxSteps; }
    void SetLightDirection(XMFLOAT3 dir);
    void SetLightColor(XMFLOAT3 color) { m_params.lightColor = color; }

    // Getters for controls
    const VolumeRenderParams& GetParams() const { return m_params; }

private:
    bool CreatePipeline(ComPtr<ID3D12Device5> device);
    bool CreateRootSignature(ComPtr<ID3D12Device5> device);
    bool CreateSampler(ComPtr<ID3D12Device5> device);

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    // Constant buffers
    ComPtr<ID3D12Resource> m_cameraConstantBuffer;
    ComPtr<ID3D12Resource> m_volumeConstantBuffer;

    // Sampler
    ComPtr<ID3D12DescriptorHeap> m_samplerHeap;

    // Parameters
    VolumeRenderParams m_params;

    // Descriptor heap reference
    DescriptorHeap* m_descriptorHeap = nullptr;

    // Frame counter for logging
    uint32_t m_frameCounter = 0;
};