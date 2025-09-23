#include "Particles.h"
#include "../utils/Logger.h"
#include "../utils/FileLoader.h"
#include <d3dcompiler.h>
#include <random>
#include <algorithm>

Particles::Particles(ID3D12Device* device)
    : m_device(device), m_particleCount(0), m_mappedConstants(nullptr) {
    if (!device) {
        throw std::runtime_error("Particles: null device");
    }
}

bool Particles::Initialize(uint32_t particleCount) {
    m_particleCount = particleCount;

    LOGI("Particles: Initializing with " + std::to_string(m_particleCount) + " particles");

    if (!CreateRootSignatures()) {
        LOGE("Particles: Failed to create root signatures");
        return false;
    }

    if (!CreateResources()) {
        LOGE("Particles: Failed to create resources");
        return false;
    }

    if (!CreatePipelineStates()) {
        LOGE("Particles: Failed to create pipeline states");
        return false;
    }

    // Initialize particle data on CPU
    InitializeParticleData();

    LOGI("Particles: Initialized successfully");
    return true;
}

bool Particles::CreateRootSignatures() {
    // Update compute shader root signature
    {
        D3D12_ROOT_PARAMETER params[2] = {};

        // Constant buffer (b0)
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].Descriptor.RegisterSpace = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // Particle buffer UAV (u0)
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 2;
        desc.pParameters = params;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
        if (FAILED(hr)) {
            if (error) {
                LOGE("Particles update root signature error: " + std::string((char*)error->GetBufferPointer()));
            }
            return false;
        }

        hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&m_updateRootSignature));
        if (FAILED(hr)) {
            LOGE("Particles: Failed to create update root signature");
            return false;
        }
    }

    // Debug pattern root signature
    {
        D3D12_ROOT_PARAMETER params[3] = {};
        D3D12_DESCRIPTOR_RANGE uavRange = {};

        // UAV descriptor table for RWTexture2D(u0)
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0; // u0
        uavRange.RegisterSpace = 0;
        uavRange.OffsetInDescriptorsFromTableStart = 0;

        // Constant buffer (b0)
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].Descriptor.RegisterSpace = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // Particle buffer SRV (t0) as root SRV (buffer). Note: shader will not read when particleCount==0
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // HDR texture UAV (u0) via descriptor table (textures cannot be bound via root UAV)
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges = &uavRange;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 3;
        desc.pParameters = params;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
        if (FAILED(hr)) {
            if (error) {
                LOGE("Particles debug root signature error: " + std::string((char*)error->GetBufferPointer()));
            }
            return false;
        }

        hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&m_debugRootSignature));
        if (FAILED(hr)) {
            LOGE("Particles: Failed to create debug root signature");
            return false;
        }
    }

    return true;
}

bool Particles::CreatePipelineStates() {
    // Load precompiled DXIL and create compute PSOs
    using Microsoft::WRL::ComPtr;

    // Update PSO
    {
        ComPtr<ID3DBlob> csBlob;
        std::string errorMessage;
        if (!FileLoader::LoadDXILShader("shaders/vol/particles_update.dxil", csBlob, errorMessage)) {
            LOGE("Particles: Failed to load update CS DXIL: " + errorMessage);
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_updateRootSignature.Get();
        desc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };

        HRESULT hr = m_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_updatePipelineState));
        if (FAILED(hr)) {
            LOGE("Particles: Failed to create update compute PSO");
            return false;
        }
    }

    // Debug pattern PSO
    {
        ComPtr<ID3DBlob> csBlob;
        std::string errorMessage;
        if (!FileLoader::LoadDXILShader("shaders/vol/particles_debug_pattern.dxil", csBlob, errorMessage)) {
            LOGE("Particles: Failed to load debug pattern CS DXIL: " + errorMessage);
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_debugRootSignature.Get();
        desc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };

        HRESULT hr = m_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_debugPipelineState));
        if (FAILED(hr)) {
            LOGE("Particles: Failed to create debug pattern compute PSO");
            return false;
        }
    }

    LOGI("Particles: Compute PSOs created (update + debug pattern)");
    return true;
}

