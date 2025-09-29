#include "ParticleSystem.h"
#include "../utils/Logger.h"
#include "../utils/FileLoader.h"
#include "../../include/d3dx12/d3dx12.h"
#include <cmath>
#include <random>

ParticleSystem::ParticleSystem()
    : m_particleCount(0)
    , m_totalTime(0.0f)
{
}

ParticleSystem::~ParticleSystem() {
    Shutdown();
}

bool ParticleSystem::Initialize(ID3D12Device* device, uint32_t particleCount) {
    m_device = device;
    m_particleCount = particleCount;

    // Query for ID3D12Device2 for mesh shader support
    HRESULT hr = m_device.As(&m_device2);
    if (FAILED(hr)) {
        LOGE("Failed to query ID3D12Device2 for mesh shader support: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    LOGI("Initializing ParticleSystem with " + std::to_string(particleCount) + " particles");

    if (!CreateBuffers()) {
        LOGE("Failed to create particle buffers");
        return false;
    }

    if (!CompileShaders()) {
        LOGE("Failed to compile particle shaders");
        return false;
    }

    if (!CreateComputePipeline()) {
        LOGE("Failed to create compute pipeline");
        return false;
    }

    if (!CreateMeshPipeline()) {
        LOGE("Failed to create mesh pipeline");
        return false;
    }

    InitializeAccretionDisk();

    LOGI("ParticleSystem initialized successfully");
    return true;
}

void ParticleSystem::Shutdown() {
    // Resources will be automatically released via ComPtr
}

bool ParticleSystem::CreateBuffers() {
    // Create particle buffer
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
        m_particleCount * sizeof(Particle),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_particleBuffer)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create particle buffer: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    // Create constants buffer for physics
    CD3DX12_RESOURCE_DESC constantsDesc = CD3DX12_RESOURCE_DESC::Buffer(
        (sizeof(ParticleConstants) + 255) & ~255 // Align to 256 bytes
    );

    hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &constantsDesc,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        nullptr,
        IID_PPV_ARGS(&m_particleConstantsBuffer)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create particle constants buffer: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    // Create render constants buffer (use UPLOAD heap for easy CPU updates)
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC renderConstantsDesc = CD3DX12_RESOURCE_DESC::Buffer(
        (sizeof(RenderConstants) + 255) & ~255 // Align to 256 bytes
    );

    hr = m_device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &renderConstantsDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_renderConstantsBuffer)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create render constants buffer: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    return true;
}

bool ParticleSystem::CompileShaders() {
    // Load pre-compiled DXIL shaders (same pattern as other systems)
    std::string errorMessage;

    // Load compute shader
    if (!FileLoader::LoadDXILShader("shaders/particles/particle_physics.dxil", m_computeShader, errorMessage)) {
        LOGE("Failed to load particle physics compute shader: " + errorMessage);
        return false;
    }

    // Load mesh shader
    if (!FileLoader::LoadDXILShader("shaders/particles/particle_mesh.dxil", m_meshShader, errorMessage)) {
        LOGE("Failed to load particle mesh shader: " + errorMessage);
        return false;
    }

    // Load pixel shader
    if (!FileLoader::LoadDXILShader("shaders/particles/particle_pixel.dxil", m_pixelShader, errorMessage)) {
        LOGE("Failed to load particle pixel shader: " + errorMessage);
        return false;
    }

    LOGI("Particle shaders loaded successfully");
    return true;
}

bool ParticleSystem::CreateComputePipeline() {
    // Create root signature for compute
    CD3DX12_ROOT_PARAMETER1 rootParams[2];
    rootParams[0].InitAsUnorderedAccessView(0); // Particle buffer
    rootParams[1].InitAsConstantBufferView(0);  // Constants

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams);

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        LOGE("Failed to serialize compute root signature");
        return false;
    }

    hr = m_device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&m_computeRootSig));
    if (FAILED(hr)) {
        LOGE("Failed to create compute root signature");
        return false;
    }

    // Create compute PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
    computeDesc.pRootSignature = m_computeRootSig.Get();
    computeDesc.CS = { m_computeShader->GetBufferPointer(), m_computeShader->GetBufferSize() };

    hr = m_device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&m_computePSO));
    if (FAILED(hr)) {
        LOGE("Failed to create compute PSO: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    return true;
}

bool ParticleSystem::CreateMeshPipeline() {
    // Create root signature for mesh pipeline
    CD3DX12_ROOT_PARAMETER1 rootParams[2];
    rootParams[0].InitAsShaderResourceView(0); // Particle buffer (SRV for mesh shader)
    rootParams[1].InitAsConstantBufferView(0); // Render constants

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams);

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        LOGE("Failed to serialize mesh root signature");
        return false;
    }

    hr = m_device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&m_meshRootSig));
    if (FAILED(hr)) {
        LOGE("Failed to create mesh root signature");
        return false;
    }

    // Create mesh pipeline state using pipeline state stream (DX12 spec compliant)
    struct MeshPipelineStateStream {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_MS meshShader;
        CD3DX12_PIPELINE_STATE_STREAM_PS pixelShader;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blendDesc;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depthStencil;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS renderTargetFormats;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC sampleDesc;
    } pipelineStateStream;

    // Root signature
    pipelineStateStream.rootSignature = m_meshRootSig.Get();

    // Mesh shader
    D3D12_SHADER_BYTECODE meshShaderBytecode = {};
    meshShaderBytecode.pShaderBytecode = m_meshShader->GetBufferPointer();
    meshShaderBytecode.BytecodeLength = m_meshShader->GetBufferSize();
    pipelineStateStream.meshShader = meshShaderBytecode;

    // Pixel shader
    D3D12_SHADER_BYTECODE pixelShaderBytecode = {};
    pixelShaderBytecode.pShaderBytecode = m_pixelShader->GetBufferPointer();
    pixelShaderBytecode.BytecodeLength = m_pixelShader->GetBufferSize();
    pipelineStateStream.pixelShader = pixelShaderBytecode;

    // Blend state for alpha blending
    CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipelineStateStream.blendDesc = blendDesc;

    // Depth stencil state
    CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Don't write depth for transparent particles
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pipelineStateStream.depthStencil = depthStencilDesc;

    // Rasterizer state
    CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // No culling for billboards
    pipelineStateStream.rasterizer = rasterizerDesc;

    // Render target formats
    D3D12_RT_FORMAT_ARRAY renderTargetFormats = {};
    renderTargetFormats.NumRenderTargets = 1;
    renderTargetFormats.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR format
    pipelineStateStream.renderTargetFormats = renderTargetFormats;

    // Sample description
    DXGI_SAMPLE_DESC sampleDesc = {};
    sampleDesc.Count = 1;
    sampleDesc.Quality = 0;
    pipelineStateStream.sampleDesc = sampleDesc;

    // Create pipeline state stream descriptor
    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;
    streamDesc.SizeInBytes = sizeof(pipelineStateStream);

    // Create the PSO using ID3D12Device2
    hr = m_device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_meshPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create mesh PSO: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    LOGI("Mesh shader pipeline created successfully");
    return true;
}

