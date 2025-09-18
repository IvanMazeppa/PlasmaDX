#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <cstdint>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class Particles {
public:
    struct Particle {
        XMFLOAT3 position;
        float life;        // 0.0 = dead, 1.0 = full life
        XMFLOAT3 velocity;
        float padding;     // Align to 16 bytes for GPU
    };

    struct ParticleConstants {
        float deltaTime;
        float totalTime;
        float worldBounds;  // [-worldBounds, +worldBounds] cube
        uint32_t particleCount;

        XMFLOAT3 gravity;   // Gravity vector (usually down)
        float damping;      // Velocity damping factor
    };

    explicit Particles(ID3D12Device* device);
    ~Particles() = default;

    // Initialize with particle count
    bool Initialize(uint32_t particleCount = 65536);

    // Update particles for one frame
    void Update(ID3D12GraphicsCommandList* cmdList,
                float deltaTime,
                float totalTime);

    // Write debug pattern to HDR texture (for VOL_0001 testing)
    void WriteDebugPattern(ID3D12GraphicsCommandList* cmdList,
                          ID3D12Resource* hdrTexture,
                          D3D12_GPU_DESCRIPTOR_HANDLE hdrUavHandle,
                          uint32_t width, uint32_t height);

    // Resource access
    ID3D12Resource* GetParticleBuffer() const { return m_particleBuffer.Get(); }
    uint32_t GetParticleCount() const { return m_particleCount; }

private:
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12RootSignature> m_updateRootSignature;
    ComPtr<ID3D12RootSignature> m_debugRootSignature;
    ComPtr<ID3D12PipelineState> m_updatePipelineState;
    ComPtr<ID3D12PipelineState> m_debugPipelineState;

    // GPU resources
    ComPtr<ID3D12Resource> m_particleBuffer;
    ComPtr<ID3D12Resource> m_constantBuffer;

    uint32_t m_particleCount;
    ParticleConstants* m_mappedConstants;  // Persistently mapped CB

    bool CreateRootSignatures();
    bool CreatePipelineStates();
    bool CreateResources();
    void InitializeParticleData();
};