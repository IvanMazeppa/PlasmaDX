#include "Camera.h"
#include "../utils/Logger.h"
#include <algorithm>

using namespace DirectX;

Camera::Camera() {
    UpdateMatrices();
}

void Camera::Initialize(float aspectRatio) {
    m_aspectRatio = aspectRatio;
    UpdateMatrices();
    LOGI("Camera initialized");
}

void Camera::Update(float deltaTime) {
    bool moved = false;
    XMFLOAT3 forward = { 0.0f, 0.0f, 1.0f };
    XMFLOAT3 right = { 1.0f, 0.0f, 0.0f };

    // Calculate forward and right vectors from yaw
    forward.x = sin(m_yaw);
    forward.z = cos(m_yaw);
    right.x = cos(m_yaw);
    right.z = -sin(m_yaw);

    float speed = m_moveSpeed * deltaTime;

    // WASD movement
    if (m_keys['W'] || m_keys['w']) {
        m_position.x += forward.x * speed;
        m_position.z += forward.z * speed;
        moved = true;
    }
    if (m_keys['S'] || m_keys['s']) {
        m_position.x -= forward.x * speed;
        m_position.z -= forward.z * speed;
        moved = true;
    }
    if (m_keys['A'] || m_keys['a']) {
        m_position.x -= right.x * speed;
        m_position.z -= right.z * speed;
        moved = true;
    }
    if (m_keys['D'] || m_keys['d']) {
        m_position.x += right.x * speed;
        m_position.z += right.z * speed;
        moved = true;
    }

    // Up/down
    if (m_keys['Q'] || m_keys['q']) {
        m_position.y += speed;
        moved = true;
    }
    if (m_keys['E'] || m_keys['e']) {
        m_position.y -= speed;
        moved = true;
    }

    if (moved) {
        UpdateMatrices();
    }
}

void Camera::OnMouseMove(int deltaX, int deltaY) {
    m_yaw += deltaX * m_lookSpeed;
    m_pitch += deltaY * m_lookSpeed;

    // Clamp pitch
    m_pitch = std::clamp(m_pitch, -XM_PIDIV2 + 0.1f, XM_PIDIV2 - 0.1f);

    UpdateMatrices();
}

void Camera::SetKeyState(char key, bool pressed) {
    if (key >= 0 && key < 256) {
        m_keys[key] = pressed;
    }
}

void Camera::SetAspectRatio(float aspectRatio) {
    if (aspectRatio != m_aspectRatio) {
        m_aspectRatio = aspectRatio;
        UpdateMatrices();
    }
}

void Camera::UpdateMatrices() {
    // Create view matrix
    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMVECTOR forward = XMVectorSet(sin(m_yaw) * cos(m_pitch), sin(m_pitch), cos(m_yaw) * cos(m_pitch), 0.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR target = XMVectorAdd(pos, forward);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);

    // Store matrices (transposed for HLSL)
    m_constants.view = XMMatrixTranspose(view);
    m_constants.proj = XMMatrixTranspose(proj);
    m_constants.viewInverse = XMMatrixTranspose(XMMatrixInverse(nullptr, view));
    m_constants.projInverse = XMMatrixTranspose(XMMatrixInverse(nullptr, proj));

    // Update constant buffer if it exists
    if (m_mappedData) {
        UpdateConstantBuffer();
    }
}

bool Camera::CreateConstantBuffer(ID3D12Device* device) {
    // Create upload heap for constant buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(Constants);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_constantBuffer));

    if (FAILED(hr)) {
        LOGE("Failed to create camera constant buffer");
        return false;
    }

    // Map for persistent access
    hr = m_constantBuffer->Map(0, nullptr, &m_mappedData);
    if (FAILED(hr)) {
        LOGE("Failed to map camera constant buffer");
        return false;
    }

    // Initial update
    UpdateConstantBuffer();

    LOGI("Camera constant buffer created");
    return true;
}

void Camera::UpdateConstantBuffer() {
    if (m_mappedData) {
        memcpy(m_mappedData, &m_constants, sizeof(Constants));
    }
}