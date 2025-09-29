#include "ProceduralPlasma.h"
#include "../utils/Logger.h"
#include "../utils/DescriptorHeap.h"
#include <cmath>

ProceduralPlasma::ProceduralPlasma() = default;
ProceduralPlasma::~ProceduralPlasma() = default;

bool ProceduralPlasma::Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap) {
    LOGI("Initializing ProceduralPlasma system...");

    m_descriptorHeap = descriptorHeap;
    if (!m_descriptorHeap) {
        LOGE("DescriptorHeap is required for ProceduralPlasma");
        return false;
    }

    // Create constant buffer for ProceduralPlasmaConstants
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = sizeof(ProceduralPlasmaConstants);
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_constantBuffer)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create procedural plasma constant buffer");
        return false;
    }

    // Map constant buffer persistently
    hr = m_constantBuffer->Map(0, nullptr, &m_constantMapped);
    if (FAILED(hr)) {
        LOGE("Failed to map procedural plasma constant buffer");
        return false;
    }

    // Allocate descriptor for constant buffer SRV
    m_constantsSrvIndex = m_descriptorHeap->Allocate();
    if (m_constantsSrvIndex == 0) {
        LOGE("Failed to allocate SRV descriptor for procedural plasma constants");
        return false;
    }
    m_constantsSrvCpu = m_descriptorHeap->GetCPUHandle(m_constantsSrvIndex);
    m_constantsSrvGpu = m_descriptorHeap->GetGPUHandle(m_constantsSrvIndex);

    // Create SRV for constant buffer (structured buffer access in shaders)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = 1;
    srvDesc.Buffer.StructureByteStride = sizeof(ProceduralPlasmaConstants);

    device->CreateShaderResourceView(m_constantBuffer.Get(), &srvDesc, m_constantsSrvCpu);
    LOGI("Created SRV for procedural plasma constants at index " + std::to_string(m_constantsSrvIndex));

    // Initialize plasma parameters with beautiful molten metal settings
    m_constants.containerCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_constants.containerRadius = 2.5f;

    // Create swirling flow pattern for molten metal effect
    m_constants.primaryFlowDirection = XMFLOAT3(1.0f, 0.0f, 0.0f);
    m_constants.flowSpeed = 1.2f;
    m_constants.secondaryFlowDirection = XMFLOAT3(0.0f, 1.0f, 0.0f);
    m_constants.turbulenceStrength = 0.8f;

    // Plasma visual properties - boosted for high visibility
    m_constants.plasmaIntensity = 10.0f;        // Much brighter for visibility
    m_constants.temperatureVariation = 3.0f;   // Higher temperature contrast
    m_constants.noiseScale = 1.0f;             // Larger features for visibility
    m_constants.timeScale = 1.0f;

    // Molten metal color palette: hot orange-yellow to deep red
    m_constants.hotColor = XMFLOAT3(1.0f, 0.7f, 0.2f);    // Bright orange-yellow
    m_constants.coldColor = XMFLOAT3(0.6f, 0.1f, 0.05f);  // Deep red-orange

    m_constants.currentTime = 0.0f;
    m_constants.deltaTime = 0.016f;

    LOGI("ProceduralPlasma initialization complete - ready for molten metal rendering");
    return true;
}

void ProceduralPlasma::Shutdown() {
    if (m_constantMapped) {
        m_constantBuffer->Unmap(0, nullptr);
        m_constantMapped = nullptr;
    }

    if (m_descriptorHeap && m_constantsSrvIndex != 0) {
        // Note: DescriptorHeap may not have explicit Free method - check lifecycle
        m_constantsSrvIndex = 0;
    }

    m_constantBuffer.Reset();
    LOGI("ProceduralPlasma shutdown complete");
}

void ProceduralPlasma::UpdatePhysics(float deltaTime) {
    m_time += deltaTime;
    m_constants.currentTime = m_time;
    m_constants.deltaTime = deltaTime;

    // Create complex, organic flow patterns for molten metal
    float flowTime = m_time * m_constants.flowSpeed;
    float turbTime = m_time * m_constants.turbulenceStrength * 2.0f;

    // Primary flow creates the main current direction
    m_flowOffset.x += m_constants.primaryFlowDirection.x * deltaTime * m_constants.flowSpeed;
    m_flowOffset.y += m_constants.primaryFlowDirection.y * deltaTime * m_constants.flowSpeed;
    m_flowOffset.z += m_constants.primaryFlowDirection.z * deltaTime * m_constants.flowSpeed;

    // Secondary turbulent flow creates swirling patterns
    float turbulentX = std::sin(turbTime * 0.7f) * std::cos(turbTime * 0.3f);
    float turbulentY = std::cos(turbTime * 0.5f) * std::sin(turbTime * 0.8f);
    float turbulentZ = std::sin(turbTime * 0.6f) * std::cos(turbTime * 0.4f);

    m_turbulenceOffset.x += turbulentX * deltaTime * m_constants.turbulenceStrength;
    m_turbulenceOffset.y += turbulentY * deltaTime * m_constants.turbulenceStrength;
    m_turbulenceOffset.z += turbulentZ * deltaTime * m_constants.turbulenceStrength;

    // Automatically evolve the flow direction for dynamic motion
    float evolutionTime = m_time * 0.2f;
    m_constants.primaryFlowDirection.x = std::cos(evolutionTime) * 0.8f + std::sin(evolutionTime * 1.3f) * 0.2f;
    m_constants.primaryFlowDirection.y = std::sin(evolutionTime * 0.7f) * 0.6f;
    m_constants.primaryFlowDirection.z = std::sin(evolutionTime * 1.1f) * 0.4f;

    // Normalize flow direction
    float flowLength = std::sqrt(
        m_constants.primaryFlowDirection.x * m_constants.primaryFlowDirection.x +
        m_constants.primaryFlowDirection.y * m_constants.primaryFlowDirection.y +
        m_constants.primaryFlowDirection.z * m_constants.primaryFlowDirection.z
    );
    if (flowLength > 0.001f) {
        m_constants.primaryFlowDirection.x /= flowLength;
        m_constants.primaryFlowDirection.y /= flowLength;
        m_constants.primaryFlowDirection.z /= flowLength;
    }
}

void ProceduralPlasma::UploadToGPU(ComPtr<ID3D12GraphicsCommandList4> cmdList) {
    // Update constant buffer with current parameters
    if (m_constantMapped) {
        memcpy(m_constantMapped, &m_constants, sizeof(ProceduralPlasmaConstants));
    }

    // Note: For DXR mode 8, the actual GPU data upload is handled by encoding
    // our parameters into the MetaballSystem buffer format for compatibility.
    // This avoids the need for additional resource bindings.
}