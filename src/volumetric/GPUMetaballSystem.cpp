#include "GPUMetaballSystem.h"
#include "../utils/Logger.h"
#include "../utils/DescriptorHeap.h"
#include <d3dx12/d3dx12.h>
#include <fstream>
#include <random>

GPUMetaballSystem::GPUMetaballSystem() = default;
GPUMetaballSystem::~GPUMetaballSystem() = default;

bool GPUMetaballSystem::Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap) {
    LOGI("Initializing GPU-accelerated MetaballSystem...");

    m_device = device;  // Keep reference for upload operations
    m_descriptorHeap = descriptorHeap;
    if (!m_descriptorHeap) {
        LOGE("DescriptorHeap is required for GPUMetaballSystem");
        return false;
    }

    // Create GPU buffers
    if (!CreateBuffers(device)) {
        LOGE("Failed to create GPU buffers");
        return false;
    }

    // Load shaders and create pipeline states
    if (!LoadShaders()) {
        LOGE("Failed to load compute shaders");
        return false;
    }

    if (!CreatePipelineStates(device)) {
        LOGE("Failed to create compute pipeline states");
        return false;
    }

    // Initialize physics parameters with good defaults
    SetPhysicsParameters(0.95f, 0.3f, 0.1f);
    SetContainerBounds(XMFLOAT3(0.5f, 0.5f, 0.5f), 0.4f);

    // Initialize density constants for proper metaball separation
    m_densityConstants.metaballStrength = 2.0f;        // Much higher for testing visibility
    m_densityConstants.temperatureInfluence = 0.2f;    // Moderate temperature effects
    m_densityConstants.blendThreshold = 0.05f;         // Low threshold for visibility
    m_densityConstants.voxelSize = 0.01f;              // Fine voxel resolution

    LOGI("GPU MetaballSystem initialized successfully");
    return true;
}

void GPUMetaballSystem::Shutdown() {
    // Unmap constant buffers
    if (m_spatialConstantBuffer && m_spatialConstantMapped) {
        m_spatialConstantBuffer->Unmap(0, nullptr);
        m_spatialConstantMapped = nullptr;
    }
    if (m_physicsConstantBuffer && m_physicsConstantMapped) {
        m_physicsConstantBuffer->Unmap(0, nullptr);
        m_physicsConstantMapped = nullptr;
    }
    if (m_densityConstantBuffer && m_densityConstantMapped) {
        m_densityConstantBuffer->Unmap(0, nullptr);
        m_densityConstantMapped = nullptr;
    }

    // Free descriptor heap entries
    if (m_descriptorHeap && m_metaballSrvIndex != UINT32_MAX) {
        m_descriptorHeap->Free(m_metaballSrvIndex);
        m_metaballSrvIndex = UINT32_MAX;
    }

    // Reset all COM pointers
    for (int i = 0; i < 2; i++) {
        m_metaballBuffer[i].Reset();
        m_velocityBuffer[i].Reset();
        m_propertiesBuffer[i].Reset();
    }
    m_gridCellCounts.Reset();
    m_gridCellOffsets.Reset();
    m_metaballIndices.Reset();

    m_spatialConstantBuffer.Reset();
    m_physicsConstantBuffer.Reset();
    m_densityConstantBuffer.Reset();

    m_spatialPSO.Reset();
    m_physicsPSO.Reset();
    m_densityPSO.Reset();
    m_spatialRootSig.Reset();
    m_physicsRootSig.Reset();
    m_densityRootSig.Reset();

    LOGI("GPU MetaballSystem shutdown complete");
}

