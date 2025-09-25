#pragma once

#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// Individual metaball data
struct Metaball {
    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    float radius = 0.5f;
    XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    float temperature = 1.0f;        // 0.0 = cold (sinks), 1.0 = hot (rises)
    XMFLOAT3 color = { 1.0f, 0.5f, 0.2f };  // Plasma color
    float mass = 1.0f;
    float targetRadius = 0.5f;       // For smooth size transitions
    float age = 0.0f;                // For lifecycle effects
};

// GPU constant buffer data (256-byte aligned)
struct alignas(256) MetaballConstants {
    uint32_t numMetaballs = 0;
    float time = 0.0f;
    float deltaTime = 0.016f;
    float containerRadius = 1.0f;    // Sphere container bounds

    XMFLOAT3 gravity = { 0.0f, -2.0f, 0.0f };  // Downward gravity
    float buoyancyStrength = 3.0f;   // Temperature-driven upward force

    XMFLOAT3 containerCenter = { 0.0f, 0.0f, 0.0f };
    float viscosity = 0.8f;          // Fluid resistance

    float mergeDistance = 0.3f;      // When metaballs start to merge
    float splitThreshold = 1.5f;     // When large metaballs split
    float noiseStrength = 0.1f;      // Organic motion noise
    float padding = 0.0f;
};

// GPU metaball data array (tightly packed for shader)
struct MetaballGPUData {
    XMFLOAT3 position;
    float radius;
    XMFLOAT3 velocity;
    float temperature;
    XMFLOAT3 color;
    float mass;
};

class DescriptorHeap;

class MetaballSystem {
public:
    static const uint32_t MAX_METABALLS = 16;

    MetaballSystem();
    ~MetaballSystem();

    bool Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap);
    void Shutdown();

    // Update physics simulation on CPU
    void UpdatePhysics(float deltaTime);

    // Upload data to GPU buffers
    void UploadToGPU(ComPtr<ID3D12GraphicsCommandList4> cmdList);

    // Preset configurations
    void SetupLavaLampPreset();      // Classic lava lamp behavior
    void SetupPlasmaStormPreset();   // More chaotic plasma movement
    void SetupGentleBubblesPreset(); // Slow, peaceful motion

    // Interactive controls
    void AddMetaball(XMFLOAT3 position, float temperature = 0.5f);
    bool IncreaseCount();         // Add a randomly initialized metaball (returns false if at max)
    bool DecreaseCount();         // Remove one metaball if possible (returns false if at min)
    void SetContainerBounds(XMFLOAT3 center, float radius);
    void SetPhysicsParams(XMFLOAT3 gravity, float buoyancy, float viscosity);

    // Getters for GPU binding
    ID3D12Resource* GetConstantBuffer() const { return m_constantBuffer.Get(); }
    ID3D12Resource* GetMetaballBuffer() const { return m_metaballBuffer.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetMetaballSRV() const { return m_metaballSrvGpu; }
    uint32_t GetMetaballCount() const { return static_cast<uint32_t>(m_metaballs.size()); }

private:
    void updateSingleMetaball(Metaball& metaball, float deltaTime);
    Metaball createRandomMetaball() const;  // Utility for interactive add
    void handleCollisions();
    void handleMergingAndSplitting();
    void applyContainerConstraints();
    float evaluateMetaballField(const XMFLOAT3& position, const Metaball& metaball) const;

    // Metaball data
    std::vector<Metaball> m_metaballs;
    MetaballConstants m_constants;

    // GPU resources
    ComPtr<ID3D12Resource> m_constantBuffer;
    ComPtr<ID3D12Resource> m_metaballBuffer;
    void* m_constantMapped = nullptr;
    void* m_metaballMapped = nullptr;

    // Descriptor heap and SRV
    DescriptorHeap* m_descriptorHeap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE m_metaballSrvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_metaballSrvGpu = {};
    uint32_t m_metaballSrvIndex = UINT32_MAX;

    // Physics parameters
    float m_time = 0.0f;
};