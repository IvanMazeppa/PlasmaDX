#pragma once
#include <DirectXMath.h>
#include <wrl.h>
#include <d3d12.h>

class Camera {
public:
    // Camera constant buffer structure (256-byte aligned)
    struct alignas(256) Constants {
        DirectX::XMMATRIX view;          // 64 bytes
        DirectX::XMMATRIX proj;          // 64 bytes
        DirectX::XMMATRIX viewInverse;   // 64 bytes
        DirectX::XMMATRIX projInverse;   // 64 bytes
        // Total: 256 bytes (exactly 4 matrices)
    };

    Camera();
    ~Camera() = default;

    // Initialize camera with aspect ratio
    void Initialize(float aspectRatio);

    // Update matrices based on input
    void Update(float deltaTime);

    // Handle input
    void OnMouseMove(int deltaX, int deltaY);
    void SetKeyState(char key, bool pressed);

    // Update aspect ratio (on window resize)
    void SetAspectRatio(float aspectRatio);

    // Get camera data for GPU
    const Constants& GetConstants() const { return m_constants; }

    // Create/update constant buffer
    bool CreateConstantBuffer(ID3D12Device* device);
    void UpdateConstantBuffer();
    ID3D12Resource* GetConstantBuffer() const { return m_constantBuffer.Get(); }

private:
    void UpdateMatrices();

    // Camera parameters
    DirectX::XMFLOAT3 m_position = { 0.0f, 2.0f, -5.0f };
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_fov = 45.0f;
    float m_aspectRatio = 16.0f / 9.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;

    // Movement
    float m_moveSpeed = 5.0f;
    float m_lookSpeed = 0.002f;
    bool m_keys[256] = {};

    // Matrices and GPU data
    Constants m_constants = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
    void* m_mappedData = nullptr;
};