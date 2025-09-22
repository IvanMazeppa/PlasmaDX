#include "VolumeRenderer.h"
#include "DensityVolume.h"
#include "../core/Camera.h"
#include "../utils/Logger.h"
#include "../utils/DescriptorHeap.h"
#include <d3dx12/d3dx12.h>
#include <d3dcompiler.h>
#include <fstream>
#include <vector>

VolumeRenderer::VolumeRenderer()
    : m_frameCounter(0) {
    // Initialize light direction to normalized vector
    XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&m_params.lightDirection));
    XMStoreFloat3(&m_params.lightDirection, lightDir);
}

VolumeRenderer::~VolumeRenderer() {
    Shutdown();
}

bool VolumeRenderer::Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap) {
    m_descriptorHeap = descriptorHeap;

    // Create sampler heap
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.NumDescriptors = 1;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerHeap));
    if (FAILED(hr)) {
        LOGE("Failed to create sampler heap");
        return false;
    }

    // Create sampler
    if (!CreateSampler(device)) {
        LOGE("Failed to create sampler");
        return false;
    }

    // Create constant buffers
    const UINT constantBufferSize = (sizeof(XMFLOAT4X4) * 3 + sizeof(XMFLOAT3) * 2 + sizeof(float) * 2 + 255) & ~255; // Camera constants
    const UINT volumeBufferSize = (sizeof(VolumeRenderParams) + 255) & ~255; // Volume constants

    // Camera constant buffer
    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);

        hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_cameraConstantBuffer)
        );

        if (FAILED(hr)) {
            LOGE("Failed to create camera constant buffer");
            return false;
        }

        m_cameraConstantBuffer->SetName(L"VolumeRenderer_CameraConstants");
    }

    // Volume constant buffer
    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(volumeBufferSize);

        hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_volumeConstantBuffer)
        );

        if (FAILED(hr)) {
            LOGE("Failed to create volume constant buffer");
            return false;
        }

        m_volumeConstantBuffer->SetName(L"VolumeRenderer_VolumeConstants");
    }

    // Create root signature and pipeline
    if (!CreateRootSignature(device)) {
        LOGE("Failed to create root signature");
        return false;
    }

    if (!CreatePipeline(device)) {
        LOGE("Failed to create pipeline");
        return false;
    }

    LOGI("VolumeRenderer initialized successfully");
    return true;
}

bool VolumeRenderer::CreateSampler(ComPtr<ID3D12Device5> device) {
    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

    device->CreateSampler(&samplerDesc, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool VolumeRenderer::CreateRootSignature(ComPtr<ID3D12Device5> device) {
    // Root signature: [0] Density SRV, [1] HDR UAV, [2] Camera CBV, [3] Volume CBV, [4] Sampler
    CD3DX12_DESCRIPTOR_RANGE1 srvRange, uavRange, samplerRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0
    samplerRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0); // s0

    CD3DX12_ROOT_PARAMETER1 rootParams[5];
    rootParams[0].InitAsDescriptorTable(1, &srvRange); // Density SRV
    rootParams[1].InitAsDescriptorTable(1, &uavRange); // HDR UAV
    rootParams[2].InitAsConstantBufferView(0); // Camera constants b0
    rootParams[3].InitAsConstantBufferView(1); // Volume constants b1
    rootParams[4].InitAsDescriptorTable(1, &samplerRange); // Sampler s0

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3DX12SerializeVersionedRootSignature(&rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error))) {
        if (error) {
            LOGE("Ray march root signature error");
        }
        return false;
    }

    HRESULT hr = device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(hr)) {
        LOGE("Failed to create ray march root signature");
        return false;
    }

    return true;
}

bool VolumeRenderer::CreatePipeline(ComPtr<ID3D12Device5> device) {
    // Load compiled shader
    std::vector<uint8_t> shaderBytecode;

    std::ifstream file("shaders/raymarch.dxil", std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOGW("raymarch.dxil not found, ray marching disabled");
        return true; // Non-fatal
    }

    size_t size = file.tellg();
    shaderBytecode.resize(size);
    file.seekg(0);
    file.read((char*)shaderBytecode.data(), size);

    // Create PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.CS.pShaderBytecode = shaderBytecode.data();
    psoDesc.CS.BytecodeLength = shaderBytecode.size();

    HRESULT hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
    if (FAILED(hr)) {
        LOGE("Failed to create ray march PSO");
        return false;
    }

    LOGI("Ray march pipeline created successfully");
    return true;
}