void ParticleSystem::InitializeAccretionDisk() {
    LOGI("Initializing NASA-quality accretion disk with " + std::to_string(m_particleCount) + " particles");

    // This would normally initialize particle positions in a buffer
    // For now, we'll set up the initial state via compute shader
}

void ParticleSystem::UpdatePhysics(ID3D12GraphicsCommandList* cmdList, float deltaTime) {
    m_totalTime += deltaTime;

    // Update constants
    ParticleConstants constants = {};
    constants.deltaTime = deltaTime;
    constants.totalTime = m_totalTime;
    constants.blackHoleMass = BLACK_HOLE_MASS;
    constants.gravityStrength = GRAVITY_CONSTANT;
    constants.blackHolePosition = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    constants.viscosity = 0.01f;
    constants.diskAxis = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
    constants.innerRadius = INNER_STABLE_ORBIT;
    constants.outerRadius = OUTER_DISK_RADIUS;
    constants.diskThickness = DISK_THICKNESS;
    constants.temperatureScale = 1.0f;
    constants.particleCount = static_cast<float>(m_particleCount);

    // Upload constants (simplified - in real implementation would use upload heap)
    // For now, just dispatch the compute shader
    cmdList->SetComputeRootSignature(m_computeRootSig.Get());
    cmdList->SetPipelineState(m_computePSO.Get());
    cmdList->SetComputeRootUnorderedAccessView(0, m_particleBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootConstantBufferView(1, m_particleConstantsBuffer->GetGPUVirtualAddress());

    // Dispatch with one thread per particle
    UINT groupCount = (m_particleCount + 63) / 64; // 64 threads per group
    cmdList->Dispatch(groupCount, 1, 1);

    // Barrier to ensure compute finishes before mesh shader reads
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_particleBuffer.Get());
    cmdList->ResourceBarrier(1, &barrier);
}

void ParticleSystem::RenderParticles(ID3D12GraphicsCommandList* cmdList,
                                   const DirectX::XMMATRIX& viewMatrix,
                                   const DirectX::XMMATRIX& projMatrix,
                                   D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                                   UINT width, UINT height) {
    // Query for ID3D12GraphicsCommandList6 for DispatchMesh support
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> cmdList6;
    HRESULT hr = cmdList->QueryInterface(IID_PPV_ARGS(&cmdList6));
    if (FAILED(hr)) {
        LOGE("Failed to query ID3D12GraphicsCommandList6 for DispatchMesh support");
        return;
    }

    // Set render targets and viewport for mesh shader rendering
    cmdList6->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Set viewport to match render target dimensions
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmdList6->RSSetViewports(1, &viewport);

    // Set scissor rect
    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(width);
    scissorRect.bottom = static_cast<LONG>(height);
    cmdList6->RSSetScissorRects(1, &scissorRect);

    // Update render constants
    RenderConstants renderConstants = {};
    renderConstants.viewMatrix = viewMatrix;
    renderConstants.projMatrix = projMatrix;
    renderConstants.cameraPos = DirectX::XMFLOAT3(0.0f, 0.0f, -5.0f); // Would get from camera
    renderConstants.particleSize = 0.1f;
    renderConstants.temperatureScale = 1.0f;

    // Upload render constants to GPU
    void* mappedData;
    hr = m_renderConstantsBuffer->Map(0, nullptr, &mappedData);
    if (SUCCEEDED(hr)) {
        memcpy(mappedData, &renderConstants, sizeof(RenderConstants));
        m_renderConstantsBuffer->Unmap(0, nullptr);
    } else {
        LOGE("Failed to map render constants buffer for upload");
        return;
    }

    // Set mesh pipeline
    cmdList6->SetGraphicsRootSignature(m_meshRootSig.Get());
    cmdList6->SetPipelineState(m_meshPSO.Get());
    cmdList6->SetGraphicsRootShaderResourceView(0, m_particleBuffer->GetGPUVirtualAddress());
    cmdList6->SetGraphicsRootConstantBufferView(1, m_renderConstantsBuffer->GetGPUVirtualAddress());

    // Dispatch mesh shader workgroups
    // Each workgroup handles 32 particles, creating 4 vertices and 2 triangles per particle
    UINT workgroupCount = (m_particleCount + 31) / 32;
    cmdList6->DispatchMesh(workgroupCount, 1, 1);
}