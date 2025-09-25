#include "MetaballSystem.h"
#include "../utils/Logger.h"
#include "../utils/DescriptorHeap.h"
#include <cmath>
#include <random>
#include <algorithm>

MetaballSystem::MetaballSystem() = default;
MetaballSystem::~MetaballSystem() = default;

bool MetaballSystem::Initialize(ComPtr<ID3D12Device5> device, DescriptorHeap* descriptorHeap) {
    LOGI("Initializing MetaballSystem for lava lamp simulation...");

    m_descriptorHeap = descriptorHeap;
    if (!m_descriptorHeap) {
        LOGE("DescriptorHeap is required for MetaballSystem");
        return false;
    }

    // Create constant buffer for MetaballConstants
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = sizeof(MetaballConstants);
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
        LOGE("Failed to create metaball constant buffer");
        return false;
    }

    // Map constant buffer persistently
    hr = m_constantBuffer->Map(0, nullptr, &m_constantMapped);
    if (FAILED(hr)) {
        LOGE("Failed to map metaball constant buffer");
        return false;
    }

    // Create metaball data buffer
    D3D12_RESOURCE_DESC mbDesc = {};
    mbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    mbDesc.Width = sizeof(MetaballGPUData) * MAX_METABALLS;
    mbDesc.Height = 1;
    mbDesc.DepthOrArraySize = 1;
    mbDesc.MipLevels = 1;
    mbDesc.Format = DXGI_FORMAT_UNKNOWN;
    mbDesc.SampleDesc.Count = 1;
    mbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &mbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_metaballBuffer)
    );

    if (FAILED(hr)) {
        LOGE("Failed to create metaball data buffer");
        return false;
    }

    // Map metaball buffer persistently
    hr = m_metaballBuffer->Map(0, nullptr, &m_metaballMapped);
    if (FAILED(hr)) {
        LOGE("Failed to map metaball data buffer");
        return false;
    }

    // Create SRV for metaball structured buffer
    m_metaballSrvIndex = m_descriptorHeap->Allocate();
    if (m_metaballSrvIndex == UINT_MAX) {
        LOGE("Failed to allocate SRV descriptor for metaball buffer");
        return false;
    }
    m_metaballSrvCpu = m_descriptorHeap->GetCPUHandle(m_metaballSrvIndex);
    m_metaballSrvGpu = m_descriptorHeap->GetGPUHandle(m_metaballSrvIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.NumElements = MAX_METABALLS;
    srvDesc.Buffer.StructureByteStride = sizeof(MetaballGPUData);

    device->CreateShaderResourceView(m_metaballBuffer.Get(), &srvDesc, m_metaballSrvCpu);
    LOGI("Created SRV for metaball structured buffer at index " + std::to_string(m_metaballSrvIndex));

    // Initialize with lava lamp preset
    SetupLavaLampPreset();

    LOGI("MetaballSystem initialized with " + std::to_string(m_metaballs.size()) + " metaballs");
    return true;
}

void MetaballSystem::Shutdown() {
    if (m_constantBuffer) {
        if (m_constantMapped) {
            m_constantBuffer->Unmap(0, nullptr);
            m_constantMapped = nullptr;
        }
        m_constantBuffer.Reset();
    }

    if (m_metaballBuffer) {
        if (m_metaballMapped) {
            m_metaballBuffer->Unmap(0, nullptr);
            m_metaballMapped = nullptr;
        }
        m_metaballBuffer.Reset();
    }

    // Free SRV descriptor
    if (m_descriptorHeap && m_metaballSrvIndex != UINT32_MAX) {
        m_descriptorHeap->Free(m_metaballSrvIndex);
        m_metaballSrvIndex = UINT32_MAX;
    }

    m_metaballs.clear();
}

