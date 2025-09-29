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
    // Initialize constants memory to zero to avoid undefined reads in shaders
    if (m_constantMapped) {
        std::memset(m_constantMapped, 0, sizeof(MetaballConstants));
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
    // Zero-initialize the entire GPU data buffer to ensure safe defaults for any unread slots
    if (m_metaballMapped) {
        std::memset(m_metaballMapped, 0, sizeof(MetaballGPUData) * MAX_METABALLS);
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

    // Initialize with metallic six-merge preset by default for Mode 7 experiments
    SetupMetallicSixMergePreset();

    LOGI("MetaballSystem initialized with " + std::to_string(m_metaballs.size()) + " metaballs");
    LOGI("Simple orbital physics: Stable motion around gravity center");
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

    // Set physics parameters for SPH plasma simulation
    m_constants.gravity = XMFLOAT3(0.0f, -2.0f, 0.0f);    // Stronger gravity for plasma
    m_constants.buoyancyStrength = 4.0f;                  // Temperature drives upward motion
    m_constants.viscosity = 0.8f;                         // Fluid resistance
    m_constants.containerRadius = 1.2f;                   // Larger container for 100 particles
    m_constants.containerCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_constants.mergeDistance = 0.15f;                    // Smaller merge distance for more particles
    m_constants.splitThreshold = 1.5f;
    m_constants.noiseStrength = 0.08f;                    // More organic motion

    // SPH physics parameters for realistic fluid dynamics
    m_constants.sphSmoothingRadius = 0.15f;               // Interaction radius between particles
    m_constants.sphRestDensity = 1000.0f;                 // Rest density of plasma fluid
    m_constants.sphPressureConstant = 100.0f;             // Pressure response strength
    m_constants.sphViscosityConstant = 0.3f;              // Viscosity smoothing strength
    m_constants.plasmaIntensity = 1.5f;                   // Emission intensity

    // Create initial metaballs with varied temperatures
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-0.8f, 0.8f);  // Larger spawn area for SPH
    std::uniform_real_distribution<float> tempDist(0.3f, 0.9f);  // Higher temperature range for plasma
    std::uniform_real_distribution<float> sizeDist(0.05f, 0.12f); // Smaller particles for fluid simulation

    const int numBalls = std::min(100, static_cast<int>(MAX_METABALLS));  // Respect buffer limits
    for (int i = 0; i < numBalls; ++i) {
        Metaball ball;
        ball.position = XMFLOAT3(posDist(gen), posDist(gen), posDist(gen));
        ball.velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
        ball.radius = sizeDist(gen);
        ball.targetRadius = ball.radius;
        ball.temperature = tempDist(gen);
        ball.mass = ball.radius * ball.radius * 0.8f; // Mass proportional to area

        // Initialize SPH properties
        ball.density = m_constants.sphRestDensity;   // Start at rest density
        ball.pressure = 0.0f;                        // No initial pressure
        ball.pressureForce = XMFLOAT3(0.0f, 0.0f, 0.0f);
        ball.viscosityForce = XMFLOAT3(0.0f, 0.0f, 0.0f);

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

void MetaballSystem::SetupMetallicSixMergePreset() {
    LOGI("Setting up metallic six-merge preset...");
    m_metaballs.clear();

    // Physics tuned for slow, heavy metallic blobs
    m_constants.gravity = XMFLOAT3(0.0f, -1.0f, 0.0f);
    m_constants.buoyancyStrength = 0.6f;
    m_constants.viscosity = 1.2f;           // Higher viscosity for slow motion
    m_constants.containerRadius = 1.4f;     // Slightly larger container
    m_constants.containerCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_constants.mergeDistance = 0.22f;      // Encourage merging
    m_constants.splitThreshold = 2.2f;      // Avoid frequent splits
    m_constants.noiseStrength = 0.02f;      // Minimal noise for smooth metallic motion

    // SPH-ish params (even if we use simple orbital, keep consistent ranges)
    m_constants.sphSmoothingRadius = 0.18f;
    m_constants.sphRestDensity = 1100.0f;
    m_constants.sphPressureConstant = 80.0f;
    m_constants.sphViscosityConstant = 0.5f;
    m_constants.plasmaIntensity = 1.0f;

    // Place 6 larger metaballs around a ring for natural merging
    const int numBalls = 6;
    const float ringRadius = 0.7f;
    for (int i = 0; i < numBalls; ++i) {
        float angle = float(i) / float(numBalls) * 6.2831853f; // 2*pi
        Metaball ball;
        ball.position = XMFLOAT3(ringRadius * std::cos(angle), 0.0f, ringRadius * std::sin(angle));
        ball.velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
        ball.radius = 0.20f;         // Larger for metallic lobe look
        ball.targetRadius = ball.radius;
        ball.temperature = 0.55f;    // Moderate
        ball.mass = ball.radius * ball.radius * 0.8f;
        // Cooler metallic tones (will be recolored in shader to be metallic)
        ball.color = XMFLOAT3(0.9f, 0.9f, 0.95f);

        // Initialize SPH properties
        ball.density = m_constants.sphRestDensity;
        ball.pressure = 0.0f;
        ball.pressureForce = XMFLOAT3(0.0f, 0.0f, 0.0f);
        ball.viscosityForce = XMFLOAT3(0.0f, 0.0f, 0.0f);

        m_metaballs.push_back(ball);
    }

    LOGI("Created 6 metallic metaballs (slow merge preset)");
}

void MetaballSystem::UpdatePhysics(float deltaTime) {
    m_time += deltaTime;
    m_constants.time = m_time;
    m_constants.deltaTime = deltaTime;
    m_constants.numMetaballs = static_cast<uint32_t>(m_metaballs.size());

    // Slow, smooth orbital motion; then apply collisions and merging
    updateSimpleOrbitalPhysics(deltaTime * 0.5f);
    handleCollisions();
    handleMergingAndSplitting();

    // Always apply container constraints
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

void MetaballSystem::updateSimpleOrbitalPhysics(float deltaTime) {
    // Simple orbital physics around a central gravity point for beautiful, dynamic motion
    const XMFLOAT3& gravityCenter = m_constants.containerCenter;  // Gravity center point
    const float gravityStrength = 4.0f;   // Stronger gravity for faster motion
    const float orbitalSpeed = 3.0f;      // Faster orbital velocity for visibility
    const float dampingFactor = 0.95f;    // Less damping for more dynamic motion

    // Simple orbital motion around gravity center for beautiful, stable animation
    for (auto& metaball : m_metaballs) {
        // Calculate vector from metaball to gravity center
        XMFLOAT3 toCenter = {
            gravityCenter.x - metaball.position.x,
            gravityCenter.y - metaball.position.y,
            gravityCenter.z - metaball.position.z
        };

        float distance = std::sqrt(toCenter.x * toCenter.x + toCenter.y * toCenter.y + toCenter.z * toCenter.z);

        if (distance > 0.001f) {
            // Normalize direction vector
            XMFLOAT3 direction = {
                toCenter.x / distance,
                toCenter.y / distance,
                toCenter.z / distance
            };

            // Apply gravity force toward center (distance-based falloff)
            float gravityForce = gravityStrength / (1.0f + distance * 0.5f);
            XMFLOAT3 gravity = {
                direction.x * gravityForce,
                direction.y * gravityForce,
                direction.z * gravityForce
            };

            // Calculate 3D orbital motion with varied orbital planes per metaball
            // Use metaball's initial properties to determine its orbital plane
            float orbitAngle = metaball.mass * 6.28f; // Different orbit angle per metaball
            float inclination = metaball.temperature * 1.57f; // 0 to PI/2 inclination based on temperature

            // Create two perpendicular vectors to the radial direction for 3D orbital motion
            XMFLOAT3 axis1 = { -toCenter.y, toCenter.x, 0.0f };
            XMFLOAT3 axis2 = {
                toCenter.x * toCenter.z,
                toCenter.y * toCenter.z,
                -(toCenter.x * toCenter.x + toCenter.y * toCenter.y)
            };

            // Normalize the axes
            float len1 = std::sqrt(axis1.x*axis1.x + axis1.y*axis1.y + axis1.z*axis1.z);
            float len2 = std::sqrt(axis2.x*axis2.x + axis2.y*axis2.y + axis2.z*axis2.z);
            if (len1 > 0.001f) { axis1.x /= len1; axis1.y /= len1; axis1.z /= len1; }
            if (len2 > 0.001f) { axis2.x /= len2; axis2.y /= len2; axis2.z /= len2; }

            // Combine axes with time-based rotation for orbital motion
            float timeAngle = m_time * orbitalSpeed + orbitAngle;
            XMFLOAT3 tangent = {
                axis1.x * std::cos(timeAngle) + axis2.x * std::sin(timeAngle) * std::cos(inclination),
                axis1.y * std::cos(timeAngle) + axis2.y * std::sin(timeAngle) * std::cos(inclination),
                axis1.z * std::cos(timeAngle) + axis2.z * std::sin(timeAngle) * std::cos(inclination)
            };

            float tangentLength = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
            if (tangentLength > 0.001f) {
                tangent.x /= tangentLength;
                tangent.y /= tangentLength;
                tangent.z /= tangentLength;
            }

            // Apply orbital velocity (temperature affects speed)
            float speedMultiplier = orbitalSpeed * (0.8f + metaball.temperature * 0.4f);
            XMFLOAT3 orbitalForce = {
                tangent.x * speedMultiplier,
                tangent.y * speedMultiplier,
                tangent.z * speedMultiplier
            };

            // Temperature-based vertical motion (hot rises, cold sinks)
            float buoyancy = m_constants.buoyancyStrength * (metaball.temperature - 0.5f) * 2.0f;

            // Combine all forces
            XMFLOAT3 totalForce = {
                gravity.x + orbitalForce.x,
                gravity.y + orbitalForce.y + buoyancy,
                gravity.z + orbitalForce.z
            };

            // Update velocity with forces
            metaball.velocity.x = (metaball.velocity.x + totalForce.x * deltaTime) * dampingFactor;
            metaball.velocity.y = (metaball.velocity.y + totalForce.y * deltaTime) * dampingFactor;
            metaball.velocity.z = (metaball.velocity.z + totalForce.z * deltaTime) * dampingFactor;

            // Update position with velocity
            metaball.position.x += metaball.velocity.x * deltaTime;
            metaball.position.y += metaball.velocity.y * deltaTime;
            metaball.position.z += metaball.velocity.z * deltaTime;

            // Smooth size variation based on temperature and motion
            float targetRadius = 0.06f + metaball.temperature * 0.06f + std::sin(m_time * 2.0f + distance) * 0.02f;
            metaball.radius = metaball.radius * 0.95f + targetRadius * 0.05f; // Smooth interpolation
        }
    }
}

void MetaballSystem::UploadToGPU(ComPtr<ID3D12GraphicsCommandList4> cmdList) {
    // Update constant buffer
    if (m_constantMapped) {
        memcpy(m_constantMapped, &m_constants, sizeof(MetaballConstants));
    }

    // Update metaball data buffer with bounds checking
    if (m_metaballMapped && !m_metaballs.empty()) {
        std::vector<MetaballGPUData> gpuData;

        // Critical: Prevent buffer overflow by limiting to MAX_METABALLS
        size_t metaballCount = std::min(m_metaballs.size(), static_cast<size_t>(MAX_METABALLS));
        gpuData.reserve(metaballCount);

        for (size_t i = 0; i < metaballCount; ++i) {
            const auto& metaball = m_metaballs[i];
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
        const size_t maxBufferSize = MAX_METABALLS * sizeof(MetaballGPUData);

        // Double-check buffer bounds before copy
        if (dataSize <= maxBufferSize) {
            // Copy used elements
            std::memcpy(m_metaballMapped, gpuData.data(), dataSize);
            // Zero the remainder to avoid shaders reading stale/uninitialized data
            size_t remainder = maxBufferSize - dataSize;
            if (remainder > 0) {
                std::memset(static_cast<char*>(m_metaballMapped) + dataSize, 0, remainder);
            }
        } else {
            LOGE("Buffer overflow prevented: trying to copy " + std::to_string(dataSize) +
                 " bytes into " + std::to_string(maxBufferSize) + " byte buffer");
        }
    }
}

void MetaballSystem::EncodeProceduralPlasmaData(const XMFLOAT3& containerCenter, float containerRadius,
                                               const XMFLOAT3& flowDirection, float flowSpeed,
                                               const XMFLOAT3& secondaryFlow, float turbulence) {
    // Clear any existing metaballs and encode procedural plasma parameters as a single "metaball"
    m_metaballs.clear();

    Metaball plasmaMetaball;
    // Encode parameters using metaball fields (shader will interpret these correctly)
    plasmaMetaball.position = containerCenter;        // Container center
    plasmaMetaball.radius = containerRadius;          // Container radius
    plasmaMetaball.velocity = flowDirection;          // Primary flow direction
    plasmaMetaball.temperature = flowSpeed;           // Flow speed
    plasmaMetaball.color = secondaryFlow;             // Secondary flow direction
    plasmaMetaball.mass = turbulence;                 // Turbulence strength

    // Add procedural plasma parameters as a single metaball
    m_metaballs.push_back(plasmaMetaball);

    LOGI("Encoded procedural plasma parameters as metaball data for Mode 8");
}