void VolumeRenderer::Shutdown() {
    m_pipelineState.Reset();
    m_rootSignature.Reset();
    m_samplerHeap.Reset();
    m_volumeConstantBuffer.Reset();
    m_cameraConstantBuffer.Reset();
}

void VolumeRenderer::SetLightDirection(XMFLOAT3 dir) {
    XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&dir));
    XMStoreFloat3(&m_params.lightDirection, lightDir);
}

void VolumeRenderer::RenderVolume(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                                  DensityVolume* densityVolume,
                                  ComPtr<ID3D12Resource> hdrTarget,
                                  D3D12_GPU_DESCRIPTOR_HANDLE hdrUav,
                                  Camera* camera,
                                  float time) {
    if (!m_pipelineState || !densityVolume || !camera) {
        return;
    }

    // Update parameters
    m_params.time = time;

    // Update camera constants
    {
        struct CameraConstants {
            XMFLOAT4X4 viewMatrix;
            XMFLOAT4X4 projMatrix;
            XMFLOAT4X4 invViewProjMatrix;
            XMFLOAT3 cameraPosition;
            float nearPlane;
            XMFLOAT3 cameraForward;
            float farPlane;
        };

        CameraConstants constants = {};

        // Get camera matrices
        XMMATRIX view = camera->GetViewMatrix();
        XMMATRIX proj = camera->GetProjectionMatrix();
        XMMATRIX viewProj = XMMatrixMultiply(view, proj);
        XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

        XMStoreFloat4x4(&constants.viewMatrix, XMMatrixTranspose(view));
        XMStoreFloat4x4(&constants.projMatrix, XMMatrixTranspose(proj));
        XMStoreFloat4x4(&constants.invViewProjMatrix, XMMatrixTranspose(invViewProj));

        constants.cameraPosition = camera->GetPosition();
        constants.cameraForward = camera->GetForward();
        constants.nearPlane = 0.1f;
        constants.farPlane = 100.0f;

        // Upload constants
        void* mappedData;
        m_cameraConstantBuffer->Map(0, nullptr, &mappedData);
        memcpy(mappedData, &constants, sizeof(constants));
        m_cameraConstantBuffer->Unmap(0, nullptr);
    }

    // Update volume constants
    {
        void* mappedData;
        m_volumeConstantBuffer->Map(0, nullptr, &mappedData);
        memcpy(mappedData, &m_params, sizeof(m_params));
        m_volumeConstantBuffer->Unmap(0, nullptr);
    }

    // Ensure density volume is in SRV state
    densityVolume->TransitionToSRV(cmdList);

    // Set pipeline state
    cmdList->SetComputeRootSignature(m_rootSignature.Get());
    cmdList->SetPipelineState(m_pipelineState.Get());

    // Set descriptor heaps
    ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap->GetHeap(), m_samplerHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    // Bind resources
    cmdList->SetComputeRootDescriptorTable(0, densityVolume->GetSRV()); // Density SRV
    cmdList->SetComputeRootDescriptorTable(1, hdrUav); // HDR UAV
    cmdList->SetComputeRootConstantBufferView(2, m_cameraConstantBuffer->GetGPUVirtualAddress()); // Camera CBV
    cmdList->SetComputeRootConstantBufferView(3, m_volumeConstantBuffer->GetGPUVirtualAddress()); // Volume CBV
    cmdList->SetComputeRootDescriptorTable(4, m_samplerHeap->GetGPUDescriptorHandleForHeapStart()); // Sampler

    // Dispatch ray marching
    uint32_t groupsX = (static_cast<uint32_t>(m_params.screenSize.x) + 15) / 16;
    uint32_t groupsY = (static_cast<uint32_t>(m_params.screenSize.y) + 15) / 16;
    cmdList->Dispatch(groupsX, groupsY, 1);

    // Log heartbeat every ~60 frames
    if (++m_frameCounter % 60 == 0) {
        LOGI("VolumeRenderer heartbeat - ray marching active");
    }
}