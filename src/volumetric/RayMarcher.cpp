#include "RayMarcher.h"
#include "../utils/DescriptorHeap.h"
#include "../core/Camera.h"
#include "../utils/Logger.h"
#include "DensityVolume.h"
#include <d3dcompiler.h>
#include "../../include/d3dx12/d3dx12.h"
#include <pix.h>

struct CameraConstants {
    XMMATRIX viewMatrix;
    XMMATRIX projMatrix;
    XMMATRIX invViewProjMatrix;
    XMFLOAT3 cameraPosition;
    float nearPlane;
    XMFLOAT3 cameraForward;
    float farPlane;
};

struct VolumeConstants {
    XMFLOAT3 volumeMin;
    float densityScale;
    XMFLOAT3 volumeMax;
    float absorption;
    XMFLOAT3 lightDirection;
    float stepSize;
    XMFLOAT3 lightColor;
    uint32_t maxSteps;
    XMFLOAT2 screenSize;
    float time;
    float exposure;
};

RayMarcher::RayMarcher() {
}

RayMarcher::~RayMarcher() {
    Shutdown();
}

bool RayMarcher::Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap) {
    LOGI("Initializing RayMarcher (VOL_0003)...");
    m_descriptorHeap = descriptorHeap;

    if (!CreateRootSignature(device)) {
        LOGE("Failed to create RayMarcher root signature");
        return false;
    }

    if (!CreatePipeline(device)) {
        LOGE("Failed to create RayMarcher pipeline");
        return false;
    }

    if (!CreateSampler(device)) {
        LOGE("Failed to create RayMarcher sampler");
        return false;
    }

    if (!CreateConstantBuffers(device)) {
        LOGE("Failed to create RayMarcher constant buffers");
        return false;
    }

    LOGI("RayMarcher initialized successfully");
    return true;
}

void RayMarcher::Shutdown() {
    m_rootSignature.Reset();
    m_pipelineState.Reset();
    m_cameraConstantBuffer.Reset();
    m_volumeConstantBuffer.Reset();
    m_samplerHeap.Reset();
}

bool RayMarcher::CreateRootSignature(ComPtr<ID3D12Device5> device) {
    CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

    CD3DX12_ROOT_PARAMETER1 rootParams[5];
    rootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[2].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
    rootParams[3].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
    rootParams[4].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr,
                         D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1,
                                                        &signature, &error);
    if (FAILED(hr)) {
        if (error) {
            LOGE(std::string("Root signature compilation error: ") + (char*)error->GetBufferPointer());
        }
        return false;
    }

    hr = device->CreateRootSignature(0, signature->GetBufferPointer(),
                                      signature->GetBufferSize(),
                                      IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(hr)) {
        LOGE("Failed to create root signature: 0x" + std::to_string(hr));
        return false;
    }

    return true;
}

bool RayMarcher::CreatePipeline(ComPtr<ID3D12Device5> device) {
    // Load compiled shader
    std::wstring shaderPath = L"shaders/vol/ray_march_cs.dxil";
    ComPtr<ID3DBlob> computeShader;

    HRESULT hr = D3DReadFileToBlob(shaderPath.c_str(), &computeShader);
    if (FAILED(hr)) {
        LOGE("Failed to load raymarch shader: 0x" + std::to_string(hr));
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
    if (FAILED(hr)) {
        LOGE("Failed to create compute pipeline state: 0x" + std::to_string(hr));
        return false;
    }

    return true;
}

bool RayMarcher::CreateSampler(ComPtr<ID3D12Device5> device) {
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = 1;
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerHeap));
    if (FAILED(hr)) {
        LOGE("Failed to create sampler heap: 0x" + std::to_string(hr));
        return false;
    }

    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.MipLODBias = 0;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

    D3D12_CPU_DESCRIPTOR_HANDLE samplerHandle = m_samplerHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateSampler(&samplerDesc, samplerHandle);

    return true;
}

bool RayMarcher::CreateConstantBuffers(ComPtr<ID3D12Device5> device) {
    // Camera constant buffer (256-byte aligned)
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(256);

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_cameraConstantBuffer));

    if (FAILED(hr)) {
        LOGE("Failed to create camera constant buffer: 0x" + std::to_string(hr));
        return false;
    }

    // Volume constant buffer (256-byte aligned)
    hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_volumeConstantBuffer));

    if (FAILED(hr)) {
        LOGE("Failed to create volume constant buffer: 0x" + std::to_string(hr));
        return false;
    }

    return true;
}