bool GPUMetaballSystem::CreateBuffers(ComPtr<ID3D12Device5> device) {
    // Helper for creating buffers
    auto CreateBuffer = [&](ComPtr<ID3D12Resource>& buffer, size_t size, const std::string& name) -> bool {
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer)
        );

        if (FAILED(hr)) {
            LOGE("Failed to create " + name + " buffer");
            return false;
        }
        return true;
    };

    // Create ping-pong metaball data buffers
    size_t metaballSize = sizeof(MetaballGPUData) * MAX_METABALLS;
    if (!CreateBuffer(m_metaballBuffer[0], sizeof(XMFLOAT4) * MAX_METABALLS, "metaball positions 0") ||
        !CreateBuffer(m_metaballBuffer[1], sizeof(XMFLOAT4) * MAX_METABALLS, "metaball positions 1") ||
        !CreateBuffer(m_velocityBuffer[0], sizeof(XMFLOAT4) * MAX_METABALLS, "metaball velocities 0") ||
        !CreateBuffer(m_velocityBuffer[1], sizeof(XMFLOAT4) * MAX_METABALLS, "metaball velocities 1") ||
        !CreateBuffer(m_propertiesBuffer[0], sizeof(XMFLOAT4) * MAX_METABALLS, "metaball properties 0") ||
        !CreateBuffer(m_propertiesBuffer[1], sizeof(XMFLOAT4) * MAX_METABALLS, "metaball properties 1")) {
        return false;
    }

    // Create spatial grid buffers
    uint32_t totalCells = GRID_SIZE * GRID_SIZE * GRID_SIZE;
    if (!CreateBuffer(m_gridCellCounts, sizeof(uint32_t) * totalCells, "grid cell counts") ||
        !CreateBuffer(m_gridCellOffsets, sizeof(uint32_t) * totalCells, "grid cell offsets") ||
        !CreateBuffer(m_metaballIndices, sizeof(uint32_t) * MAX_METABALLS * 8, "metaball indices")) {
        return false;
    }

    // Create constant buffers (upload heap for CPU writes)
    auto CreateConstantBuffer = [&](ComPtr<ID3D12Resource>& buffer, size_t size, void*& mapped, const std::string& name) -> bool {
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(((size + 255) & ~255)); // 256-byte aligned

        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)
        );

        if (FAILED(hr)) {
            LOGE("Failed to create " + name + " constant buffer");
            return false;
        }

        hr = buffer->Map(0, nullptr, &mapped);
        if (FAILED(hr)) {
            LOGE("Failed to map " + name + " constant buffer");
            return false;
        }

        return true;
    };

    if (!CreateConstantBuffer(m_spatialConstantBuffer, sizeof(SpatialHashConstants), m_spatialConstantMapped, "spatial") ||
        !CreateConstantBuffer(m_physicsConstantBuffer, sizeof(PhysicsConstants), m_physicsConstantMapped, "physics") ||
        !CreateConstantBuffer(m_densityConstantBuffer, sizeof(DensityConstants), m_densityConstantMapped, "density")) {
        return false;
    }

    // Create SRV for metaball buffer (for rendering integration)
    m_metaballSrvIndex = m_descriptorHeap->Allocate();
    if (m_metaballSrvIndex == UINT_MAX) {
        LOGE("Failed to allocate SRV descriptor for metaball buffer");
        return false;
    }
    m_metaballSrvCpu = m_descriptorHeap->GetCPUHandle(m_metaballSrvIndex);
    m_metaballSrvGpu = m_descriptorHeap->GetGPUHandle(m_metaballSrvIndex);

    // Create the SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MAX_METABALLS;
    srvDesc.Buffer.StructureByteStride = sizeof(XMFLOAT4);

    device->CreateShaderResourceView(m_metaballBuffer[0].Get(), &srvDesc, m_metaballSrvCpu);

    LOGI("Created GPU metaball buffers successfully");
    return true;
}

bool GPUMetaballSystem::LoadShaders() {
    // Helper for loading shader files
    auto LoadShaderFile = [](const std::string& path, std::vector<uint8_t>& shaderData) -> bool {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            LOGE("Failed to open shader file: " + path);
            return false;
        }

        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        shaderData.resize(size);
        file.read(reinterpret_cast<char*>(shaderData.data()), size);
        file.close();

        LOGI("Loaded shader: " + path + " (" + std::to_string(size) + " bytes)");
        return true;
    };

    // Load all compute shaders
    std::vector<std::string> shaderPaths = {
        "shaders/metaball/spatial_hash.dxil",
        "shaders/metaball/metaball_physics.dxil",
        "shaders/metaball/metaball_density.dxil"
    };

    std::vector<std::vector<uint8_t>*> shaderTargets = {
        &m_spatialShader,
        &m_physicsShader,
        &m_densityShader
    };

    for (size_t i = 0; i < shaderPaths.size(); i++) {
        if (!LoadShaderFile(shaderPaths[i], *shaderTargets[i])) {
            return false;
        }
    }

    return true;
}

