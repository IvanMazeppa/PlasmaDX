#include "DensityVolume.h"
#include "MetaballSystem.h"
#include "../utils/Logger.h"
#include "../utils/DescriptorHeap.h"
#include <d3dx12/d3dx12.h>
#include <d3dcompiler.h>
#include <cmath>
#include <fstream>
#include <vector>
#include <cstring>

DensityVolume::DensityVolume()
    : m_currentPreset(VolumePreset::Medium)
    , m_dimension(128)
    , m_stateA(D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    , m_stateB(D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    , m_frameCounter(0) {
}

DensityVolume::~DensityVolume() {
    Shutdown();
}

bool DensityVolume::Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap, VolumePreset preset) {
    m_descriptorHeap = descriptorHeap;
    m_currentPreset = preset;
    m_dimension = static_cast<uint32_t>(preset);

    // Create two 3D textures (A and B) with UAV support (ping-pong)
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
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_densityA)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create density volume texture A");
        return false;
    }

    hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_densityB)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create density volume texture B");
        return false;
    }

    m_densityA->SetName(L"DensityVolumeA");
    m_densityB->SetName(L"DensityVolumeB");

    // Allocate descriptors
    if (!m_descriptorHeap) {
        LOGE("Global descriptor heap not available");
        return false;
    }

    m_srvIndexA = m_descriptorHeap->Allocate();
    m_uavIndexA = m_descriptorHeap->Allocate();
    m_srvIndexB = m_descriptorHeap->Allocate();
    m_uavIndexB = m_descriptorHeap->Allocate();

    if (m_srvIndexA == UINT32_MAX || m_uavIndexA == UINT32_MAX ||
        m_srvIndexB == UINT32_MAX || m_uavIndexB == UINT32_MAX) {
        LOGE("Failed to allocate descriptors for density volume A/B");
        return false;
    }

    m_srvCpuA = m_descriptorHeap->GetCPUHandle(m_srvIndexA);
    m_srvGpuA = m_descriptorHeap->GetGPUHandle(m_srvIndexA);
    m_uavCpuA = m_descriptorHeap->GetCPUHandle(m_uavIndexA);
    m_uavGpuA = m_descriptorHeap->GetGPUHandle(m_uavIndexA);

    m_srvCpuB = m_descriptorHeap->GetCPUHandle(m_srvIndexB);
    m_srvGpuB = m_descriptorHeap->GetGPUHandle(m_srvIndexB);
    m_uavCpuB = m_descriptorHeap->GetCPUHandle(m_uavIndexB);
    m_uavGpuB = m_descriptorHeap->GetGPUHandle(m_uavIndexB);

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture3D.MipLevels = 1;
    device->CreateShaderResourceView(m_densityA.Get(), &srvDesc, m_srvCpuA);
    device->CreateShaderResourceView(m_densityB.Get(), &srvDesc, m_srvCpuB);

    // Create UAV
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uavDesc.Texture3D.MipSlice = 0;
    uavDesc.Texture3D.FirstWSlice = 0;
    uavDesc.Texture3D.WSize = m_dimension;
    device->CreateUnorderedAccessView(m_densityA.Get(), nullptr, &uavDesc, m_uavCpuA);
    device->CreateUnorderedAccessView(m_densityB.Get(), nullptr, &uavDesc, m_uavCpuB);

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
        // Increase constants to 8 to support sphere params (cx,cy,cz,radius,density,pad,pad,pad)
        rootParams[1].InitAsConstants(8, 0);

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

    // Advect root signature: SRV (t0), UAV (u0), constants b0
    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange, uavRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

        CD3DX12_ROOT_PARAMETER1 rootParams[3];
        rootParams[0].InitAsDescriptorTable(1, &srvRange);
        rootParams[1].InitAsDescriptorTable(1, &uavRange);
        rootParams[2].InitAsConstantBufferView(0);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> signature, error;
        if (FAILED(D3DX12SerializeVersionedRootSignature(&rootSigDesc,
            D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error))) {
            if (error) {
                LOGE("Advect root signature error");
            }
            return false;
        }

        HRESULT hr = device->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&m_advectRootSig));
        if (FAILED(hr)) {
            LOGE("Failed to create advect root signature");
            return false;
        }
    }

    return true;
}

