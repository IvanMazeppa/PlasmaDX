#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class DescriptorHeap;

// Procedural plasma parameters that get uploaded to GPU
struct ProceduralPlasmaConstants {
    XMFLOAT3 containerCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);
    float containerRadius = 2.0f;

    XMFLOAT3 primaryFlowDirection = XMFLOAT3(1.0f, 0.0f, 0.0f);
    float flowSpeed = 1.0f;

    XMFLOAT3 secondaryFlowDirection = XMFLOAT3(0.0f, 1.0f, 0.0f);
    float turbulenceStrength = 0.5f;

    float plasmaIntensity = 2.0f;
    float temperatureVariation = 1.0f;
    float noiseScale = 3.0f;
    float timeScale = 1.0f;

    XMFLOAT3 hotColor = XMFLOAT3(1.0f, 0.6f, 0.2f);    // Orange-yellow for hot plasma
    float padding1;
    XMFLOAT3 coldColor = XMFLOAT3(0.8f, 0.2f, 0.1f);   // Deep red for cool plasma
    float padding2;

    float currentTime = 0.0f;
    float deltaTime = 0.016f;
    float padding3[2];
};

class ProceduralPlasma {
public:
    ProceduralPlasma();
    ~ProceduralPlasma();

    bool Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap);
    void Shutdown();

    void UpdatePhysics(float deltaTime);
    void UploadToGPU(ComPtr<ID3D12GraphicsCommandList4> cmdList);

    // Parameter controls for real-time adjustment
    void SetFlowSpeed(float speed) { m_constants.flowSpeed = speed; }
    void SetTurbulence(float strength) { m_constants.turbulenceStrength = strength; }
    void SetPlasmaIntensity(float intensity) { m_constants.plasmaIntensity = intensity; }
    void SetTemperatureVariation(float variation) { m_constants.temperatureVariation = variation; }
    void SetNoiseScale(float scale) { m_constants.noiseScale = scale; }

    // Getters for DXR binding
    D3D12_GPU_DESCRIPTOR_HANDLE GetConstantsSRV() const { return m_constantsSrvGpu; }
    const ProceduralPlasmaConstants& GetConstants() const { return m_constants; }

private:
    ProceduralPlasmaConstants m_constants;

    // GPU resources
    ComPtr<ID3D12Resource> m_constantBuffer;
    void* m_constantMapped = nullptr;

    // Descriptor heap management
    DescriptorHeap* m_descriptorHeap = nullptr;
    uint32_t m_constantsSrvIndex = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE m_constantsSrvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_constantsSrvGpu = {};

    // Physics state
    float m_time = 0.0f;
    XMFLOAT3 m_flowOffset = XMFLOAT3(0.0f, 0.0f, 0.0f);
    XMFLOAT3 m_turbulenceOffset = XMFLOAT3(0.0f, 0.0f, 0.0f);
};