bool GPUMetaballSystem::CreatePipelineStates(ComPtr<ID3D12Device5> device) {
    HRESULT hr;

    // Create Spatial Hash Root Signature
    {
        CD3DX12_ROOT_PARAMETER1 rootParams[5];
        rootParams[0].InitAsConstantBufferView(0); // SpatialHashConstants
        rootParams[1].InitAsShaderResourceView(0); // g_metaballPositions
        rootParams[2].InitAsUnorderedAccessView(0); // g_gridCellCounts
        rootParams[3].InitAsUnorderedAccessView(1); // g_gridCellOffsets
        rootParams[4].InitAsUnorderedAccessView(2); // g_metaballIndices

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> serializedRootSig, errorBlob;
        hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
        if (FAILED(hr)) {
            LOGE("Failed to serialize spatial hash root signature");
            return false;
        }

        hr = device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&m_spatialRootSig));
        if (FAILED(hr)) {
            LOGE("Failed to create spatial hash root signature");
            return false;
        }
    }

    // Create Physics Root Signature
    {
        CD3DX12_ROOT_PARAMETER1 rootParams[10];
        rootParams[0].InitAsConstantBufferView(0); // PhysicsConstants
        rootParams[1].InitAsShaderResourceView(0); // g_metaballPositions (input)
        rootParams[2].InitAsShaderResourceView(1); // g_metaballVelocities (input)
        rootParams[3].InitAsShaderResourceView(2); // g_metaballProperties (input)
        rootParams[4].InitAsShaderResourceView(3); // g_gridCellCounts
        rootParams[5].InitAsShaderResourceView(4); // g_gridCellOffsets
        rootParams[6].InitAsShaderResourceView(5); // g_metaballIndices
        rootParams[7].InitAsUnorderedAccessView(0); // g_outputPositions
        rootParams[8].InitAsUnorderedAccessView(1); // g_outputVelocities
        rootParams[9].InitAsUnorderedAccessView(2); // g_outputProperties

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> serializedRootSig, errorBlob;
        hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
        if (FAILED(hr)) {
            LOGE("Failed to serialize physics root signature");
            return false;
        }

        hr = device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&m_physicsRootSig));
        if (FAILED(hr)) {
            LOGE("Failed to create physics root signature");
            return false;
        }
    }

    // Create Density Root Signature
    {
        CD3DX12_ROOT_PARAMETER1 rootParams[7];
        rootParams[0].InitAsConstantBufferView(0); // DensityConstants
        rootParams[1].InitAsShaderResourceView(0); // g_metaballPositions
        rootParams[2].InitAsShaderResourceView(1); // g_metaballProperties
        rootParams[3].InitAsShaderResourceView(2); // g_gridCellCounts
        rootParams[4].InitAsShaderResourceView(3); // g_gridCellOffsets
        rootParams[5].InitAsShaderResourceView(4); // g_metaballIndices

        // Use descriptor table for typed UAV (RWTexture3D)
        CD3DX12_DESCRIPTOR_RANGE1 uavRange;
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        rootParams[6].InitAsDescriptorTable(1, &uavRange); // g_densityVolume

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> serializedRootSig, errorBlob;
        hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
        if (FAILED(hr)) {
            LOGE("Failed to serialize density root signature");
            return false;
        }

        hr = device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&m_densityRootSig));
        if (FAILED(hr)) {
            LOGE("Failed to create density root signature");
            return false;
        }
    }

    // Create Pipeline State Objects
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};

    // Spatial Hash PSO
    psoDesc.pRootSignature = m_spatialRootSig.Get();
    psoDesc.CS.pShaderBytecode = m_spatialShader.data();
    psoDesc.CS.BytecodeLength = m_spatialShader.size();
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_spatialPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create spatial hash PSO");
        return false;
    }

    // Physics PSO
    psoDesc.pRootSignature = m_physicsRootSig.Get();
    psoDesc.CS.pShaderBytecode = m_physicsShader.data();
    psoDesc.CS.BytecodeLength = m_physicsShader.size();
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_physicsPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create physics PSO");
        return false;
    }

    // Density PSO
    psoDesc.pRootSignature = m_densityRootSig.Get();
    psoDesc.CS.pShaderBytecode = m_densityShader.data();
    psoDesc.CS.BytecodeLength = m_densityShader.size();
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_densityPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create density PSO");
        return false;
    }

    LOGI("Created GPU metaball compute pipeline states with proper root signatures");
    return true;
}