bool DensityVolume::CreatePipelines(ComPtr<ID3D12Device5> device) {
    // Load compiled shaders
    std::vector<uint8_t> fillShader, sliceShader, advectShader;

    // Load density_fill.dxil
    {
        // Try multiple possible paths
        std::vector<std::string> possiblePaths = {
            "shaders/density_fill.dxil",
            "../shaders/density_fill.dxil",
            "../../shaders/density_fill.dxil"
        };

        std::ifstream file;
        std::string usedPath;

        for (const auto& path : possiblePaths) {
            file.open(path, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                usedPath = path;
                LOGI("Found density_fill.dxil at: " + path);
                break;
            }
        }

        if (!file.is_open()) {
            LOGW("density_fill.dxil not found in any expected location, density volume will use fallback");
            return true; // Non-fatal, we can still run
        }
        size_t size = file.tellg();
        fillShader.resize(size);
        file.seekg(0);
        file.read((char*)fillShader.data(), size);
    }

    // Load density_advect_curl.dxil (VOL_0004)
    {
        std::vector<std::string> possiblePaths = {
            "shaders/vol/density_advect_curl.dxil",
            "../shaders/vol/density_advect_curl.dxil",
            "../../shaders/vol/density_advect_curl.dxil"
        };

        std::ifstream file;
        for (const auto& path : possiblePaths) {
            file.open(path, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                size_t size = file.tellg();
                advectShader.resize(size);
                file.seekg(0);
                file.read((char*)advectShader.data(), size);
                LOGI(std::string("Found density_advect_curl.dxil at: ") + path);
                break;
            }
        }
    }

    // Load density_slice.dxil
    {
        std::vector<std::string> possiblePaths = {
            "shaders/density_slice.dxil",
            "../shaders/density_slice.dxil",
            "../../shaders/density_slice.dxil"
        };

        std::ifstream file;
        std::string usedPath;

        for (const auto& path : possiblePaths) {
            file.open(path, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                usedPath = path;
                LOGI("Found density_slice.dxil at: " + path);
                break;
            }
        }

        if (!file.is_open()) {
            LOGW("density_slice.dxil not found in any expected location, debug slice disabled");
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
            LOGE("Failed to create fill PSO, hr=0x" + std::to_string(hr));
            return false;
        }
        LOGI("Successfully created density fill PSO");
    }

    // Create slice PSO
    if (!sliceShader.empty()) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_sliceRootSig.Get();
        psoDesc.CS.pShaderBytecode = sliceShader.data();
        psoDesc.CS.BytecodeLength = sliceShader.size();

        HRESULT hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_slicePSO));
        if (FAILED(hr)) {
            LOGE("Failed to create slice PSO, hr=0x" + std::to_string(hr));
            return false;
        }
        LOGI("Successfully created density slice PSO");
    }

    // Create advect PSO
    if (!advectShader.empty()) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_advectRootSig.Get();
        psoDesc.CS.pShaderBytecode = advectShader.data();
        psoDesc.CS.BytecodeLength = advectShader.size();

        HRESULT hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_advectPSO));
        if (FAILED(hr)) {
            LOGE("Failed to create advect PSO, hr=0x" + std::to_string(hr));
            return false;
        }
        LOGI("Successfully created curl-advect PSO");

        // Create constant buffer for advect params
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(256);
        hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_advectCB));
        if (FAILED(hr)) {
            LOGE("Failed to create advect constant buffer");
            return false;
        }
    }

    LOGI("DensityVolume pipelines created successfully");
    return true;
}

void DensityVolume::Shutdown() {
    if (m_descriptorHeap) {
        if (m_srvIndexA != UINT32_MAX) { m_descriptorHeap->Free(m_srvIndexA); m_srvIndexA = UINT32_MAX; }
        if (m_uavIndexA != UINT32_MAX) { m_descriptorHeap->Free(m_uavIndexA); m_uavIndexA = UINT32_MAX; }
        if (m_srvIndexB != UINT32_MAX) { m_descriptorHeap->Free(m_srvIndexB); m_srvIndexB = UINT32_MAX; }
        if (m_uavIndexB != UINT32_MAX) { m_descriptorHeap->Free(m_uavIndexB); m_uavIndexB = UINT32_MAX; }
    }

    m_advectCB.Reset();
    m_advectPSO.Reset();
    m_advectRootSig.Reset();
    m_slicePSO.Reset();
    m_fillPSO.Reset();
    m_sliceRootSig.Reset();
    m_fillRootSig.Reset();
    m_densityA.Reset();
    m_densityB.Reset();
}

