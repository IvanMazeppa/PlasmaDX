#include "DensityVolume.h"
#include "../utils/Logger.h"
#include "../utils/DescriptorHeap.h"
#include <d3dx12/d3dx12.h>
#include <d3dcompiler.h>
#include <cmath>
#include <fstream>
#include <vector>

DensityVolume::DensityVolume()
    : m_currentPreset(VolumePreset::Medium)
    , m_dimension(128)
    , m_currentState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    , m_frameCounter(0) {
}

DensityVolume::~DensityVolume() {
    Shutdown();
}

bool DensityVolume::Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap, VolumePreset preset) {
    m_descriptorHeap = descriptorHeap;
    m_currentPreset = preset;
    m_dimension = static_cast<uint32_t>(preset);

    // Create 3D texture with UAV support
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = m_dimension;
    desc.Height = m_dimension;
    desc.DepthOrArraySize = static_cast<UINT16>(m_dimension);
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        m_currentState,
        nullptr,
        IID_PPV_ARGS(&m_densityTexture)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create density volume texture");
        return false;
    }

    m_densityTexture->SetName(L"DensityVolume");

    // Allocate descriptors
    if (!m_descriptorHeap) {
        LOGE("Global descriptor heap not available");
        return false;
    }

    m_srvIndex = m_descriptorHeap->Allocate();
    m_uavIndex = m_descriptorHeap->Allocate();

    if (m_srvIndex == UINT32_MAX || m_uavIndex == UINT32_MAX) {
        LOGE("Failed to allocate descriptors for density volume");
        return false;
    }

    m_srvCpu = m_descriptorHeap->GetCPUHandle(m_srvIndex);
    m_srvGpu = m_descriptorHeap->GetGPUHandle(m_srvIndex);
    m_uavCpu = m_descriptorHeap->GetCPUHandle(m_uavIndex);
    m_uavGpu = m_descriptorHeap->GetGPUHandle(m_uavIndex);

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture3D.MipLevels = 1;
    device->CreateShaderResourceView(m_densityTexture.Get(), &srvDesc, m_srvCpu);

    // Create UAV
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uavDesc.Texture3D.MipSlice = 0;
    uavDesc.Texture3D.FirstWSlice = 0;
    uavDesc.Texture3D.WSize = m_dimension;
    device->CreateUnorderedAccessView(m_densityTexture.Get(), nullptr, &uavDesc, m_uavCpu);

    LOGI("DensityVolume created successfully");

    // Create pipelines
    if (!CreateRootSignatures(device)) {
        LOGE("Failed to create root signatures");
        return false;
    }

    if (!CreatePipelines(device)) {
        LOGE("Failed to create compute pipelines");
        return false;
    }

    return true;
}

bool DensityVolume::CreateRootSignatures(ComPtr<ID3D12Device5> device) {
    // Fill root signature: UAV for density + constants
    {
        CD3DX12_DESCRIPTOR_RANGE1 uavRange;
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

        CD3DX12_ROOT_PARAMETER1 rootParams[2];
        rootParams[0].InitAsDescriptorTable(1, &uavRange);
        rootParams[1].InitAsConstants(4, 0); // time, scale, center.x, center.y

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> signature, error;
        if (FAILED(D3DX12SerializeVersionedRootSignature(&rootSigDesc,
            D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error))) {
            if (error) {
                LOGE("Fill root signature error");
            }
            return false;
        }

        HRESULT hr = device->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&m_fillRootSig));
        if (FAILED(hr)) {
            LOGE("Failed to create fill root signature");
            return false;
        }
    }

    // Slice root signature: SRV for density + UAV for HDR + constants
    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange, uavRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

        CD3DX12_ROOT_PARAMETER1 rootParams[3];
        rootParams[0].InitAsDescriptorTable(1, &srvRange);
        rootParams[1].InitAsDescriptorTable(1, &uavRange);
        rootParams[2].InitAsConstants(2, 0); // sliceZ, volumeDim

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> signature, error;
        if (FAILED(D3DX12SerializeVersionedRootSignature(&rootSigDesc,
            D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error))) {
            if (error) {
                LOGE("Slice root signature error");
            }
            return false;
        }

        HRESULT hr = device->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&m_sliceRootSig));
        if (FAILED(hr)) {
            LOGE("Failed to create slice root signature");
            return false;
        }
    }

    return true;
}

