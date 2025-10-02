#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <memory>
#include <algorithm>

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
        float turbulenceStrength;
        DirectX::XMFLOAT3 diskAxis;
        float dampingFactor;
        float innerRadius;
        float outerRadius;
        float diskThickness;
        float viscosity;
        float angularMomentumBoost;
        uint32_t constraintShape;  // 0=NONE, 1=SPHERE, 2=DISC, 3=TORUS, 4=ACCRETION_DISK
        float constraintRadius;
        float constraintThickness;
        float particleCount;
    };

    // Render constants for mesh shader
    struct RenderConstants {
        DirectX::XMMATRIX viewMatrix;
        DirectX::XMMATRIX projMatrix;
        DirectX::XMFLOAT3 cameraPos;
        float particleSize;
        float temperatureScale;
        float colorTempOffset;  // Runtime color adjustment
        float colorTempScale;   // Runtime color scaling
        float padding;
    };

    ParticleSystem();
    ~ParticleSystem();

    bool Initialize(ID3D12Device* device, uint32_t particleCount);
    void Shutdown();

    void UpdatePhysics(ID3D12GraphicsCommandList* cmdList, float deltaTime);
    void RenderParticles(ID3D12GraphicsCommandList* cmdList,
                        const DirectX::XMMATRIX& viewMatrix,
                        const DirectX::XMMATRIX& projMatrix,
                        const DirectX::XMFLOAT3& cameraPos,
                        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                        UINT width, UINT height,
                        D3D12_GPU_DESCRIPTOR_HANDLE shadowMapSrv = {},
                        uint32_t mode9SubMode = 0,
                        D3D12_CPU_DESCRIPTOR_HANDLE emissionRtvHandle = {});

    // Runtime adjustable parameters
    void AdjustGravity(float delta) { m_gravityStrength += delta; }
    void AdjustTurbulence(float delta) { m_turbulenceStrength += delta; }
    void AdjustDamping(float delta) { m_dampingFactor = std::clamp(m_dampingFactor + delta, 0.9f, 1.0f); }
    void AdjustAngularMomentum(float delta) { m_angularMomentumBoost += delta; }
    void AdjustViscosity(float delta) { m_viscosity += delta; }
    void AdjustParticleSize(float delta) { m_particleSize = std::max(0.5f, m_particleSize + delta); }
    void AdjustColorTempOffset(float delta) { m_colorTempOffset += delta; }
    void AdjustColorTempScale(float delta) { m_colorTempScale = std::max(0.1f, m_colorTempScale + delta); }
    void ResetParticles() { m_totalTime = 0.0f; }
    void CycleConstraintShape() { m_constraintShape = (m_constraintShape + 1) % 5; }  // Cycle through 0-4

    // Getters for DXR BLAS construction
    ID3D12Resource* GetParticleBuffer() const { return m_particleBuffer.Get(); }
    uint32_t GetParticleCount() const { return m_particleCount; }
    float GetParticleSize() const { return m_particleSize; }

    // Mode 9.2: Particle lighting support
    bool CreateParticleBufferSRV(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);
    void SetParticleBufferSRVIndex(UINT index) { m_particleBufferSrvIndex = index; }
    UINT GetParticleBufferSRVIndex() const { return m_particleBufferSrvIndex; }

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

    // Runtime adjustable physics parameters
    float m_gravityStrength = 500.0f;
    float m_turbulenceStrength = 15.0f;
    float m_dampingFactor = 0.99f;
    float m_angularMomentumBoost = 1.0f;
    float m_viscosity = 0.01f;
    uint32_t m_constraintShape = 0;  // 0=NONE, 1=SPHERE, 2=DISC, 3=TORUS, 4=ACCRETION_DISK
    float m_constraintRadius = 50.0f;
    float m_constraintThickness = 5.0f;

    // Render parameters
    float m_particleSize = 5.0f;
    float m_colorTempOffset = 0.0f;

    // Mode 9.2: Particle buffer SRV for lighting compute
    UINT m_particleBufferSrvIndex = UINT_MAX;
    float m_colorTempScale = 1.0f;

    // NASA-quality accretion disk parameters
    static constexpr float BLACK_HOLE_MASS = 4.15e6f; // Sagittarius A* mass in solar masses
    static constexpr float INNER_STABLE_ORBIT = 6.0f; // Schwarzschild radii
    static constexpr float OUTER_DISK_RADIUS = 60.0f; // Smaller radius to concentrate particles in hot zone
    static constexpr float DISK_THICKNESS = 40.0f; // Thinner for more concentration
};