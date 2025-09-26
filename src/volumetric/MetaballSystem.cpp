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

    const int numBalls = 100;  // Increased for SPH simulation
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

void MetaballSystem::UpdatePhysics(float deltaTime) {
    m_time += deltaTime;
    m_constants.time = m_time;
    m_constants.deltaTime = deltaTime;
    m_constants.numMetaballs = static_cast<uint32_t>(m_metaballs.size());

    // SPH fluid simulation for realistic plasma behavior
    if (m_metaballs.size() > 50) {
        // Use SPH for large particle counts (realistic fluid dynamics)
        updateSPHPhysics(deltaTime);
    } else {
        // Use simple physics for small counts (fallback)
        for (auto& metaball : m_metaballs) {
            updateSingleMetaball(metaball, deltaTime);
        }
        handleCollisions();
        handleMergingAndSplitting();
    }

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

// ============================================================================
// SPH FLUID PHYSICS IMPLEMENTATION
// ============================================================================

void MetaballSystem::updateSPHPhysics(float deltaTime) {
    // Step 1: Calculate density and pressure for all particles
    calculateDensityAndPressure();

    // Step 2: Calculate pressure forces
    calculatePressureForces();

    // Step 3: Calculate viscosity forces
    calculateViscosityForces();

    // Step 4: Integrate forces and update positions
    integrateSPHForces(deltaTime);
}

void MetaballSystem::calculateDensityAndPressure() {
    const float h = m_constants.sphSmoothingRadius;
    const float h2 = h * h;

    // Reset densities
    for (auto& particle : m_metaballs) {
        particle.density = 0.0f;
    }

    // Calculate density for each particle
    for (size_t i = 0; i < m_metaballs.size(); ++i) {
        auto& pi = m_metaballs[i];

        for (size_t j = 0; j < m_metaballs.size(); ++j) {
            auto& pj = m_metaballs[j];

            XMFLOAT3 diff = {
                pi.position.x - pj.position.x,
                pi.position.y - pj.position.y,
                pi.position.z - pj.position.z
            };

            float distance2 = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

            if (distance2 < h2) {
                float distance = sqrtf(distance2);
                pi.density += pj.mass * sphKernel(distance, h);
            }
        }

        // Calculate pressure from density (equation of state)
        pi.pressure = m_constants.sphPressureConstant * (pi.density - m_constants.sphRestDensity);

        // Ensure minimum density to avoid instabilities
        pi.density = std::max(pi.density, m_constants.sphRestDensity * 0.1f);
    }
}

void MetaballSystem::calculatePressureForces() {
    const float h = m_constants.sphSmoothingRadius;
    const float h2 = h * h;

    // Reset pressure forces
    for (auto& particle : m_metaballs) {
        particle.pressureForce = { 0.0f, 0.0f, 0.0f };
    }

    // Calculate pressure forces between particles
    for (size_t i = 0; i < m_metaballs.size(); ++i) {
        auto& pi = m_metaballs[i];

        for (size_t j = i + 1; j < m_metaballs.size(); ++j) {
            auto& pj = m_metaballs[j];

            XMFLOAT3 diff = {
                pi.position.x - pj.position.x,
                pi.position.y - pj.position.y,
                pi.position.z - pj.position.z
            };

            float distance2 = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

            if (distance2 < h2) {
                float distance = sqrtf(distance2);
                if (distance > 0.0001f) {
                    // Pressure gradient kernel
                    XMFLOAT3 gradient = sphKernelGradient(diff, distance, h);

                    // Symmetric pressure force
                    float pressureTerm = (pi.pressure / (pi.density * pi.density) +
                                         pj.pressure / (pj.density * pj.density));

                    XMFLOAT3 force = {
                        -pj.mass * pressureTerm * gradient.x,
                        -pj.mass * pressureTerm * gradient.y,
                        -pj.mass * pressureTerm * gradient.z
                    };

                    // Apply force (Newton's 3rd law)
                    pi.pressureForce.x += force.x;
                    pi.pressureForce.y += force.y;
                    pi.pressureForce.z += force.z;

                    pj.pressureForce.x -= force.x;
                    pj.pressureForce.y -= force.y;
                    pj.pressureForce.z -= force.z;
                }
            }
        }
    }
}

void MetaballSystem::calculateViscosityForces() {
    const float h = m_constants.sphSmoothingRadius;
    const float h2 = h * h;
    const float viscosity = m_constants.sphViscosityConstant;

    // Reset viscosity forces
    for (auto& particle : m_metaballs) {
        particle.viscosityForce = { 0.0f, 0.0f, 0.0f };
    }

    // Calculate viscosity forces between particles
    for (size_t i = 0; i < m_metaballs.size(); ++i) {
        auto& pi = m_metaballs[i];

        for (size_t j = 0; j < m_metaballs.size(); ++j) {
            if (i == j) continue;

            auto& pj = m_metaballs[j];

            XMFLOAT3 diff = {
                pi.position.x - pj.position.x,
                pi.position.y - pj.position.y,
                pi.position.z - pj.position.z
            };

            float distance2 = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

            if (distance2 < h2) {
                float distance = sqrtf(distance2);

                // Velocity difference
                XMFLOAT3 velDiff = {
                    pj.velocity.x - pi.velocity.x,
                    pj.velocity.y - pi.velocity.y,
                    pj.velocity.z - pi.velocity.z
                };

                // Viscosity kernel (Laplacian)
                float kernel = sphKernelDerivative(distance, h);

                float viscosityTerm = viscosity * pj.mass / pj.density * kernel;

                pi.viscosityForce.x += viscosityTerm * velDiff.x;
                pi.viscosityForce.y += viscosityTerm * velDiff.y;
                pi.viscosityForce.z += viscosityTerm * velDiff.z;
            }
        }
    }
}

void MetaballSystem::integrateSPHForces(float deltaTime) {
    const XMFLOAT3 gravity = m_constants.gravity;

    for (auto& particle : m_metaballs) {
        // Combine all forces
        XMFLOAT3 totalForce = {
            particle.pressureForce.x + particle.viscosityForce.x + gravity.x * particle.mass,
            particle.pressureForce.y + particle.viscosityForce.y + gravity.y * particle.mass,
            particle.pressureForce.z + particle.viscosityForce.z + gravity.z * particle.mass
        };

        // Add temperature-based buoyancy (plasma rises when hot)
        totalForce.y += particle.temperature * m_constants.buoyancyStrength * particle.mass;

        // Update velocity (F = ma, so a = F/m)
        float invMass = 1.0f / particle.mass;
        particle.velocity.x += totalForce.x * invMass * deltaTime;
        particle.velocity.y += totalForce.y * invMass * deltaTime;
        particle.velocity.z += totalForce.z * invMass * deltaTime;

        // Damping to prevent explosion
        const float damping = 0.99f;
        particle.velocity.x *= damping;
        particle.velocity.y *= damping;
        particle.velocity.z *= damping;

        // Update position
        particle.position.x += particle.velocity.x * deltaTime;
        particle.position.y += particle.velocity.y * deltaTime;
        particle.position.z += particle.velocity.z * deltaTime;

        // Update visual properties based on density and temperature
        particle.radius = 0.08f + (particle.density / m_constants.sphRestDensity) * 0.04f;
        particle.radius = std::max(0.05f, std::min(particle.radius, 0.15f));

        // Color based on temperature and pressure
        float tempFactor = std::max(0.0f, std::min(particle.temperature, 1.0f));
        float pressureFactor = std::max(0.0f, std::min(particle.pressure / m_constants.sphPressureConstant, 1.0f));

        particle.color.x = 0.8f + tempFactor * 0.2f;  // Red: hotter = more red
        particle.color.y = 0.3f + pressureFactor * 0.4f; // Green: higher pressure = more green
        particle.color.z = 0.1f + (1.0f - tempFactor) * 0.6f; // Blue: cooler = more blue
    }
}

// SPH kernel functions (Poly6 kernel)
float MetaballSystem::sphKernel(float distance, float smoothingRadius) const {
    if (distance >= smoothingRadius) return 0.0f;

    float h2 = smoothingRadius * smoothingRadius;
    float q = distance / smoothingRadius;
    float factor = h2 - distance * distance;
    return (315.0f / (64.0f * 3.14159f * h2 * h2 * smoothingRadius)) * factor * factor * factor;
}

float MetaballSystem::sphKernelDerivative(float distance, float smoothingRadius) const {
    if (distance >= smoothingRadius || distance <= 0.0001f) return 0.0f;

    float h2 = smoothingRadius * smoothingRadius;
    float factor = h2 - distance * distance;
    return (315.0f / (64.0f * 3.14159f * h2 * h2 * smoothingRadius)) * 3.0f * factor * factor * (-2.0f * distance);
}

XMFLOAT3 MetaballSystem::sphKernelGradient(const XMFLOAT3& vec, float distance, float smoothingRadius) const {
    if (distance >= smoothingRadius || distance <= 0.0001f) {
        return { 0.0f, 0.0f, 0.0f };
    }

    float kernelDeriv = sphKernelDerivative(distance, smoothingRadius);
    float invDistance = 1.0f / distance;

    return {
        kernelDeriv * vec.x * invDistance,
        kernelDeriv * vec.y * invDistance,
        kernelDeriv * vec.z * invDistance
    };
}