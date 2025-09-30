#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <memory>

class ParticleSystem {
public:
    // Particle structure matching HLSL layout
    struct Particle {
        float position[3];
        float temperature;
        float velocity[3];
        float density;
    };

    // Constants buffer structure for particle physics
    struct ParticleConstants {
        float deltaTime;
        float totalTime;
        float blackHoleMass;
        float gravityStrength;
        DirectX::XMFLOAT3 blackHolePosition;
        float viscosity;
        DirectX::XMFLOAT3 diskAxis;
        float innerRadius;
        float outerRadius;
        float diskThickness;
        float temperatureScale;
        float particleCount;
    };

    // Render constants for mesh shader
    struct RenderConstants {
        DirectX::XMMATRIX viewMatrix;
        DirectX::XMMATRIX projMatrix;
        DirectX::XMFLOAT3 cameraPos;
        float particleSize;
        float temperatureScale;
        float padding[3];
    };

    ParticleSystem();
    ~ParticleSystem();

    bool Initialize(ID3D12Device* device, uint32_t particleCount);
    void Shutdown();

    void UpdatePhysics(ID3D12GraphicsCommandList* cmdList, float deltaTime);
    void RenderParticles(ID3D12GraphicsCommandList* cmdList,
                        const DirectX::XMMATRIX& viewMatrix,
                        const DirectX::XMMATRIX& projMatrix,
                        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                        UINT width, UINT height);

private:
    bool CreateBuffers();
    bool CreateComputePipeline();
    bool CreateMeshPipeline();
    bool CompileShaders();
    void InitializeAccretionDisk();

    // Device and resources
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12Device2> m_device2; // For mesh shaders

    // Particle data
    Microsoft::WRL::ComPtr<ID3D12Resource> m_particleBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_particleConstantsBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_renderConstantsBuffer;

    // Pipeline states
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_computePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_meshPSO;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_computeRootSig;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_meshRootSig;

    // Shader bytecode
    Microsoft::WRL::ComPtr<ID3DBlob> m_computeShader;
    Microsoft::WRL::ComPtr<ID3DBlob> m_meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> m_pixelShader;

    // Parameters
    uint32_t m_particleCount;
    float m_totalTime;

    // NASA-quality accretion disk parameters
    static constexpr float BLACK_HOLE_MASS = 4.15e6f; // Sagittarius A* mass in solar masses
    static constexpr float GRAVITY_CONSTANT = 500.0f; // Scaled for visible motion (not SI units)
    static constexpr float INNER_STABLE_ORBIT = 6.0f; // Schwarzschild radii
    static constexpr float OUTER_DISK_RADIUS = 200.0f; // MUCH larger for spacing
    static constexpr float DISK_THICKNESS = 80.0f; // HUGE thickness for cloud-like volume
};