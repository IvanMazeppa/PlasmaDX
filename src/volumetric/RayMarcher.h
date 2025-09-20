#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <algorithm>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class DescriptorHeap;
class DensityVolume;
class Camera;

struct RayMarcherParams {
    XMFLOAT3 volumeMin = { -1.0f, -1.0f, -1.0f };
    float densityScale = 1.0f;
    XMFLOAT3 volumeMax = { 1.0f, 1.0f, 1.0f };
    float absorption = 0.5f;  // Reduced for better visibility
    XMFLOAT3 lightDirection = { 0.3f, 0.8f, 0.2f };
    float stepSize = 0.02f;
    XMFLOAT3 lightColor = { 1.0f, 0.9f, 0.7f };
    uint32_t maxSteps = 128;
    XMFLOAT2 screenSize = { 1920.0f, 1080.0f };
    float time = 0.0f;
    float exposure = 5.0f;  // Increased for better visibility
};

class RayMarcher {
public:
    RayMarcher();
    ~RayMarcher();

    bool Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap);
    void Shutdown();

    void March(ComPtr<ID3D12GraphicsCommandList4> cmdList,
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
    void SetExposure(float exposure) { m_params.exposure = exposure; }
    void SetLightDirection(XMFLOAT3 dir);
    void SetLightColor(XMFLOAT3 color) { m_params.lightColor = color; }
    void SetScreenSize(float width, float height) {
        m_params.screenSize.x = width;
        m_params.screenSize.y = height;
    }

    // VOL_0003A: Debug modes (0=Off, 1=RayDir, 2=Bounds)
    void SetDebugMode(uint32_t mode) { m_debugMode = mode % 3u; }
    void CycleDebugMode() { m_debugMode = (m_debugMode + 1u) % 3u; }
    uint32_t GetDebugMode() const { return m_debugMode; }

    const RayMarcherParams& GetParams() const { return m_params; }

private:
    bool CreatePipeline(ComPtr<ID3D12Device5> device);
    bool CreateRootSignature(ComPtr<ID3D12Device5> device);
    bool CreateSampler(ComPtr<ID3D12Device5> device);
    bool CreateConstantBuffers(ComPtr<ID3D12Device5> device);

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    // Constant buffers
    ComPtr<ID3D12Resource> m_cameraConstantBuffer;
    ComPtr<ID3D12Resource> m_volumeConstantBuffer;

    // Sampler
    ComPtr<ID3D12DescriptorHeap> m_samplerHeap;

    // Parameters
    RayMarcherParams m_params;

    // Descriptor heap reference
    DescriptorHeap* m_descriptorHeap = nullptr;

    // Frame counter for logging
    uint32_t m_frameCounter = 0;

    // Temporal accumulation (VOL_0004)
    ComPtr<ID3D12Resource> m_historyTexture;
    uint32_t m_historyUavIndex = UINT_MAX;
    uint32_t m_historySrvIndex = UINT_MAX;
    bool m_temporalEnabled = true;
    float m_temporalAlpha = 0.1f;  // Blend factor: 0.1 = 10% current, 90% history
    uint32_t m_frameIndex = 0;

    // Camera jitter for TAA
    float m_jitterX = 0.0f;
    float m_jitterY = 0.0f;

    // VOL_0003A
    uint32_t m_debugMode = 0u;

public:
    // Temporal accumulation controls
    void SetTemporalEnabled(bool enabled) { m_temporalEnabled = enabled; }
    void ResetHistory() { m_frameIndex = 0; }
    bool IsTemporalEnabled() const { return m_temporalEnabled; }
    void SetTemporalBlendFactor(float alpha) { m_temporalAlpha = std::max(0.01f, std::min(1.0f, alpha)); }
};