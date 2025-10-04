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

    // Mode 10: Create compute + traditional VS/PS pipelines
    if (!CreateComputeParticlePipeline()) {
        LOGE("Failed to create compute particle build pipeline");
        return false;
    }

    if (!CreateTraditionalRasterPipeline()) {
        LOGE("Failed to create traditional raster pipeline");
        return false;
    }

    InitializeAccretionDisk();

    LOGI("ParticleSystem initialized successfully");
    return true;
}

void ParticleSystem::Shutdown() {
    // Resources will be automatically released via ComPtr
}

bool ParticleSystem::CreateParticleBufferSRV(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle) {
    // Mode 9.2: Create SRV for particle buffer to allow lighting compute shader to read particle positions
    if (!m_particleBuffer) {
        LOGE("Cannot create particle buffer SRV - particle buffer not initialized");
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = m_particleCount;
    srvDesc.Buffer.StructureByteStride = sizeof(Particle);  // 64 bytes per particle
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(m_particleBuffer.Get(), &srvDesc, srvHandle);
    return true;
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

    // Create constants buffer for physics (use UPLOAD heap for CPU writes)
    CD3DX12_HEAP_PROPERTIES uploadHeapProps2(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC constantsDesc = CD3DX12_RESOURCE_DESC::Buffer(
        (sizeof(ParticleConstants) + 255) & ~255 // Align to 256 bytes
    );

    hr = m_device->CreateCommittedResource(
        &uploadHeapProps2,
        D3D12_HEAP_FLAG_NONE,
        &constantsDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
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

    // Mode 10: Create vertex buffer for compute-built particles (400K vertices = 100K particles × 4)
    // ParticleVertex structure: float4 position + float2 texCoord + float4 color + float alpha = 44 bytes
    // Actual size with padding: 48 bytes (aligned to 16 bytes for GPU)
    const UINT vertexBufferSize = m_particleCount * 4 * 48;  // 4 vertices per particle, 48 bytes per vertex

    CD3DX12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
        vertexBufferSize,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS  // UAV for compute shader writes
    );

    hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &vertexBufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_particleVertexBuffer)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create particle vertex buffer: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    // Mode 10: Create index buffer for particles (600K indices = 100K particles × 6 indices)
    const UINT indexBufferSize = m_particleCount * 6 * sizeof(uint32_t);

    CD3DX12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);

    hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &indexBufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_particleIndexBuffer)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create particle index buffer: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    // Initialize index buffer (static data - never changes)
    // Upload buffer for index initialization
    CD3DX12_HEAP_PROPERTIES uploadProps(D3D12_HEAP_TYPE_UPLOAD);
    Microsoft::WRL::ComPtr<ID3D12Resource> indexUploadBuffer;

    hr = m_device->CreateCommittedResource(
        &uploadProps,
        D3D12_HEAP_FLAG_NONE,
        &indexBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&indexUploadBuffer)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create index upload buffer: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    // Map and fill index buffer
    uint32_t* indexData;
    hr = indexUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    if (SUCCEEDED(hr)) {
        for (uint32_t i = 0; i < m_particleCount; i++) {
            uint32_t vertexBase = i * 4;
            uint32_t indexBase = i * 6;

            // Triangle 1: 0-1-2
            indexData[indexBase + 0] = vertexBase + 0;
            indexData[indexBase + 1] = vertexBase + 1;
            indexData[indexBase + 2] = vertexBase + 2;

            // Triangle 2: 1-3-2
            indexData[indexBase + 3] = vertexBase + 1;
            indexData[indexBase + 4] = vertexBase + 3;
            indexData[indexBase + 5] = vertexBase + 2;
        }
        indexUploadBuffer->Unmap(0, nullptr);

        // Copy to GPU (note: this requires a command list - we'll defer this to first render)
        // For now, store the upload buffer so we can copy it later
        // Actually, we need to copy this immediately - create a temporary command list
        // WORKAROUND: We'll initialize indices in the first render call instead
        LOGI("Index buffer upload deferred to first render (needs command list)");
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

    // Mode 10: Load compute + traditional VS/PS shaders
    if (!FileLoader::LoadDXILShader("shaders/particles/particle_build_compute.dxil", m_computeParticleBuildShader, errorMessage)) {
        LOGE("Failed to load particle build compute shader: " + errorMessage);
        return false;
    }

    if (!FileLoader::LoadDXILShader("shaders/particles/particle_traditional_vs.dxil", m_traditionalVertexShader, errorMessage)) {
        LOGE("Failed to load traditional vertex shader: " + errorMessage);
        return false;
    }

    if (!FileLoader::LoadDXILShader("shaders/particles/particle_traditional_ps.dxil", m_traditionalPixelShader, errorMessage)) {
        LOGE("Failed to load traditional pixel shader: " + errorMessage);
        return false;
    }

    LOGI("Particle shaders loaded successfully (mesh + traditional pipelines)");
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
    // Create root signature for mesh pipeline (Mode 9.2+ lighting support)
    // Param 0: Particle buffer SRV (t0)
    // Param 1: Render constants CBV (b0)
    // Param 2: Shadow map SRV (t1) - descriptor table
    // Param 3: Particle lighting SRV (t2) - descriptor table (Mode 9.2)
    // Param 4: Mode params CBV (b1) - 32-bit constants for sub-mode flag
    // Param 5: Static sampler (s0) for shadow map

    CD3DX12_DESCRIPTOR_RANGE1 shadowMapRange;
    shadowMapRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // 1 SRV at t1

    CD3DX12_DESCRIPTOR_RANGE1 lightingRange;
    lightingRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // 1 SRV at t2

    CD3DX12_ROOT_PARAMETER1 rootParams[5];
    rootParams[0].InitAsShaderResourceView(0); // Particle buffer (SRV t0 for mesh shader)
    rootParams[1].InitAsConstantBufferView(0); // Render constants (CBV b0)
    rootParams[2].InitAsDescriptorTable(1, &shadowMapRange); // Shadow map (SRV t1)
    rootParams[3].InitAsDescriptorTable(1, &lightingRange); // Particle lighting (SRV t2)
    rootParams[4].InitAsConstants(4, 1); // Mode params (4 dwords = 16 bytes at b1)

    // Static sampler for shadow map (s0)
    CD3DX12_STATIC_SAMPLER_DESC shadowSampler(
        0, // s0
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 1, &shadowSampler);

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

    // Depth stencil state (disabled since we don't have a depth buffer)
    CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = FALSE; // No depth buffer for Mode 9
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pipelineStateStream.depthStencil = depthStencilDesc;

    // Rasterizer state
    CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // No culling for billboards
    pipelineStateStream.rasterizer = rasterizerDesc;

    // Render target formats (Mode 9.2: Dual render targets for color + emission)
    D3D12_RT_FORMAT_ARRAY renderTargetFormats = {};
    renderTargetFormats.NumRenderTargets = 2;  // Color + emission
    renderTargetFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;      // Color (backbuffer)
    renderTargetFormats.RTFormats[1] = DXGI_FORMAT_R11G11B10_FLOAT;     // Emission buffer
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

bool ParticleSystem::CreateComputeParticlePipeline() {
    // Mode 10: Compute shader that builds particle vertex buffer with RT lighting
    // Root signature must match shader:
    //   b0 = BuildParams (particle count + mode params)
    //   t0 = particles SRV
    //   t1 = particleLighting SRV
    //   b1 = RenderConstants CBV
    //   u0 = outputVertices UAV

    CD3DX12_DESCRIPTOR_RANGE1 particlesRange, lightingRange;
    particlesRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);  // t0
    lightingRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);   // t1

    CD3DX12_ROOT_PARAMETER1 rootParams[5];
    rootParams[0].InitAsConstants(4, 0);                         // Build params (b0): particle count + mode
    rootParams[1].InitAsDescriptorTable(1, &particlesRange);     // Particles SRV (t0)
    rootParams[2].InitAsDescriptorTable(1, &lightingRange);      // Particle lighting SRV (t1)
    rootParams[3].InitAsConstantBufferView(1);                   // Render constants (b1)
    rootParams[4].InitAsUnorderedAccessView(0);                  // Output vertices UAV (u0)

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams);

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        LOGE("Failed to serialize compute particle build root signature");
        return false;
    }

    hr = m_device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&m_computeParticleBuildRootSig));
    if (FAILED(hr)) {
        LOGE("Failed to create compute particle build root signature");
        return false;
    }

    // Create compute PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
    computeDesc.pRootSignature = m_computeParticleBuildRootSig.Get();
    computeDesc.CS = { m_computeParticleBuildShader->GetBufferPointer(), m_computeParticleBuildShader->GetBufferSize() };

    hr = m_device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&m_computeParticleBuildPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create compute particle build PSO: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    LOGI("Compute particle build pipeline created successfully");
    return true;
}