void RayMarcher::SetLightDirection(XMFLOAT3 dir) {
    XMStoreFloat3(&m_params.lightDirection, XMVector3Normalize(XMLoadFloat3(&dir)));
}

void RayMarcher::March(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                      DensityVolume* densityVolume,
                      ComPtr<ID3D12Resource> hdrTarget,
                      D3D12_GPU_DESCRIPTOR_HANDLE hdrUav,
                      Camera* camera,
                      float time) {

    PIXBeginEvent(cmdList.Get(), PIX_COLOR(0, 255, 0), "RayMarcher::March");

    // Update camera constants
    if (camera && m_cameraConstantBuffer) {
        CameraConstants* cameraData;
        m_cameraConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&cameraData));

        cameraData->viewMatrix = XMMatrixTranspose(camera->GetViewMatrix());
        cameraData->projMatrix = XMMatrixTranspose(camera->GetProjectionMatrix());
        cameraData->invViewProjMatrix = XMMatrixTranspose(camera->GetInvViewProjMatrix());
        cameraData->cameraPosition = camera->GetPosition();
        cameraData->nearPlane = camera->GetNearPlane();
        cameraData->cameraForward = camera->GetForward();
        cameraData->farPlane = camera->GetFarPlane();

        m_cameraConstantBuffer->Unmap(0, nullptr);
    }

    // Update volume constants
    if (m_volumeConstantBuffer) {
        VolumeConstants* volumeData;
        m_volumeConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&volumeData));

        volumeData->volumeMin = m_params.volumeMin;
        volumeData->densityScale = m_params.densityScale;
        volumeData->volumeMax = m_params.volumeMax;
        volumeData->absorption = m_params.absorption;
        volumeData->lightDirection = m_params.lightDirection;
        volumeData->stepSize = m_params.stepSize;
        volumeData->lightColor = m_params.lightColor;
        volumeData->maxSteps = m_params.maxSteps;
        volumeData->screenSize = m_params.screenSize;
        volumeData->time = time;
        volumeData->exposure = m_params.exposure;

        m_volumeConstantBuffer->Unmap(0, nullptr);
    }

    // HDR target should already be in UAV state from caller

    // Set pipeline state and root signature
    cmdList->SetPipelineState(m_pipelineState.Get());
    cmdList->SetComputeRootSignature(m_rootSignature.Get());

    // Bind resources
    cmdList->SetComputeRootConstantBufferView(0, m_cameraConstantBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootConstantBufferView(1, m_volumeConstantBuffer->GetGPUVirtualAddress());

    // Bind density SRV
    if (densityVolume) {
        D3D12_GPU_DESCRIPTOR_HANDLE densitySrv = densityVolume->GetSRV();
        cmdList->SetComputeRootDescriptorTable(2, densitySrv);
    }

    // Bind HDR UAV
    cmdList->SetComputeRootDescriptorTable(3, hdrUav);

    // Bind sampler
    ID3D12DescriptorHeap* heaps[] = { m_samplerHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    cmdList->SetComputeRootDescriptorTable(4, m_samplerHeap->GetGPUDescriptorHandleForHeapStart());

    // Dispatch compute shader
    PIXBeginEvent(cmdList.Get(), PIX_COLOR(0, 255, 255), "Dispatch RayMarch");
    uint32_t threadsX = (uint32_t(m_params.screenSize.x) + 15) / 16;
    uint32_t threadsY = (uint32_t(m_params.screenSize.y) + 15) / 16;
    cmdList->Dispatch(threadsX, threadsY, 1);
    PIXEndEvent(cmdList.Get());

    // UAV barrier to ensure writes complete
    PIXBeginEvent(cmdList.Get(), PIX_COLOR(255, 255, 0), "UAV Barrier");
    D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(hdrTarget.Get());
    cmdList->ResourceBarrier(1, &uavBarrier);
    PIXEndEvent(cmdList.Get());

    // Leave HDR in UAV state for caller to manage

    PIXEndEvent(cmdList.Get());

    // Log parameters every second
    m_frameCounter++;
    if (m_frameCounter % 60 == 0) {
        LOGI("RayMarcher: steps=" + std::to_string(m_params.maxSteps) +
             ", stepSize=" + std::to_string(m_params.stepSize) +
             ", density=" + std::to_string(m_params.densityScale) +
             ", exposure=" + std::to_string(m_params.exposure));
    }
}