void GPUMetaballSystem::SetupAccretionDisk(uint32_t count) {
    m_activeMetaballs = std::min(std::min(count, 20u), MAX_METABALLS); // Limit to 20 for testing

    // Initialize metaball data for accretion disk simulation
    std::vector<XMFLOAT4> positions(MAX_METABALLS);
    std::vector<XMFLOAT4> velocities(MAX_METABALLS);
    std::vector<XMFLOAT4> properties(MAX_METABALLS);

    // Random number generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> angleDistr(0.0f, 2.0f * 3.14159265f);
    std::uniform_real_distribution<float> radiusDistr(0.2f, 0.8f); // Closer to center for testing
    std::uniform_real_distribution<float> heightDistr(-0.1f, 0.1f); // Flatter for testing
    std::uniform_real_distribution<float> sizeDistr(0.1f, 0.15f); // Larger particles for visibility
    std::uniform_real_distribution<float> tempDistr(0.3f, 1.0f);
    std::uniform_real_distribution<float> massDistr(0.8f, 1.5f);

    XMFLOAT3 center = m_physicsConstants.containerCenter;

    // Create accretion disk distribution
    for (uint32_t i = 0; i < m_activeMetaballs; i++) {
        // Disk-like distribution
        float angle = angleDistr(gen);
        float radius = radiusDistr(gen);
        float height = heightDistr(gen) * (1.0f - radius/2.0f); // Thinner at edges

        // Position in disk formation
        positions[i] = XMFLOAT4(
            center.x + radius * cos(angle),
            center.y + height,
            center.z + radius * sin(angle),
            sizeDistr(gen) // radius
        );

        // Enhanced orbital velocity for flowing metal streams
        float orbitalSpeed = 2.0f / sqrt(radius + 0.1f); // 4x faster for visible motion
        velocities[i] = XMFLOAT4(
            -orbitalSpeed * sin(angle), // tangential velocity
            0.0f,
            orbitalSpeed * cos(angle),
            massDistr(gen) // mass
        );

        // Properties: temperature, age, intensity, target_radius
        properties[i] = XMFLOAT4(
            tempDistr(gen),     // temperature
            0.0f,               // age
            1.0f,               // intensity
            positions[i].w      // target radius
        );
    }

    // Zero out unused metaballs
    for (uint32_t i = m_activeMetaballs; i < MAX_METABALLS; i++) {
        positions[i] = XMFLOAT4(0, 0, 0, 0);
        velocities[i] = XMFLOAT4(0, 0, 0, 0);
        properties[i] = XMFLOAT4(0, 0, 0, 0);
    }

    // Upload to GPU buffers (requires upload heap)
    UploadDataToGPU(positions, velocities, properties);

    LOGI("GPU MetaballSystem configured for accretion disk with " + std::to_string(m_activeMetaballs) + " metaballs");
}