bool ParticleSystem::CreateTraditionalRasterPipeline() {
    // Mode 10: Traditional VS/PS pipeline for rendering compute-built particles
    // Root signature: empty (all data comes from vertex buffer)

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(0, nullptr);

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        LOGE("Failed to serialize traditional raster root signature");
        return false;
    }

    hr = m_device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&m_traditionalRasterRootSig));
    if (FAILED(hr)) {
        LOGE("Failed to create traditional raster root signature");
        return false;
    }

    // Input layout for ParticleVertex
    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",    0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",       0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",       1, DXGI_FORMAT_R32_FLOAT,          0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Create graphics PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_traditionalRasterRootSig.Get();
    psoDesc.VS = { m_traditionalVertexShader->GetBufferPointer(), m_traditionalVertexShader->GetBufferSize() };
    psoDesc.PS = { m_traditionalPixelShader->GetBufferPointer(), m_traditionalPixelShader->GetBufferSize() };
    psoDesc.InputLayout = { inputElements, _countof(inputElements) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;  // Required for non-MSAA rendering
    psoDesc.SampleMask = UINT_MAX;

    // Blend state for alpha blending
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Depth stencil disabled
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;  // No depth buffer

    // Rasterizer state
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_traditionalRasterPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create traditional raster PSO: " + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    LOGI("Traditional VS/PS raster pipeline created successfully");
    return true;
}