void MetaballSystem::SetupLavaLampPreset() {
    LOGI("Setting up lava lamp preset...");
    m_metaballs.clear();

    // Set physics parameters for classic lava lamp behavior
    m_constants.gravity = XMFLOAT3(0.0f, -1.5f, 0.0f);    // Gentle downward pull
    m_constants.buoyancyStrength = 2.5f;                  // Temperature drives upward motion
    m_constants.viscosity = 0.7f;                         // Viscous like real lava lamp
    m_constants.containerRadius = 0.9f;                   // Slightly smaller than render volume
    m_constants.containerCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_constants.mergeDistance = 0.25f;
    m_constants.splitThreshold = 1.2f;
    m_constants.noiseStrength = 0.05f;                    // Subtle organic motion

    // Create initial metaballs with varied temperatures
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-0.6f, 0.6f);
    std::uniform_real_distribution<float> tempDist(0.2f, 0.8f);
    std::uniform_real_distribution<float> sizeDist(0.15f, 0.4f);

    const int numBalls = 8;
    for (int i = 0; i < numBalls; ++i) {
        Metaball ball;
        ball.position = XMFLOAT3(posDist(gen), posDist(gen), posDist(gen));
        ball.velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
        ball.radius = sizeDist(gen);
        ball.targetRadius = ball.radius;
        ball.temperature = tempDist(gen);
        ball.mass = ball.radius * ball.radius * 0.8f; // Mass proportional to area

        // Color based on temperature: hot = orange/yellow, cold = red/purple
        float temp = ball.temperature;
        ball.color = XMFLOAT3(
            1.0f,                                    // Red always high
            0.3f + temp * 0.7f,                     // Green increases with heat
            0.1f + (1.0f - temp) * 0.4f            // Blue higher when cold
        );

        m_metaballs.push_back(ball);
    }

    LOGI("Created " + std::to_string(numBalls) + " metaballs for lava lamp simulation");
}

void MetaballSystem::UpdatePhysics(float deltaTime) {
    m_time += deltaTime;
    m_constants.time = m_time;
    m_constants.deltaTime = deltaTime;
    m_constants.numMetaballs = static_cast<uint32_t>(m_metaballs.size());

    // Update each metaball
    for (auto& metaball : m_metaballs) {
        updateSingleMetaball(metaball, deltaTime);
    }

    // Handle interactions
    handleCollisions();
    handleMergingAndSplitting();
    applyContainerConstraints();
}

Metaball MetaballSystem::createRandomMetaball() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-0.6f, 0.6f);
    std::uniform_real_distribution<float> tempDist(0.2f, 0.8f);
    std::uniform_real_distribution<float> sizeDist(0.15f, 0.4f);

    Metaball ball;
    ball.position = XMFLOAT3(posDist(gen), posDist(gen), posDist(gen));
    ball.velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
    ball.radius = sizeDist(gen);
    ball.targetRadius = ball.radius;
    ball.temperature = tempDist(gen);
    ball.mass = ball.radius * ball.radius * 0.8f;
    float t = ball.temperature;
    ball.color = XMFLOAT3(1.0f, 0.3f + t * 0.7f, 0.1f + (1.0f - t) * 0.4f);
    return ball;
}

bool MetaballSystem::IncreaseCount() {
    if (m_metaballs.size() >= MAX_METABALLS) return false;
    m_metaballs.push_back(createRandomMetaball());
    LOGI("Metaball +1 → count=" + std::to_string(m_metaballs.size()));
    return true;
}

bool MetaballSystem::DecreaseCount() {
    if (m_metaballs.size() <= 1) return false;
    m_metaballs.pop_back();
    LOGI("Metaball -1 → count=" + std::to_string(m_metaballs.size()));
    return true;
}