bool DensityVolume::CreatePipelines(ComPtr<ID3D12Device5> device) {
    // Load compiled shaders
    std::vector<uint8_t> fillShader, sliceShader;

    // Load density_fill.dxil
    {
        std::ifstream file("shaders/density_fill.dxil", std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            LOGW("density_fill.dxil not found, density volume will use fallback");
            return true; // Non-fatal, we can still run
        }
        size_t size = file.tellg();
        fillShader.resize(size);
        file.seekg(0);
        file.read((char*)fillShader.data(), size);
    }

    // Load density_slice.dxil
    {
        std::ifstream file("shaders/density_slice.dxil", std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            LOGW("density_slice.dxil not found, debug slice disabled");
            // Continue without slice shader
        } else {
            size_t size = file.tellg();
            sliceShader.resize(size);
            file.seekg(0);
            file.read((char*)sliceShader.data(), size);
        }
    }

    // Create fill PSO
    if (!fillShader.empty()) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_fillRootSig.Get();
        psoDesc.CS.pShaderBytecode = fillShader.data();
        psoDesc.CS.BytecodeLength = fillShader.size();

        HRESULT hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_fillPSO));
        if (FAILED(hr)) {
            LOGE("Failed to create fill PSO");
            return false;
        }
    }

    // Create slice PSO
    if (!sliceShader.empty()) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_sliceRootSig.Get();
        psoDesc.CS.pShaderBytecode = sliceShader.data();
        psoDesc.CS.BytecodeLength = sliceShader.size();

        HRESULT hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_slicePSO));
        if (FAILED(hr)) {
            LOGE("Failed to create slice PSO");
            return false;
        }
    }

    LOGI("DensityVolume pipelines created successfully");
    return true;
}

void DensityVolume::Shutdown() {
    if (m_descriptorHeap) {
        if (m_srvIndex != UINT32_MAX) {
            m_descriptorHeap->Free(m_srvIndex);
            m_srvIndex = UINT32_MAX;
        }
        if (m_uavIndex != UINT32_MAX) {
            m_descriptorHeap->Free(m_uavIndex);
            m_uavIndex = UINT32_MAX;
        }
    }

    m_slicePSO.Reset();
    m_fillPSO.Reset();
    m_sliceRootSig.Reset();
    m_fillRootSig.Reset();
    m_densityTexture.Reset();
}

void DensityVolume::FillAnalytic(ComPtr<ID3D12GraphicsCommandList4> cmdList, float time) {
    if (!m_fillPSO || !m_fillRootSig || !m_densityTexture) {
        // For now, just ensure UAV state
        if (m_currentState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            TransitionToUAV(cmdList);
        }

        // Log heartbeat every ~60 frames
        if (++m_frameCounter % 60 == 0) {
            LOGI("DensityVolume heartbeat");
        }
        return;
    }

    // Ensure UAV state
    if (m_currentState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        TransitionToUAV(cmdList);
    }

    cmdList->SetComputeRootSignature(m_fillRootSig.Get());
    cmdList->SetPipelineState(m_fillPSO.Get());
    cmdList->SetComputeRootDescriptorTable(0, m_uavGpu);

    // Set constants: time, scale, center
    float constants[4] = { time, 0.5f, 0.5f, 0.5f };
    cmdList->SetComputeRoot32BitConstants(1, 4, constants, 0);

    // Dispatch with 8x8x8 thread groups
    uint32_t groups = (m_dimension + 7) / 8;
    cmdList->Dispatch(groups, groups, groups);

    // UAV barrier to ensure writes complete
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_densityTexture.Get();
    cmdList->ResourceBarrier(1, &barrier);
}

void DensityVolume::DebugSlice(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                               ComPtr<ID3D12Resource> hdrTarget,
                               D3D12_GPU_DESCRIPTOR_HANDLE hdrUav,
                               uint32_t sliceZ) {
    if (!m_slicePSO || !m_sliceRootSig || !m_densityTexture || !hdrTarget) {
        return;
    }

    // Transition density to SRV for reading
    if (m_currentState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE &&
        m_currentState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        TransitionToSRV(cmdList);
    }

    // Note: HDR target should already be in UAV state from caller

    cmdList->SetComputeRootSignature(m_sliceRootSig.Get());
    cmdList->SetPipelineState(m_slicePSO.Get());
    cmdList->SetComputeRootDescriptorTable(0, m_srvGpu);
    cmdList->SetComputeRootDescriptorTable(1, hdrUav);

    uint32_t constants[2] = { sliceZ, m_dimension };
    cmdList->SetComputeRoot32BitConstants(2, 2, constants, 0);

    // Dispatch to cover HDR resolution (assuming 1920x1080)
    cmdList->Dispatch((1920 + 15) / 16, (1080 + 15) / 16, 1);
}

void DensityVolume::TransitionToSRV(ComPtr<ID3D12GraphicsCommandList4> cmdList) {
    if (m_currentState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ||
        m_currentState == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_densityTexture.Get();
    barrier.Transition.StateBefore = m_currentState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(1, &barrier);
    m_currentState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

void DensityVolume::TransitionToUAV(ComPtr<ID3D12GraphicsCommandList4> cmdList) {
    if (m_currentState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_densityTexture.Get();
    barrier.Transition.StateBefore = m_currentState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(1, &barrier);
    m_currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

void DensityVolume::CyclePreset() {
    switch (m_currentPreset) {
        case VolumePreset::Small:
            m_currentPreset = VolumePreset::Medium;
            break;
        case VolumePreset::Medium:
            m_currentPreset = VolumePreset::Large;
            break;
        case VolumePreset::Large:
            m_currentPreset = VolumePreset::XLarge;
            break;
        case VolumePreset::XLarge:
            m_currentPreset = VolumePreset::Small;
            break;
    }
    LOGI("DensityVolume preset changed");
}

bool DensityVolume::RecreateVolume(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap) {
    Shutdown();
    return Initialize(device, descriptorHeap, m_currentPreset);
}