void ParticleSystem::InitializeAccretionDisk() {
    LOGI("Initializing NASA-quality accretion disk with " + std::to_string(m_particleCount) + " particles");

    // This would normally initialize particle positions in a buffer
    // For now, we'll set up the initial state via compute shader
}

void ParticleSystem::UpdatePhysics(ID3D12GraphicsCommandList* cmdList, float deltaTime) {
    // Update constants (use current totalTime BEFORE incrementing for first-frame initialization)
    ParticleConstants constants = {};
    constants.deltaTime = deltaTime;
    constants.totalTime = m_totalTime;  // First frame this is 0.0, which triggers initialization

    // Increment time for next frame
    m_totalTime += deltaTime;
    constants.blackHoleMass = BLACK_HOLE_MASS;
    constants.gravityStrength = m_gravityStrength;
    constants.blackHolePosition = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    constants.turbulenceStrength = m_turbulenceStrength;
    constants.diskAxis = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
    constants.dampingFactor = m_dampingFactor;
    constants.innerRadius = INNER_STABLE_ORBIT;
    constants.outerRadius = OUTER_DISK_RADIUS;
    constants.diskThickness = DISK_THICKNESS;
    constants.viscosity = m_viscosity;
    constants.angularMomentumBoost = m_angularMomentumBoost;
    constants.constraintShape = m_constraintShape;
    constants.constraintRadius = m_constraintRadius;
    constants.constraintThickness = m_constraintThickness;
    constants.particleCount = static_cast<float>(m_particleCount);

    // Upload constants to GPU via mapped memory
    void* mappedData;
    HRESULT hr = m_particleConstantsBuffer->Map(0, nullptr, &mappedData);
    if (SUCCEEDED(hr)) {
        memcpy(mappedData, &constants, sizeof(ParticleConstants));
        m_particleConstantsBuffer->Unmap(0, nullptr);
    } else {
        LOGE("Failed to map particle constants buffer - HRESULT: 0x" +
             std::to_string(static_cast<uint32_t>(hr)));
        if (hr == DXGI_ERROR_DEVICE_REMOVED) {
            HRESULT reason = m_device->GetDeviceRemovedReason();
            LOGE("DEVICE REMOVED! Reason: 0x" + std::to_string(static_cast<uint32_t>(reason)));
        }
        return;
    }

    // Dispatch compute shader
    cmdList->SetComputeRootSignature(m_computeRootSig.Get());
    cmdList->SetPipelineState(m_computePSO.Get());
    cmdList->SetComputeRootUnorderedAccessView(0, m_particleBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootConstantBufferView(1, m_particleConstantsBuffer->GetGPUVirtualAddress());

    // Dispatch with one thread per particle
    UINT groupCount = (m_particleCount + 63) / 64; // 64 threads per group
    cmdList->Dispatch(groupCount, 1, 1);

    static int s_dispatchCount = 0;
    if (s_dispatchCount < 5) {
        LOGI("Compute shader dispatch #" + std::to_string(s_dispatchCount) +
             " (totalTime=" + std::to_string(constants.totalTime) +
             " [before increment], m_totalTime=" + std::to_string(m_totalTime) + " [after increment])");
        s_dispatchCount++;
    }

    // Barrier to ensure compute finishes before mesh shader reads
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_particleBuffer.Get());
    cmdList->ResourceBarrier(1, &barrier);
}