void MetaballSystem::updateSingleMetaball(Metaball& metaball, float deltaTime) {
    // Temperature-driven buoyancy force (hot rises, cold sinks)
    float buoyancy = (metaball.temperature - 0.5f) * m_constants.buoyancyStrength;
    XMFLOAT3 buoyancyForce = XMFLOAT3(0.0f, buoyancy, 0.0f);

    // Gravity force
    XMFLOAT3 gravityForce = XMFLOAT3(
        m_constants.gravity.x * metaball.mass,
        m_constants.gravity.y * metaball.mass,
        m_constants.gravity.z * metaball.mass
    );

    // Add organic noise for natural motion
    float noiseX = sin(m_time * 1.3f + metaball.position.y * 5.0f) * m_constants.noiseStrength;
    float noiseY = sin(m_time * 0.8f + metaball.position.x * 3.0f) * m_constants.noiseStrength;
    float noiseZ = sin(m_time * 1.1f + metaball.position.z * 4.0f) * m_constants.noiseStrength;
    XMFLOAT3 noiseForce = XMFLOAT3(noiseX, noiseY, noiseZ);

    // Total force
    XMFLOAT3 totalForce = XMFLOAT3(
        buoyancyForce.x + gravityForce.x + noiseForce.x,
        buoyancyForce.y + gravityForce.y + noiseForce.y,
        buoyancyForce.z + gravityForce.z + noiseForce.z
    );

    // Update velocity (F = ma, so a = F/m)
    float invMass = 1.0f / metaball.mass;
    metaball.velocity.x += totalForce.x * invMass * deltaTime;
    metaball.velocity.y += totalForce.y * invMass * deltaTime;
    metaball.velocity.z += totalForce.z * invMass * deltaTime;

    // Apply viscosity (fluid resistance)
    float dampening = 1.0f - (m_constants.viscosity * deltaTime);
    metaball.velocity.x *= dampening;
    metaball.velocity.y *= dampening;
    metaball.velocity.z *= dampening;

    // Update position
    metaball.position.x += metaball.velocity.x * deltaTime;
    metaball.position.y += metaball.velocity.y * deltaTime;
    metaball.position.z += metaball.velocity.z * deltaTime;

    // Smooth radius transitions
    if (abs(metaball.radius - metaball.targetRadius) > 0.01f) {
        float radiusDelta = (metaball.targetRadius - metaball.radius) * 2.0f * deltaTime;
        metaball.radius += radiusDelta;
    }

    // Update age
    metaball.age += deltaTime;
}

void MetaballSystem::applyContainerConstraints() {
    // Keep metaballs within spherical container
    for (auto& metaball : m_metaballs) {
        XMFLOAT3 center = m_constants.containerCenter;
        float radius = m_constants.containerRadius - metaball.radius;

        XMFLOAT3 offset = XMFLOAT3(
            metaball.position.x - center.x,
            metaball.position.y - center.y,
            metaball.position.z - center.z
        );

        float distance = sqrt(offset.x*offset.x + offset.y*offset.y + offset.z*offset.z);

        if (distance > radius) {
            // Push back inside with some bounce
            float pushBack = (distance - radius) / distance;
            metaball.position.x -= offset.x * pushBack;
            metaball.position.y -= offset.y * pushBack;
            metaball.position.z -= offset.z * pushBack;

            // Reflect velocity for bounce effect
            float dot = (metaball.velocity.x * offset.x +
                        metaball.velocity.y * offset.y +
                        metaball.velocity.z * offset.z) / distance;

            metaball.velocity.x -= 1.5f * dot * offset.x / distance;
            metaball.velocity.y -= 1.5f * dot * offset.y / distance;
            metaball.velocity.z -= 1.5f * dot * offset.z / distance;
        }
    }
}