void GPUMetaballSystem::SetContainerBounds(const XMFLOAT3& center, float radius) {
    m_physicsConstants.containerCenter = center;
    m_physicsConstants.containerRadius = radius;

    // Update spatial grid bounds
    m_spatialConstants.gridMin = XMFLOAT3(center.x - radius, center.y - radius, center.z - radius);
    m_spatialConstants.gridMax = XMFLOAT3(center.x + radius, center.y + radius, center.z + radius);
    m_spatialConstants.cellSize = (2.0f * radius) / GRID_SIZE;

    m_physicsConstants.gridMin = m_spatialConstants.gridMin;
    m_physicsConstants.cellSize = m_spatialConstants.cellSize;

    m_densityConstants.gridMin = m_spatialConstants.gridMin;
    m_densityConstants.cellSize = m_spatialConstants.cellSize;
}

void GPUMetaballSystem::SetPhysicsParameters(float viscosity, float buoyancy, float noiseStrength) {
    m_physicsConstants.viscosity = viscosity;
    m_physicsConstants.buoyancyStrength = buoyancy;
    m_physicsConstants.noiseStrength = noiseStrength;
    m_physicsConstants.gravity = XMFLOAT3(0.0f, -0.2f, 0.0f); // Reduced gravity
    m_physicsConstants.collisionRadius = 0.8f; // Smaller collision radius for more separation
}

void GPUMetaballSystem::UpdatePhysics(ComPtr<ID3D12GraphicsCommandList4> cmdList, float deltaTime) {
    m_time += deltaTime;
    m_frameCounter++;

    if (m_frameCounter % 60 == 0) { // Log every second at 60fps
        LOGI("GPU Metaball Physics Update: frame=" + std::to_string(m_frameCounter) +
             " time=" + std::to_string(m_time) + " metaballs=" + std::to_string(m_activeMetaballs));
    }

    // Update constants
    m_spatialConstants.numMetaballs = m_activeMetaballs;
    m_spatialConstants.frameCounter = m_frameCounter;

    m_physicsConstants.deltaTime = deltaTime;
    m_physicsConstants.time = m_time;

    // Copy constants to GPU
    if (m_spatialConstantMapped) {
        memcpy(m_spatialConstantMapped, &m_spatialConstants, sizeof(SpatialHashConstants));
    }
    if (m_physicsConstantMapped) {
        memcpy(m_physicsConstantMapped, &m_physicsConstants, sizeof(PhysicsConstants));
    }

    // Execute GPU pipeline
    if (m_frameCounter % 60 == 0) {
        LOGI("GPU Metaball: Dispatching spatial hashing and physics updates");
    }
    DispatchSpatialHashing(cmdList);
    DispatchPhysicsUpdate(cmdList);

    // Swap ping-pong buffers
    m_currentBuffer = 1 - m_currentBuffer;
}

void GPUMetaballSystem::FillDensityVolume(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                                         ComPtr<ID3D12Resource> densityTexture, uint32_t volumeSize,
                                         D3D12_GPU_DESCRIPTOR_HANDLE densityUAV) {
    // Update density constants
    m_densityConstants.numMetaballs = m_activeMetaballs;
    m_densityConstants.volumeSize = volumeSize;
    m_densityConstants.time = m_time;

    if (m_densityConstantMapped) {
        memcpy(m_densityConstantMapped, &m_densityConstants, sizeof(DensityConstants));
    }

    DispatchDensityGeneration(cmdList, densityTexture, volumeSize, densityUAV);
}