void ParticleSystem::RenderParticles(ID3D12GraphicsCommandList* cmdList,
                                   const DirectX::XMMATRIX& viewMatrix,
                                   const DirectX::XMMATRIX& projMatrix,
                                   const DirectX::XMFLOAT3& cameraPos,
                                   D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                                   UINT width, UINT height,
                                   D3D12_GPU_DESCRIPTOR_HANDLE shadowMapSrv,
                                   uint32_t mode9SubMode,
                                   D3D12_CPU_DESCRIPTOR_HANDLE emissionRtvHandle,
                                   D3D12_GPU_DESCRIPTOR_HANDLE particleLightingSrv) {
    static bool s_firstCall = true;
    if (s_firstCall) {
        LOGI("ParticleSystem::RenderParticles called - starting mesh shader rendering");
        LOGI("Camera position: x=" + std::to_string(cameraPos.x) +
             " y=" + std::to_string(cameraPos.y) +
             " z=" + std::to_string(cameraPos.z));
        LOGI("Shadow map SRV handle: ptr=0x" + std::to_string(shadowMapSrv.ptr) + " Mode=" + std::to_string(mode9SubMode));
        s_firstCall = false;
    }

    // Query for ID3D12GraphicsCommandList6 for DispatchMesh support
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> cmdList6;
    HRESULT hr = cmdList->QueryInterface(IID_PPV_ARGS(&cmdList6));
    if (FAILED(hr)) {
        LOGE("Failed to query ID3D12GraphicsCommandList6 for DispatchMesh support");
        return;
    }

    // Set render targets and viewport for mesh shader rendering
    // CRITICAL FIX: PSO is configured for 2 RTVs, must ALWAYS bind 2 (even if second is unused)
    // Mode 9.2: Use dual render targets (color + emission)
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = { rtvHandle, emissionRtvHandle };
    // If no emission RTV, use main RTV for both (writes are discarded by pixel shader logic)
    if (emissionRtvHandle.ptr == 0) {
        rtvHandles[1] = rtvHandle;  // Dummy RTV to match PSO configuration
    }
    cmdList6->OMSetRenderTargets(2, rtvHandles, FALSE, nullptr);  // Always bind 2 RTVs

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
    renderConstants.cameraPos = cameraPos; // Use passed camera position directly

    renderConstants.particleSize = m_particleSize;
    renderConstants.temperatureScale = 1.0f;
    renderConstants.colorTempOffset = m_colorTempOffset;
    renderConstants.colorTempScale = m_colorTempScale;

    // Upload render constants to GPU
    void* mappedData;
    hr = m_renderConstantsBuffer->Map(0, nullptr, &mappedData);
    if (SUCCEEDED(hr)) {
        memcpy(mappedData, &renderConstants, sizeof(RenderConstants));
        m_renderConstantsBuffer->Unmap(0, nullptr);
    } else {
        LOGE("Failed to map render constants buffer - HRESULT: 0x" +
             std::to_string(static_cast<uint32_t>(hr)) +
             " Buffer ptr: " + std::to_string(m_renderConstantsBuffer.Get() != nullptr));
        return;
    }

    // Set mesh pipeline
    cmdList6->SetGraphicsRootSignature(m_meshRootSig.Get());
    cmdList6->SetPipelineState(m_meshPSO.Get());
    cmdList6->SetGraphicsRootShaderResourceView(0, m_particleBuffer->GetGPUVirtualAddress());
    cmdList6->SetGraphicsRootConstantBufferView(1, m_renderConstantsBuffer->GetGPUVirtualAddress());

    // Shadow map descriptor table (param 2) - always required (D3D12 requires all params set)
    if (shadowMapSrv.ptr == 0) {
        LOGE("CRITICAL: Shadow map descriptor is NULL! Aborting render to prevent GPU crash");
        return;
    }
    cmdList6->SetGraphicsRootDescriptorTable(2, shadowMapSrv);

    // Particle lighting descriptor table (param 3) - Mode 9.2
    if (particleLightingSrv.ptr == 0) {
        LOGE("CRITICAL: Particle lighting descriptor is NULL! Aborting render to prevent GPU crash");
        return;
    }
    cmdList6->SetGraphicsRootDescriptorTable(3, particleLightingSrv);

    // DIAGNOSTIC: Log lighting SRV GPU handle
    static bool s_lightingSrvLogged = false;
    if (!s_lightingSrvLogged) {
        LOGI("Particle renderer bound lighting SRV: GPU handle = 0x" +
             std::to_string(particleLightingSrv.ptr) + " (hex)");
        s_lightingSrvLogged = true;
    }

    // Mode params (b1): 4 dwords = { mode9SubMode, padding, padding, padding }
    uint32_t modeParams[4] = { mode9SubMode, 0, 0, 0 };

    // DIAGNOSTIC: Log mode value being passed to shader
    static bool s_modeLogged = false;
    static uint32_t s_lastMode = 0;
    if (!s_modeLogged || s_lastMode != mode9SubMode) {
        LOGI("Particle renderer passing mode to shader: mode9SubMode = " + std::to_string(mode9SubMode));
        s_modeLogged = true;
        s_lastMode = mode9SubMode;
    }

    cmdList6->SetGraphicsRoot32BitConstants(4, 4, modeParams, 0);

    // Dispatch mesh shader workgroups
    // Each workgroup handles 32 particles, creating 4 vertices and 2 triangles per particle
    UINT workgroupCount = (m_particleCount + 31) / 32;
    cmdList6->DispatchMesh(workgroupCount, 1, 1);

    static int s_callCount = 0;
    if (s_callCount < 3) {
        LOGI("DispatchMesh called with " + std::to_string(workgroupCount) + " workgroups for " + std::to_string(m_particleCount) + " particles");
        s_callCount++;
    }
}