void DensityVolume::FillAnalytic(ComPtr<ID3D12GraphicsCommandList4> cmdList, float time) {
    // Debug the early return condition
    if (++m_frameCounter % 120 == 0) {
        const bool hasTex = (m_densityA || m_densityB);
        LOGI("DensityVolume::FillAnalytic called - PSO:" + std::string(m_fillPSO ? "OK" : "NULL") +
             " RootSig:" + std::string(m_fillRootSig ? "OK" : "NULL") +
             " Textures:" + std::string(hasTex ? "OK" : "NULL"));
    }

    if (!m_fillPSO || !m_fillRootSig || (!m_densityA && !m_densityB)) {
        // For now, just ensure UAV state
        return;
    }

    // Ensure UAV state for both A and B
    transitionResource(cmdList, m_densityA.Get(), m_stateA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transitionResource(cmdList, m_densityB.Get(), m_stateB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Log density fill dispatch every 2 seconds
    if (m_frameCounter % 120 == 0) {
        LOGI("DensityVolume: Dispatching density fill compute shader");
    }

    cmdList->SetComputeRootSignature(m_fillRootSig.Get());
    cmdList->SetPipelineState(m_fillPSO.Get());

    // Set constants: time, scale, center
    float constants[4] = { time, 0.5f, 0.5f, 0.5f };
    cmdList->SetComputeRoot32BitConstants(1, 4, constants, 0);

    // Dispatch with 8x8x8 thread groups
    uint32_t groups = (m_dimension + 7) / 8;
    // Fill A
    cmdList->SetComputeRootDescriptorTable(0, m_uavGpuA);
    cmdList->Dispatch(groups, groups, groups);
    // Fill B
    cmdList->SetComputeRootDescriptorTable(0, m_uavGpuB);
    cmdList->Dispatch(groups, groups, groups);

    // UAV barrier to ensure writes complete
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; barriers[0].UAV.pResource = m_densityA.Get();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; barriers[1].UAV.pResource = m_densityB.Get();
    cmdList->ResourceBarrier(2, barriers);
}

void DensityVolume::FillAnalyticSphere(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                                       DirectX::XMFLOAT3 centerUVW, float radiusUVW, float densityValue) {
    // Ensure UAV state for writing density to both A and B
    transitionResource(cmdList, m_densityA.Get(), m_stateA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transitionResource(cmdList, m_densityB.Get(), m_stateB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Load or reuse a sphere fill PSO: reusing m_fillPSO and m_fillRootSig by pointing to new shader if needed.
    // For simplicity, reuse the existing fill root signature (UAV + 4 constants) which matches our needs.

    // Attempt to load sphere shader DXIL
    std::vector<uint8_t> sphereShader;
    {
        std::vector<std::string> possiblePaths = {
            "shaders/vol/density_fill_sphere.dxil",
            "shaders/density_fill_sphere.dxil",
            "../shaders/vol/density_fill_sphere.dxil"
        };
        std::ifstream file;
        for (const auto& path : possiblePaths) {
            file.open(path, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                size_t sz = file.tellg();
                sphereShader.resize(sz);
                file.seekg(0);
                file.read((char*)sphereShader.data(), sz);
                LOGI(std::string("Found density_fill_sphere.dxil at: ") + path);
                break;
            }
        }
    }

    // If not found, fallback to existing procedural fill
    if (sphereShader.empty()) {
        LOGW("density_fill_sphere.dxil not found; falling back to FillAnalytic");
        FillAnalytic(cmdList, 0.0f);
        return;
    }

    // Create a temporary PSO for sphere fill
    ComPtr<ID3D12PipelineState> spherePSO;
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_fillRootSig.Get();
        psoDesc.CS.pShaderBytecode = sphereShader.data();
        psoDesc.CS.BytecodeLength = sphereShader.size();
        // CreateComputePipelineState requires device; reuse m_densityTexture device via GetDevice
        ComPtr<ID3D12Device> dev;
        m_densityA->GetDevice(IID_PPV_ARGS(&dev));
        HRESULT hr = dev->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&spherePSO));
        if (FAILED(hr)) {
            LOGE("Failed to create sphere fill PSO");
            return;
        }
    }

    // Dispatch sphere fill to both A and B
    cmdList->SetComputeRootSignature(m_fillRootSig.Get());
    cmdList->SetPipelineState(spherePSO.Get());
    // Pack constants: center.x, center.y, center.z, radius, densityValue, pad
    float constants[8] = { centerUVW.x, centerUVW.y, centerUVW.z, radiusUVW, densityValue, 0.0f, 0.0f, 0.0f };
    cmdList->SetComputeRoot32BitConstants(1, 8, constants, 0);

    uint32_t groups = (m_dimension + 7) / 8;
    // A
    cmdList->SetComputeRootDescriptorTable(0, m_uavGpuA);
    cmdList->Dispatch(groups, groups, groups);
    // B
    cmdList->SetComputeRootDescriptorTable(0, m_uavGpuB);
    cmdList->Dispatch(groups, groups, groups);

    // UAV barrier to ensure writes visible to SRV readers
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; barriers[0].UAV.pResource = m_densityA.Get();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; barriers[1].UAV.pResource = m_densityB.Get();
    cmdList->ResourceBarrier(2, barriers);
}

void DensityVolume::DebugSlice(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                               ComPtr<ID3D12Resource> hdrTarget,
                               D3D12_GPU_DESCRIPTOR_HANDLE hdrUav,
                               uint32_t sliceZ) {
    if (!m_slicePSO || !m_sliceRootSig || (!m_densityA && !m_densityB) || !hdrTarget) {
        return;
    }

    // Transition density to SRV for reading
    TransitionToSRV(cmdList);

    // Note: HDR target should already be in UAV state from caller

    cmdList->SetComputeRootSignature(m_sliceRootSig.Get());
    cmdList->SetPipelineState(m_slicePSO.Get());
    cmdList->SetComputeRootDescriptorTable(0, GetSRV());
    cmdList->SetComputeRootDescriptorTable(1, hdrUav);

    uint32_t constants[2] = { sliceZ, m_dimension };
    cmdList->SetComputeRoot32BitConstants(2, 2, constants, 0);

    // Dispatch to cover HDR resolution (assuming 1920x1080)
    cmdList->Dispatch((1920 + 15) / 16, (1080 + 15) / 16, 1);
}

void DensityVolume::TransitionToSRV(ComPtr<ID3D12GraphicsCommandList4> cmdList) {
    if (m_srcIsA) {
        transitionResource(cmdList, m_densityA.Get(), m_stateA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else {
        transitionResource(cmdList, m_densityB.Get(), m_stateB, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}

void DensityVolume::TransitionToUAV(ComPtr<ID3D12GraphicsCommandList4> cmdList) {
    if (m_srcIsA) {
        transitionResource(cmdList, m_densityA.Get(), m_stateA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    } else {
        transitionResource(cmdList, m_densityB.Get(), m_stateB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
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

void DensityVolume::transitionResource(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                                       ID3D12Resource* resource,
                                       D3D12_RESOURCE_STATES& currentState,
                                       D3D12_RESOURCE_STATES targetState) {
    if (!resource || currentState == targetState) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = currentState;
    barrier.Transition.StateAfter = targetState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
    currentState = targetState;
}

void DensityVolume::AdvectCurl(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                               float deltaTime,
                               float timeSeconds) {
    if (!m_advectPSO || !m_advectRootSig || !m_advectCB) return;

    // Set heaps
    if (!m_descriptorHeap) return;
    ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap->GetHeap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    // Determine src/dst
    ID3D12Resource* src = m_srcIsA ? m_densityA.Get() : m_densityB.Get();
    ID3D12Resource* dst = m_srcIsA ? m_densityB.Get() : m_densityA.Get();
    D3D12_GPU_DESCRIPTOR_HANDLE srcSrv = m_srcIsA ? m_srvGpuA : m_srvGpuB;
    D3D12_GPU_DESCRIPTOR_HANDLE dstUav = m_srcIsA ? m_uavGpuB : m_uavGpuA;

    // Transitions: src → SRV, dst → UAV
    if (m_srcIsA) {
        transitionResource(cmdList, m_densityA.Get(), m_stateA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transitionResource(cmdList, m_densityB.Get(), m_stateB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    } else {
        transitionResource(cmdList, m_densityB.Get(), m_stateB, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transitionResource(cmdList, m_densityA.Get(), m_stateA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // Update constants
    struct AdvectParamsCB {
        float deltaTime;
        float time;
        float curlSpeed;
        float flowScale;
        float decay;
        float injectRate;
        float injectRadius;
        float gridDim;
    } params;
    params.deltaTime = deltaTime;
    params.time = timeSeconds;
    params.curlSpeed = 0.6f;
    params.flowScale = 3.0f;
    params.decay = 0.995f;
    params.injectRate = 0.015f;
    params.injectRadius = 0.25f;
    params.gridDim = float(m_dimension);

    void* mapped = nullptr;
    m_advectCB->Map(0, nullptr, &mapped);
    std::memcpy(mapped, &params, sizeof(params));
    m_advectCB->Unmap(0, nullptr);

    // Bind and dispatch
    cmdList->SetComputeRootSignature(m_advectRootSig.Get());
    cmdList->SetPipelineState(m_advectPSO.Get());
    cmdList->SetComputeRootDescriptorTable(0, srcSrv);
    cmdList->SetComputeRootDescriptorTable(1, dstUav);
    cmdList->SetComputeRootConstantBufferView(2, m_advectCB->GetGPUVirtualAddress());

    uint32_t groups = (m_dimension + 7) / 8;
    cmdList->Dispatch(groups, groups, groups);

    // UAV barrier on dst to ensure writes visible next frame
    D3D12_RESOURCE_BARRIER uav{}; uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; uav.UAV.pResource = dst;
    cmdList->ResourceBarrier(1, &uav);

    // Swap src/dst for next frame
    m_srcIsA = !m_srcIsA;
}

void DensityVolume::FillMetaballs(ComPtr<ID3D12GraphicsCommandList4> cmdList, MetaballSystem* metaballSystem) {
    if (!metaballSystem || !m_densityA || !m_densityB) {
        LOGW("FillMetaballs: Invalid metaball system or density textures");
        return;
    }

    // Lazy initialize metaball pipeline if needed
    if (!m_metaballPSO || !m_metaballRootSig) {
        ComPtr<ID3D12Device> device;
        m_densityA->GetDevice(IID_PPV_ARGS(&device));

        // Load metaball shader
        std::vector<uint8_t> shaderData;
        std::vector<std::string> possiblePaths = {
            "shaders/vol/metaball_density_fill.dxil",
            "shaders/metaball_density_fill.dxil",
            "../shaders/vol/metaball_density_fill.dxil"
        };

        std::ifstream file;
        for (const auto& path : possiblePaths) {
            file.open(path, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                size_t size = file.tellg();
                file.seekg(0, std::ios::beg);
                shaderData.resize(size);
                file.read(reinterpret_cast<char*>(shaderData.data()), size);
                file.close();
                LOGI("Loaded metaball shader from: " + path);
                break;
            }
        }

        if (shaderData.empty()) {
            LOGW("Could not find metaball_density_fill.dxil, falling back to analytic fill");
            FillAnalytic(cmdList, 0.0f);
            return;
        }

        // Create root signature for metaball shader
        // Layout: UAV (u0), SRV for metaballs (t0), CBV for constants (b0)
        D3D12_ROOT_PARAMETER1 rootParams[3] = {};

        // UAV for density texture (u0)
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_DESCRIPTOR_RANGE1 uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[0].DescriptorTable.pDescriptorRanges = &uavRange;

        // SRV for metaball data (t0)
        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_DESCRIPTOR_RANGE1 srvRange = {};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;

        // CBV for metaball constants (b0)
        rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParams[2].Descriptor.ShaderRegister = 0;
        rootParams[2].Descriptor.RegisterSpace = 0;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSigDesc.Desc_1_1.NumParameters = 3;
        rootSigDesc.Desc_1_1.pParameters = rootParams;
        rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signature, &error);
        if (FAILED(hr)) {
            LOGE("Failed to serialize metaball root signature");
            return;
        }

        hr = device->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&m_metaballRootSig));
        if (FAILED(hr)) {
            LOGE("Failed to create metaball root signature");
            return;
        }

        // Create PSO
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_metaballRootSig.Get();
        psoDesc.CS.pShaderBytecode = shaderData.data();
        psoDesc.CS.BytecodeLength = shaderData.size();

        hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_metaballPSO));
        if (FAILED(hr)) {
            LOGE("Failed to create metaball PSO");
            return;
        }

        LOGI("Metaball pipeline initialized successfully");
    }

    // Update metaball physics and upload to GPU
    metaballSystem->UploadToGPU(cmdList);

    // Transition density textures to UAV state
    transitionResource(cmdList, m_densityA.Get(), m_stateA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transitionResource(cmdList, m_densityB.Get(), m_stateB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Set compute pipeline
    cmdList->SetComputeRootSignature(m_metaballRootSig.Get());
    cmdList->SetPipelineState(m_metaballPSO.Get());

    // Bind UAV for current density texture
    D3D12_GPU_DESCRIPTOR_HANDLE currentUAV = m_srcIsA ? m_uavGpuA : m_uavGpuB;
    cmdList->SetComputeRootDescriptorTable(0, currentUAV);

    // Bind SRV for metaball structured buffer
    cmdList->SetComputeRootDescriptorTable(1, metaballSystem->GetMetaballSRV());

    // Bind metaball constant buffer
    cmdList->SetComputeRootConstantBufferView(2, metaballSystem->GetConstantBuffer()->GetGPUVirtualAddress());

    // Dispatch compute shader
    uint32_t groups = (m_dimension + 7) / 8;
    cmdList->Dispatch(groups, groups, groups);

    // UAV barrier to ensure writes are visible
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_srcIsA ? m_densityA.Get() : m_densityB.Get();
    cmdList->ResourceBarrier(1, &barrier);

    LOGI("Metaball density fill dispatched successfully");
}