bool Particles::CreateResources() {
    // Create particle structured buffer
    {
        size_t bufferSize = m_particleCount * sizeof(Particle);

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = bufferSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.SampleDesc.Quality = 0;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        HRESULT hr = m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                       IID_PPV_ARGS(&m_particleBuffer));
        if (FAILED(hr)) {
            LOGE("Particles: Failed to create particle buffer");
            return false;
        }

        LOGI("Particles: Created buffer for " + std::to_string(m_particleCount) + " particles (" +
             std::to_string(bufferSize / 1024) + " KB)");
    }

    // Create constant buffer
    {
        size_t cbSize = (sizeof(ParticleConstants) + 255) & ~255;  // 256-byte align

        D3D12_RESOURCE_DESC cbDesc = {};
        cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width = cbSize;
        cbDesc.Height = 1;
        cbDesc.DepthOrArraySize = 1;
        cbDesc.MipLevels = 1;
        cbDesc.Format = DXGI_FORMAT_UNKNOWN;
        cbDesc.SampleDesc.Count = 1;
        cbDesc.SampleDesc.Quality = 0;
        cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        cbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        HRESULT hr = m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
                                                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                       IID_PPV_ARGS(&m_constantBuffer));
        if (FAILED(hr)) {
            LOGE("Particles: Failed to create constant buffer");
            return false;
        }

        // Persistently map the constant buffer
        hr = m_constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedConstants));
        if (FAILED(hr)) {
            LOGE("Particles: Failed to map constant buffer");
            return false;
        }

        // Initialize constants with default values
        m_mappedConstants->deltaTime = 0.016f;  // 60 FPS
        m_mappedConstants->totalTime = 0.0f;
        m_mappedConstants->worldBounds = 10.0f;  // [-10, 10] cube
        m_mappedConstants->particleCount = m_particleCount;
        m_mappedConstants->gravity = XMFLOAT3(0.0f, -9.8f, 0.0f);
        m_mappedConstants->damping = 0.98f;
    }

    return true;
}

void Particles::InitializeParticleData() {
    // Create initial particle data on CPU, then upload to GPU
    std::vector<Particle> particles(m_particleCount);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-5.0f, 5.0f);
    std::uniform_real_distribution<float> velDist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> lifeDist(0.5f, 1.0f);

    for (size_t i = 0; i < m_particleCount; ++i) {
        particles[i].position = XMFLOAT3(posDist(gen), posDist(gen), posDist(gen));
        particles[i].velocity = XMFLOAT3(velDist(gen), velDist(gen), velDist(gen));
        particles[i].life = lifeDist(gen);
        particles[i].padding = 0.0f;
    }

    // Upload to GPU (we'll need a staging buffer for this)
    // For now, just log that we would upload
    LOGI("Particles: Initialized " + std::to_string(m_particleCount) + " particles with random data");
    LOGW("Particles: GPU upload not implemented yet - particles will be zero on GPU");
}

void Particles::Update(ID3D12GraphicsCommandList* cmdList, float deltaTime, float totalTime) {
    // Update constants
    if (m_mappedConstants) {
        m_mappedConstants->deltaTime = deltaTime;
        m_mappedConstants->totalTime = totalTime;
    }

    // For now, just log the update (no shader dispatch yet)
    static int frameCount = 0;
    if (++frameCount % 60 == 0) {  // Log every 60 frames
        LOGI("Particles: Update frame " + std::to_string(frameCount) +
             " (dt=" + std::to_string(deltaTime) + "s, t=" + std::to_string(totalTime) + "s)");
    }

    // TODO: Dispatch compute shader when available
    // PIX_SCOPED_EVENT(cmdList, "Particles Update");
    // cmdList->SetComputeRootSignature(m_updateRootSignature.Get());
    // cmdList->SetPipelineState(m_updatePipelineState.Get());
    // cmdList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    // cmdList->SetComputeRootUnorderedAccessView(1, m_particleBuffer->GetGPUVirtualAddress());
    // cmdList->Dispatch((m_particleCount + 255) / 256, 1, 1);
}

void Particles::WriteDebugPattern(ID3D12GraphicsCommandList* cmdList,
                                  ID3D12Resource* hdrTexture,
                                  D3D12_GPU_DESCRIPTOR_HANDLE hdrUavHandle,
                                  uint32_t width, uint32_t height) {
    // Dispatch compute shader that writes a debug pattern to the HDR UAV
    if (!m_debugPipelineState || !m_debugRootSignature) {
        LOGW("Particles: Debug PSO/RootSignature not ready; skipping debug dispatch");
        return;
    }

    // Ensure shader won't read particles SRV if we haven't uploaded data yet
    if (m_mappedConstants) {
        m_mappedConstants->particleCount = 0; // Avoid reading uninitialized SRV
    }

    // Bind root signature and pipeline
    cmdList->SetComputeRootSignature(m_debugRootSignature.Get());
    cmdList->SetPipelineState(m_debugPipelineState.Get());

    // Set constants (b0)
    cmdList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());

    // Set particle buffer SRV (t0) as root SRV (buffer). Safe since particleCount==0
    if (m_particleBuffer) {
        cmdList->SetComputeRootShaderResourceView(1, m_particleBuffer->GetGPUVirtualAddress());
    }

    // Set HDR UAV (u0) via descriptor table
    cmdList->SetComputeRootDescriptorTable(2, hdrUavHandle);

    // Dispatch over the HDR texture size
    const UINT groupSizeX = 8;
    const UINT groupSizeY = 8;
    UINT dispatchX = (width  + groupSizeX - 1) / groupSizeX;
    UINT dispatchY = (height + groupSizeY - 1) / groupSizeY;
    cmdList->Dispatch(dispatchX, dispatchY, 1);
}