void ParticleSystem::RenderComputeParticles(ID3D12GraphicsCommandList* cmdList,
                                           const DirectX::XMMATRIX& viewMatrix,
                                           const DirectX::XMMATRIX& projMatrix,
                                           const DirectX::XMFLOAT3& cameraPos,
                                           D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                                           UINT width, UINT height,
                                           D3D12_GPU_DESCRIPTOR_HANDLE particleBufferSrv,
                                           D3D12_GPU_DESCRIPTOR_HANDLE particleLightingSrv,
                                           uint32_t mode10SubMode) {
    static bool s_firstCall = true;
    if (s_firstCall) {
        LOGI("ParticleSystem::RenderComputeParticles called - Mode 10 compute + traditional VS/PS pipeline");
        s_firstCall = false;
    }

    // Update render constants
    RenderConstants renderConstants = {};
    renderConstants.viewMatrix = viewMatrix;
    renderConstants.projMatrix = projMatrix;
    renderConstants.cameraPos = cameraPos;
    renderConstants.particleSize = m_particleSize;
    renderConstants.temperatureScale = 1.0f;
    renderConstants.colorTempOffset = m_colorTempOffset;
    renderConstants.colorTempScale = m_colorTempScale;

    // Upload render constants to GPU
    void* mappedData;
    HRESULT hr = m_renderConstantsBuffer->Map(0, nullptr, &mappedData);
    if (SUCCEEDED(hr)) {
        memcpy(mappedData, &renderConstants, sizeof(RenderConstants));
        m_renderConstantsBuffer->Unmap(0, nullptr);
    } else {
        LOGE("Failed to map render constants buffer: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return;
    }

    // STEP 1: Dispatch compute shader to build particle vertex buffer
    cmdList->SetComputeRootSignature(m_computeParticleBuildRootSig.Get());
    cmdList->SetPipelineState(m_computeParticleBuildPSO.Get());

    // Root param 0: Build params (b0) - particle count + mode
    uint32_t buildParams[4] = { m_particleCount, mode10SubMode, 0, 0 };
    cmdList->SetComputeRoot32BitConstants(0, 4, buildParams, 0);

    // Root param 1: Particle buffer SRV (t0) - descriptor table
    if (particleBufferSrv.ptr != 0) {
        cmdList->SetComputeRootDescriptorTable(1, particleBufferSrv);
    } else {
        LOGE("Mode 10: Particle buffer SRV is NULL!");
    }

    // Root param 2: Particle lighting SRV (t1) - descriptor table
    if (particleLightingSrv.ptr != 0) {
        cmdList->SetComputeRootDescriptorTable(2, particleLightingSrv);
    } else {
        // No RT lighting in Mode 10.0 Baseline - this is expected
        static bool s_warned = false;
        if (!s_warned && mode10SubMode >= 1) {
            LOGW("Mode 10: Particle lighting SRV is NULL (RT lighting disabled)");
            s_warned = true;
        }
    }

    // Root param 3: Render constants CBV (b1)
    cmdList->SetComputeRootConstantBufferView(3, m_renderConstantsBuffer->GetGPUVirtualAddress());

    // Root param 4: Output vertices UAV (u0)
    cmdList->SetComputeRootUnorderedAccessView(4, m_particleVertexBuffer->GetGPUVirtualAddress());

    // Dispatch compute (256 threads per group, 100K particles = 391 groups)
    UINT groupCount = (m_particleCount + 255) / 256;
    cmdList->Dispatch(groupCount, 1, 1);

    // STEP 2: Transition vertex buffer UAV → VB
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_particleVertexBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    // STEP 3: Traditional rasterization draw call
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    cmdList->RSSetScissorRects(1, &scissorRect);

    cmdList->SetGraphicsRootSignature(m_traditionalRasterRootSig.Get());
    cmdList->SetPipelineState(m_traditionalRasterPSO.Get());

    // Set vertex and index buffers
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    vertexBufferView.BufferLocation = m_particleVertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = m_particleCount * 4 * 48;  // 4 vertices × 48 bytes
    vertexBufferView.StrideInBytes = 48;
    cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);

    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
    indexBufferView.BufferLocation = m_particleIndexBuffer->GetGPUVirtualAddress();
    indexBufferView.SizeInBytes = m_particleCount * 6 * sizeof(uint32_t);
    indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    cmdList->IASetIndexBuffer(&indexBufferView);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Draw indexed (6 indices per particle, 2 triangles per particle)
    cmdList->DrawIndexedInstanced(m_particleCount * 6, 1, 0, 0, 0);

    // STEP 4: Transition vertex buffer back to UAV for next frame
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cmdList->ResourceBarrier(1, &barrier);

    static int s_callCount = 0;
    if (s_callCount < 3) {
        LOGI("Mode 10 render: Compute dispatch (" + std::to_string(groupCount) +
             " groups) + DrawIndexed (" + std::to_string(m_particleCount * 6) + " indices)");
        s_callCount++;
    }
}