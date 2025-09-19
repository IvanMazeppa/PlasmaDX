#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>

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
    void DebugSlice(ComPtr<ID3D12GraphicsCommandList4> cmdList, ComPtr<ID3D12Resource> hdrTarget,
                   D3D12_GPU_DESCRIPTOR_HANDLE hdrUav, uint32_t sliceZ);

    void TransitionToSRV(ComPtr<ID3D12GraphicsCommandList4> cmdList);
    void TransitionToUAV(ComPtr<ID3D12GraphicsCommandList4> cmdList);

    void CyclePreset();
    bool RecreateVolume(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap);

    // Getters
    uint32_t GetDimension() const { return m_dimension; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRV() const { return m_srvGpu; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetUAV() const { return m_uavGpu; }
    ID3D12Resource* GetResource() const { return m_densityTexture.Get(); }
    VolumePreset GetCurrentPreset() const { return m_currentPreset; }

    // Descriptor indices for logging
    uint32_t GetSRVIndex() const { return m_srvIndex; }
    uint32_t GetUAVIndex() const { return m_uavIndex; }

private:
    bool CreatePipelines(ComPtr<ID3D12Device5> device);
    bool CreateRootSignatures(ComPtr<ID3D12Device5> device);

    ComPtr<ID3D12Resource> m_densityTexture;
    ComPtr<ID3D12RootSignature> m_fillRootSig;
    ComPtr<ID3D12RootSignature> m_sliceRootSig;
    ComPtr<ID3D12PipelineState> m_fillPSO;
    ComPtr<ID3D12PipelineState> m_slicePSO;

    VolumePreset m_currentPreset;
    uint32_t m_dimension;
    D3D12_RESOURCE_STATES m_currentState;

    // Descriptors
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_uavCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_uavGpu = {};
    uint32_t m_srvIndex = UINT32_MAX;
    uint32_t m_uavIndex = UINT32_MAX;

    // Frame counter for logging
    uint32_t m_frameCounter = 0;

    // Descriptor heap reference
    DescriptorHeap* m_descriptorHeap = nullptr;
};