#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;

class DescriptorHeap;

class DensityVolume {
public:
    enum class VolumePreset {
        Small = 96,
        Medium = 128,
        Large = 192,
        XLarge = 256
    };

    DensityVolume();
    ~DensityVolume();

    bool Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap, VolumePreset preset = VolumePreset::Medium);
    void Shutdown();

    void FillAnalytic(ComPtr<ID3D12GraphicsCommandList4> cmdList, float time);
    // VOL_0003B: Fill with analytic sphere baseline
    void FillAnalyticSphere(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                            DirectX::XMFLOAT3 centerUVW, float radiusUVW, float densityValue);
    void DebugSlice(ComPtr<ID3D12GraphicsCommandList4> cmdList, ComPtr<ID3D12Resource> hdrTarget,
                   D3D12_GPU_DESCRIPTOR_HANDLE hdrUav, uint32_t sliceZ);

    // VOL_0004: Curl-advection step (ping-pong src→dst, then swap)
    void AdvectCurl(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                    float deltaTime,
                    float timeSeconds);

    void TransitionToSRV(ComPtr<ID3D12GraphicsCommandList4> cmdList);
    void TransitionToUAV(ComPtr<ID3D12GraphicsCommandList4> cmdList);

    void CyclePreset();
    bool RecreateVolume(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap);

    // Getters
    uint32_t GetDimension() const { return m_dimension; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRV() const { return m_srcIsA ? m_srvGpuA : m_srvGpuB; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetUAV() const { return m_srcIsA ? m_uavGpuA : m_uavGpuB; }
    ID3D12Resource* GetResource() const { return m_srcIsA ? m_densityA.Get() : m_densityB.Get(); }
    VolumePreset GetCurrentPreset() const { return m_currentPreset; }

    // Descriptor indices for logging
    uint32_t GetSRVIndex() const { return m_srcIsA ? m_srvIndexA : m_srvIndexB; }
    uint32_t GetUAVIndex() const { return m_srcIsA ? m_uavIndexA : m_uavIndexB; }

private:
    bool CreatePipelines(ComPtr<ID3D12Device5> device);
    bool CreateRootSignatures(ComPtr<ID3D12Device5> device);
    void transitionResource(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                            ID3D12Resource* resource,
                            D3D12_RESOURCE_STATES& currentState,
                            D3D12_RESOURCE_STATES targetState);

    // Ping-pong density textures (A/B)
    ComPtr<ID3D12Resource> m_densityA;
    ComPtr<ID3D12Resource> m_densityB;

    ComPtr<ID3D12RootSignature> m_fillRootSig;
    ComPtr<ID3D12RootSignature> m_sliceRootSig;
    ComPtr<ID3D12RootSignature> m_advectRootSig; // VOL_0004
    ComPtr<ID3D12PipelineState> m_fillPSO;
    ComPtr<ID3D12PipelineState> m_slicePSO;
    ComPtr<ID3D12PipelineState> m_advectPSO; // VOL_0004
    ComPtr<ID3D12Resource> m_advectCB;       // VOL_0004 constants (b0)

    VolumePreset m_currentPreset;
    uint32_t m_dimension;
    D3D12_RESOURCE_STATES m_stateA;
    D3D12_RESOURCE_STATES m_stateB;
    bool m_srcIsA = true; // if true: src=A,dst=B; else src=B,dst=A

    // Descriptors
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvCpuA = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpuA = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_uavCpuA = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_uavGpuA = {};
    uint32_t m_srvIndexA = UINT32_MAX;
    uint32_t m_uavIndexA = UINT32_MAX;

    D3D12_CPU_DESCRIPTOR_HANDLE m_srvCpuB = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpuB = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_uavCpuB = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_uavGpuB = {};
    uint32_t m_srvIndexB = UINT32_MAX;
    uint32_t m_uavIndexB = UINT32_MAX;

    // Frame counter for logging
    uint32_t m_frameCounter = 0;

    // Descriptor heap reference
    DescriptorHeap* m_descriptorHeap = nullptr;
};