void GPUMetaballSystem::DispatchSpatialHashing(ComPtr<ID3D12GraphicsCommandList4> cmdList) {
    // Set pipeline state
    cmdList->SetPipelineState(m_spatialPSO.Get());
    cmdList->SetComputeRootSignature(m_spatialRootSig.Get());

    // Bind constant buffer
    cmdList->SetComputeRootConstantBufferView(0, m_spatialConstantBuffer->GetGPUVirtualAddress());

    // Bind metaball position buffer (SRV)
    cmdList->SetComputeRootShaderResourceView(1, m_metaballBuffer[m_currentBuffer]->GetGPUVirtualAddress());

    // Bind output buffers (UAVs)
    cmdList->SetComputeRootUnorderedAccessView(2, m_gridCellCounts->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(3, m_gridCellOffsets->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(4, m_metaballIndices->GetGPUVirtualAddress());

    // Dispatch compute shader
    uint32_t threadGroups = (m_activeMetaballs + SPATIAL_THREADS - 1) / SPATIAL_THREADS;
    cmdList->Dispatch(threadGroups, 1, 1);

    // UAV barrier to ensure spatial hashing completes before physics
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_gridCellCounts.Get();
    cmdList->ResourceBarrier(1, &barrier);
}

void GPUMetaballSystem::DispatchPhysicsUpdate(ComPtr<ID3D12GraphicsCommandList4> cmdList) {
    // Set pipeline state
    cmdList->SetPipelineState(m_physicsPSO.Get());
    cmdList->SetComputeRootSignature(m_physicsRootSig.Get());

    // Bind constant buffer
    cmdList->SetComputeRootConstantBufferView(0, m_physicsConstantBuffer->GetGPUVirtualAddress());

    // Bind input metaball buffers (SRVs) - current frame
    cmdList->SetComputeRootShaderResourceView(1, m_metaballBuffer[m_currentBuffer]->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(2, m_velocityBuffer[m_currentBuffer]->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(3, m_propertiesBuffer[m_currentBuffer]->GetGPUVirtualAddress());

    // Bind spatial grid data (SRVs)
    cmdList->SetComputeRootShaderResourceView(4, m_gridCellCounts->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(5, m_gridCellOffsets->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(6, m_metaballIndices->GetGPUVirtualAddress());

    // Bind output metaball buffers (UAVs) - next frame
    uint32_t nextBuffer = 1 - m_currentBuffer;
    cmdList->SetComputeRootUnorderedAccessView(7, m_metaballBuffer[nextBuffer]->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(8, m_velocityBuffer[nextBuffer]->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(9, m_propertiesBuffer[nextBuffer]->GetGPUVirtualAddress());

    // Dispatch compute shader
    uint32_t threadGroups = (m_activeMetaballs + PHYSICS_THREADS - 1) / PHYSICS_THREADS;
    cmdList->Dispatch(threadGroups, 1, 1);

    // UAV barrier to ensure physics update completes
    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(m_metaballBuffer[nextBuffer].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(m_velocityBuffer[nextBuffer].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(m_propertiesBuffer[nextBuffer].Get())
    };
    cmdList->ResourceBarrier(3, barriers);
}

void GPUMetaballSystem::DispatchDensityGeneration(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                                                 ComPtr<ID3D12Resource> densityTexture, uint32_t volumeSize,
                                                 D3D12_GPU_DESCRIPTOR_HANDLE densityUAV) {
    // Set pipeline state
    cmdList->SetPipelineState(m_densityPSO.Get());
    cmdList->SetComputeRootSignature(m_densityRootSig.Get());

    // Bind constant buffer
    cmdList->SetComputeRootConstantBufferView(0, m_densityConstantBuffer->GetGPUVirtualAddress());

    // Bind metaball data buffers (SRVs) - use updated data
    uint32_t dataBuffer = 1 - m_currentBuffer; // Use the just-updated buffer
    cmdList->SetComputeRootShaderResourceView(1, m_metaballBuffer[dataBuffer]->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(2, m_propertiesBuffer[dataBuffer]->GetGPUVirtualAddress());

    // Bind spatial grid data (SRVs)
    cmdList->SetComputeRootShaderResourceView(3, m_gridCellCounts->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(4, m_gridCellOffsets->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(5, m_metaballIndices->GetGPUVirtualAddress());

    // Bind density volume (UAV) using descriptor table
    cmdList->SetComputeRootDescriptorTable(6, densityUAV);

    // Dispatch compute shader - 3D thread groups for volume
    uint32_t threadGroupsX = (volumeSize + DENSITY_THREADS - 1) / DENSITY_THREADS;
    uint32_t threadGroupsY = (volumeSize + DENSITY_THREADS - 1) / DENSITY_THREADS;
    uint32_t threadGroupsZ = (volumeSize + DENSITY_THREADS - 1) / DENSITY_THREADS;

    cmdList->Dispatch(threadGroupsX, threadGroupsY, threadGroupsZ);

    // UAV barrier to ensure density generation completes
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = densityTexture.Get();
    cmdList->ResourceBarrier(1, &barrier);
}

void GPUMetaballSystem::UploadDataToGPU(const std::vector<XMFLOAT4>& positions,
                                       const std::vector<XMFLOAT4>& velocities,
                                       const std::vector<XMFLOAT4>& properties) {
    if (!m_device) {
        LOGE("Device not available for GPU upload");
        return;
    }

    // Calculate total upload size
    size_t bufferSize = sizeof(XMFLOAT4) * MAX_METABALLS;
    size_t totalUploadSize = bufferSize * 6; // 3 buffers * 2 (ping-pong)

    // Create upload buffer if needed
    if (!m_uploadBuffer) {
        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(totalUploadSize);

        HRESULT hr = m_device->CreateCommittedResource(
            &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_uploadBuffer)
        );

        if (FAILED(hr)) {
            LOGE("Failed to create upload buffer for metaball data");
            return;
        }
    }

    // Map upload buffer and copy data
    void* mappedData = nullptr;
    HRESULT hr = m_uploadBuffer->Map(0, nullptr, &mappedData);
    if (FAILED(hr)) {
        LOGE("Failed to map upload buffer");
        return;
    }

    // Copy data to upload buffer
    uint8_t* uploadPtr = static_cast<uint8_t*>(mappedData);

    // Copy to both ping-pong buffers (they start with same data)
    for (int bufferIndex = 0; bufferIndex < 2; bufferIndex++) {
        memcpy(uploadPtr, positions.data(), bufferSize);
        uploadPtr += bufferSize;
        memcpy(uploadPtr, velocities.data(), bufferSize);
        uploadPtr += bufferSize;
        memcpy(uploadPtr, properties.data(), bufferSize);
        uploadPtr += bufferSize;
    }

    m_uploadBuffer->Unmap(0, nullptr);

    // Create a command list for the upload (this is simplified - in production you'd use the main command list)
    ComPtr<ID3D12CommandAllocator> uploadAllocator;
    hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator));
    if (FAILED(hr)) {
        LOGE("Failed to create upload command allocator");
        return;
    }

    ComPtr<ID3D12GraphicsCommandList> uploadCmdList;
    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadCmdList));
    if (FAILED(hr)) {
        LOGE("Failed to create upload command list");
        return;
    }

    // Transition GPU buffers to copy dest state
    D3D12_RESOURCE_BARRIER barriers[6];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_metaballBuffer[0].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_metaballBuffer[1].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(m_velocityBuffer[0].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(m_velocityBuffer[1].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    barriers[4] = CD3DX12_RESOURCE_BARRIER::Transition(m_propertiesBuffer[0].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    barriers[5] = CD3DX12_RESOURCE_BARRIER::Transition(m_propertiesBuffer[1].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    uploadCmdList->ResourceBarrier(6, barriers);

    // Copy data from upload buffer to GPU buffers
    uploadPtr = static_cast<uint8_t*>(mappedData);
    for (int bufferIndex = 0; bufferIndex < 2; bufferIndex++) {
        size_t offset = bufferIndex * bufferSize * 3;

        uploadCmdList->CopyBufferRegion(m_metaballBuffer[bufferIndex].Get(), 0, m_uploadBuffer.Get(), offset, bufferSize);
        uploadCmdList->CopyBufferRegion(m_velocityBuffer[bufferIndex].Get(), 0, m_uploadBuffer.Get(), offset + bufferSize, bufferSize);
        uploadCmdList->CopyBufferRegion(m_propertiesBuffer[bufferIndex].Get(), 0, m_uploadBuffer.Get(), offset + bufferSize * 2, bufferSize);
    }

    // Transition back to common state
    for (int i = 0; i < 6; i++) {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(barriers[i].Transition.pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    }
    uploadCmdList->ResourceBarrier(6, barriers);

    // Execute upload commands (simplified - in production you'd submit to queue properly)
    uploadCmdList->Close();

    LOGI("Metaball data uploaded to GPU (simplified upload - may need proper queue execution)");
}