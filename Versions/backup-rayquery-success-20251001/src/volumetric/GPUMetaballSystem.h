#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <memory>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class DescriptorHeap;

// GPU-Accelerated Metaball System
// Designed to handle 100+ metaballs at 60fps using spatial partitioning
class GPUMetaballSystem {
public:
    static constexpr uint32_t MAX_METABALLS = 512;
    static constexpr uint32_t GRID_SIZE = 16;
    static constexpr uint32_t SPATIAL_THREADS = 64;
    static constexpr uint32_t PHYSICS_THREADS = 64;
    static constexpr uint32_t DENSITY_THREADS = 8;

    // GPU metaball data structures
    struct MetaballGPUData {
        XMFLOAT4 positionRadius;    // xyz = position, w = radius
        XMFLOAT4 velocityMass;      // xyz = velocity, w = mass
        XMFLOAT4 properties;        // x = temperature, y = age, z = intensity, w = target_radius
    };

    struct SpatialHashConstants {
        uint32_t numMetaballs;
        float cellSize;
        XMFLOAT3 gridMin;
        XMFLOAT3 gridMax;
        uint32_t frameCounter;
        float padding[2];
    };

    struct PhysicsConstants {
        float deltaTime;
        float viscosity;
        float buoyancyStrength;
        float noiseStrength;
        XMFLOAT3 gravity;
        XMFLOAT3 containerCenter;
        float containerRadius;
        float collisionRadius;
        float time;
        float cellSize;
        XMFLOAT3 gridMin;
        float padding;
    };

    struct DensityConstants {
        uint32_t numMetaballs;
        uint32_t volumeSize;
        float voxelSize;
        float padding1;
        XMFLOAT3 volumeMin;
        float metaballStrength;
        XMFLOAT3 volumeMax;
        float temperatureInfluence;
        float blendThreshold;
        float time;
        float cellSize;
        float padding2;
        XMFLOAT3 gridMin;
        float padding3;
    };

public:
    GPUMetaballSystem();
    ~GPUMetaballSystem();

    // Initialization and cleanup
    bool Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap);
    void Shutdown();

    // Main update methods
    void UpdatePhysics(ComPtr<ID3D12GraphicsCommandList4> cmdList, float deltaTime);
    void FillDensityVolume(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                          ComPtr<ID3D12Resource> densityTexture, uint32_t volumeSize,
                          D3D12_GPU_DESCRIPTOR_HANDLE densityUAV);

    // Configuration
    void SetupAccretionDisk(uint32_t count = 150);
    void SetContainerBounds(const XMFLOAT3& center, float radius);
    void SetPhysicsParameters(float viscosity, float buoyancy, float noiseStrength);

    // Getters for integration
    uint32_t GetMetaballCount() const { return m_activeMetaballs; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetMetaballSRV() const { return m_metaballSrvGpu; }

private:
    // Initialization helpers
    bool CreateBuffers(ComPtr<ID3D12Device5> device);
    bool CreatePipelineStates(ComPtr<ID3D12Device5> device);
    bool LoadShaders();

    // GPU pipeline stages
    void DispatchSpatialHashing(ComPtr<ID3D12GraphicsCommandList4> cmdList);
    void DispatchPhysicsUpdate(ComPtr<ID3D12GraphicsCommandList4> cmdList);
    void DispatchDensityGeneration(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                                  ComPtr<ID3D12Resource> densityTexture, uint32_t volumeSize,
                                  D3D12_GPU_DESCRIPTOR_HANDLE densityUAV);

    // Resource management
    void TransitionResources(ComPtr<ID3D12GraphicsCommandList4> cmdList, bool toCompute);
    void UploadDataToGPU(const std::vector<XMFLOAT4>& positions,
                        const std::vector<XMFLOAT4>& velocities,
                        const std::vector<XMFLOAT4>& properties);

private:
    // GPU resources
    ComPtr<ID3D12Resource> m_metaballBuffer[2];          // Ping-pong buffers for positions
    ComPtr<ID3D12Resource> m_velocityBuffer[2];          // Ping-pong buffers for velocities
    ComPtr<ID3D12Resource> m_propertiesBuffer[2];        // Ping-pong buffers for properties

    // Spatial grid resources
    ComPtr<ID3D12Resource> m_gridCellCounts;             // Count of metaballs per grid cell
    ComPtr<ID3D12Resource> m_gridCellOffsets;            // Offset into metaball list per cell
    ComPtr<ID3D12Resource> m_metaballIndices;            // Packed metaball indices by cell

    // Constant buffers
    ComPtr<ID3D12Resource> m_spatialConstantBuffer;
    ComPtr<ID3D12Resource> m_physicsConstantBuffer;
    ComPtr<ID3D12Resource> m_densityConstantBuffer;

    // Descriptors
    DescriptorHeap* m_descriptorHeap = nullptr;
    uint32_t m_metaballSrvIndex = UINT32_MAX;
    D3D12_CPU_DESCRIPTOR_HANDLE m_metaballSrvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_metaballSrvGpu = {};

    // Compute pipeline states
    ComPtr<ID3D12RootSignature> m_spatialRootSig;
    ComPtr<ID3D12PipelineState> m_spatialPSO;

    ComPtr<ID3D12RootSignature> m_physicsRootSig;
    ComPtr<ID3D12PipelineState> m_physicsPSO;

    ComPtr<ID3D12RootSignature> m_densityRootSig;
    ComPtr<ID3D12PipelineState> m_densityPSO;

    // Shader bytecode
    std::vector<uint8_t> m_spatialShader;
    std::vector<uint8_t> m_physicsShader;
    std::vector<uint8_t> m_densityShader;

    // State
    uint32_t m_activeMetaballs = 0;
    uint32_t m_currentBuffer = 0;                        // For ping-pong buffering
    float m_time = 0.0f;
    uint32_t m_frameCounter = 0;

    // Physics parameters
    SpatialHashConstants m_spatialConstants = {};
    PhysicsConstants m_physicsConstants = {};
    DensityConstants m_densityConstants = {};

    // Mapped constant buffer pointers
    void* m_spatialConstantMapped = nullptr;
    void* m_physicsConstantMapped = nullptr;
    void* m_densityConstantMapped = nullptr;

    // Upload resources (temporary buffers for initial data upload)
    ComPtr<ID3D12Resource> m_uploadBuffer;
    ComPtr<ID3D12Device5> m_device;  // Keep device reference for upload operations
};