void MetaballSystem::handleCollisions() {
    // Simple collision response between metaballs
    for (size_t i = 0; i < m_metaballs.size(); ++i) {
        for (size_t j = i + 1; j < m_metaballs.size(); ++j) {
            Metaball& a = m_metaballs[i];
            Metaball& b = m_metaballs[j];

            XMFLOAT3 diff = XMFLOAT3(
                a.position.x - b.position.x,
                a.position.y - b.position.y,
                a.position.z - b.position.z
            );

            float distance = sqrt(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);
            float minDistance = (a.radius + b.radius) * 0.8f; // Allow some overlap for merging

            if (distance < minDistance && distance > 0.01f) {
                // Separate the metaballs
                float overlap = minDistance - distance;
                float separationRatio = overlap / (2.0f * distance);

                diff.x *= separationRatio;
                diff.y *= separationRatio;
                diff.z *= separationRatio;

                a.position.x += diff.x;
                a.position.y += diff.y;
                a.position.z += diff.z;

                b.position.x -= diff.x;
                b.position.y -= diff.y;
                b.position.z -= diff.z;

                // Exchange some velocity
                float velExchange = 0.3f;
                XMFLOAT3 tempVel = a.velocity;
                a.velocity.x = a.velocity.x * (1.0f - velExchange) + b.velocity.x * velExchange;
                a.velocity.y = a.velocity.y * (1.0f - velExchange) + b.velocity.y * velExchange;
                a.velocity.z = a.velocity.z * (1.0f - velExchange) + b.velocity.z * velExchange;

                b.velocity.x = b.velocity.x * (1.0f - velExchange) + tempVel.x * velExchange;
                b.velocity.y = b.velocity.y * (1.0f - velExchange) + tempVel.y * velExchange;
                b.velocity.z = b.velocity.z * (1.0f - velExchange) + tempVel.z * velExchange;
            }
        }
    }
}

void MetaballSystem::handleMergingAndSplitting() {
    // Simple merging: if metaballs are very close, merge them
    // This creates the classic lava lamp blob behavior

    for (size_t i = 0; i < m_metaballs.size(); ++i) {
        for (size_t j = i + 1; j < m_metaballs.size(); ++j) {
            Metaball& a = m_metaballs[i];
            Metaball& b = m_metaballs[j];

            XMFLOAT3 diff = XMFLOAT3(
                a.position.x - b.position.x,
                a.position.y - b.position.y,
                a.position.z - b.position.z
            );

            float distance = sqrt(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);

            // Check if they should merge (very close and similar temperature)
            if (distance < m_constants.mergeDistance &&
                abs(a.temperature - b.temperature) < 0.3f &&
                m_metaballs.size() > 3) { // Keep minimum number of balls

                // Merge b into a
                float totalMass = a.mass + b.mass;
                a.position.x = (a.position.x * a.mass + b.position.x * b.mass) / totalMass;
                a.position.y = (a.position.y * a.mass + b.position.y * b.mass) / totalMass;
                a.position.z = (a.position.z * a.mass + b.position.z * b.mass) / totalMass;

                a.velocity.x = (a.velocity.x * a.mass + b.velocity.x * b.mass) / totalMass;
                a.velocity.y = (a.velocity.y * a.mass + b.velocity.y * b.mass) / totalMass;
                a.velocity.z = (a.velocity.z * a.mass + b.velocity.z * b.mass) / totalMass;

                a.temperature = (a.temperature * a.mass + b.temperature * b.mass) / totalMass;
                a.mass = totalMass;
                a.targetRadius = sqrt(a.mass / 0.8f); // Recalculate size

                // Remove the merged metaball
                m_metaballs.erase(m_metaballs.begin() + j);
                break; // Restart loop since we modified the vector
            }
        }
    }
}

void MetaballSystem::UploadToGPU(ComPtr<ID3D12GraphicsCommandList4> cmdList) {
    // Update constant buffer
    if (m_constantMapped) {
        memcpy(m_constantMapped, &m_constants, sizeof(MetaballConstants));
    }

    // Update metaball data buffer
    if (m_metaballMapped && !m_metaballs.empty()) {
        std::vector<MetaballGPUData> gpuData;
        gpuData.reserve(m_metaballs.size());

        for (const auto& metaball : m_metaballs) {
            MetaballGPUData data;
            data.position = metaball.position;
            data.radius = metaball.radius;
            data.velocity = metaball.velocity;
            data.temperature = metaball.temperature;
            data.color = metaball.color;
            data.mass = metaball.mass;
            gpuData.push_back(data);
        }

        size_t dataSize = gpuData.size() * sizeof(MetaballGPUData);
        memcpy(m_metaballMapped, gpuData.data(), dataSize);
    }
}