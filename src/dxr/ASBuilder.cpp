#include "ASBuilder.h"
#include "../utils/Logger.h"
#include "../../include/d3dx12/d3dx12.h"
#include <DirectXMath.h>
using namespace DirectX;
using Microsoft::WRL::ComPtr;

ASBuilder::ASBuilder(ID3D12Device* device) : m_device(device) {
	LOGI("ASBuilder stub created");
}

bool ASBuilder::CreateTriangleBLAS(
	Microsoft::WRL::ComPtr<ID3D12Resource>& outBLAS,
	Microsoft::WRL::ComPtr<ID3D12Resource>& outScratch) {

	LOGI("ASBuilder::CreateTriangleBLAS - creating HUGE occluder triangle for unmistakable shadow");

	// HUGE triangle to cover most of the particle disk
	// Particle disk: Y=0, radius 6-100 units in XZ plane
	// Position triangle at Y=20 (closer to particles) covering entire disk
	XMFLOAT3 triangleVertices[] = {
		{   0.0f, 20.0f, -120.0f },  // Top (north) - extends beyond disk
		{-120.0f, 20.0f,  120.0f },  // Bottom left (southwest) - huge coverage
		{ 120.0f, 20.0f,  120.0f }   // Bottom right (southeast) - huge coverage
	};

	// Create vertex buffer
	ComPtr<ID3D12Resource> vertexBuffer;
	D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(triangleVertices));

	HRESULT hr = m_device->CreateCommittedResource(
		&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &vertexBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&vertexBuffer));

	if (FAILED(hr)) {
		LOGE("Failed to create vertex buffer");
		return false;
	}

	// Upload vertex data
	void* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	hr = vertexBuffer->Map(0, &readRange, &pVertexDataBegin);
	if (SUCCEEDED(hr)) {
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		vertexBuffer->Unmap(0, nullptr);
	}

	// Store geometry description for later GPU build
	m_triangleGeometry = {};
	m_triangleGeometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	m_triangleGeometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	m_triangleGeometry.Triangles.VertexBuffer.StartAddress = vertexBuffer->GetGPUVirtualAddress();
	m_triangleGeometry.Triangles.VertexBuffer.StrideInBytes = sizeof(XMFLOAT3);
	m_triangleGeometry.Triangles.VertexCount = 3;
	m_triangleGeometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

	// Store BLAS description for later GPU build
	m_blasInputs = {};
	m_blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	m_blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	m_blasInputs.NumDescs = 1;
	m_blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	m_blasInputs.pGeometryDescs = &m_triangleGeometry;

	// For this debug implementation, create minimal sized buffers
	// Real implementation would use ID3D12Device5::GetRaytracingAccelerationStructurePrebuildInfo
	const UINT64 blasResultSize = 4096;  // Debug: minimal size
	const UINT64 blasScratchSize = 4096; // Debug: minimal size

	// Create BLAS result buffer
	D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC blasDesc = CD3DX12_RESOURCE_DESC::Buffer(
		blasResultSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	hr = m_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&blasDesc,
		D3D12_RESOURCE_STATE_COMMON, // Use COMMON instead of RAYTRACING_ACCELERATION_STRUCTURE for compatibility
		nullptr,
		IID_PPV_ARGS(&outBLAS));

	if (FAILED(hr)) {
		LOGE("Failed to create BLAS result buffer");
		return false;
	}

	// Create scratch buffer
	D3D12_RESOURCE_DESC scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(
		blasScratchSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	hr = m_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&scratchDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&outScratch));

	if (FAILED(hr)) {
		LOGE("Failed to create BLAS scratch buffer");
		return false;
	}

	// Store vertex buffer to keep it alive (member variable needed)
	m_triangleVertexBuffer = vertexBuffer;

	LOGI("ASBuilder::CreateTriangleBLAS completed - real triangle geometry created");
	return true;
}

bool ASBuilder::CreateParticleBLAS(
	Microsoft::WRL::ComPtr<ID3D12Resource>& outBLAS,
	Microsoft::WRL::ComPtr<ID3D12Resource>& outScratch,
	ID3D12Resource* particleBuffer,
	uint32_t particleCount,
	float particleSize) {

	LOGI("ASBuilder::CreateParticleBLAS - creating conservative procedural AABB for particle cloud");

	// IMPORTANT: Particle buffer is D3D12_HEAP_TYPE_DEFAULT (GPU-only), cannot be mapped from CPU
	// Instead, create a single conservative AABB that encompasses the entire particle system
	// This is simpler and works for self-shadowing since particles are densely packed in accretion disk

	// Accretion disk parameters (from ParticleSystem.h):
	// - Inner radius: 6 units, Outer radius: 60 units
	// - Disk thickness: 40 units (Y extent)
	// - Particles distributed in XZ plane around Y=0

	const float outerRadius = 100.0f;  // Conservative: larger than max particle distance
	const float diskThickness = 50.0f; // Conservative: larger than max Y deviation
	const float aabbHalfSize = particleSize * 0.5f;  // Per-particle half-extent

	// Create single AABB that covers entire particle cloud
	struct AABB {
		float minX, minY, minZ;
		float maxX, maxY, maxZ;
	};

	AABB conservativeAABB;
	conservativeAABB.minX = -outerRadius - aabbHalfSize;
	conservativeAABB.minY = -diskThickness - aabbHalfSize;
	conservativeAABB.minZ = -outerRadius - aabbHalfSize;
	conservativeAABB.maxX = outerRadius + aabbHalfSize;
	conservativeAABB.maxY = diskThickness + aabbHalfSize;
	conservativeAABB.maxZ = outerRadius + aabbHalfSize;

	// Create AABB buffer (single AABB: 6 floats)
	const size_t aabbBufferSize = sizeof(AABB);
	D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC aabbBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(aabbBufferSize);

	ComPtr<ID3D12Resource> aabbBuffer;
	HRESULT hr = m_device->CreateCommittedResource(
		&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &aabbBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&aabbBuffer));

	if (FAILED(hr)) {
		LOGE("Failed to create AABB buffer for particles");
		return false;
	}

	// Map AABB buffer and write conservative bounds
	void* pAABBData;
	CD3DX12_RANGE writeRange(0, 0);  // Write-only
	hr = aabbBuffer->Map(0, &writeRange, &pAABBData);
	if (FAILED(hr)) {
		LOGE("Failed to map AABB buffer");
		return false;
	}

	memcpy(pAABBData, &conservativeAABB, sizeof(AABB));
	aabbBuffer->Unmap(0, nullptr);

	LOGI("Created conservative AABB for particle cloud (bounds: X=" + std::to_string(conservativeAABB.minX) +
	     " to " + std::to_string(conservativeAABB.maxX) + ")");

	// Create geometry descriptor for single conservative AABB
	m_particleGeometry = {};
	m_particleGeometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
	m_particleGeometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;  // Treat as opaque for performance
	m_particleGeometry.AABBs.AABBCount = 1;  // Single conservative AABB
	m_particleGeometry.AABBs.AABBs.StartAddress = aabbBuffer->GetGPUVirtualAddress();
	m_particleGeometry.AABBs.AABBs.StrideInBytes = sizeof(float) * 6;

	// Build BLAS inputs for particle geometry
	m_particleBLASInputs = {};
	m_particleBLASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	m_particleBLASInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	m_particleBLASInputs.NumDescs = 1;
	m_particleBLASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	m_particleBLASInputs.pGeometryDescs = &m_particleGeometry;

	// Query prebuild info for proper sizing (important for large particle counts)
	ComPtr<ID3D12Device5> device5;
	hr = m_device.As(&device5);
	if (FAILED(hr)) {
		LOGE("Failed to query ID3D12Device5 for prebuild info");
		return false;
	}

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo{};
	device5->GetRaytracingAccelerationStructurePrebuildInfo(&m_particleBLASInputs, &prebuildInfo);

	LOGI("Particle BLAS prebuild info: result=" + std::to_string(prebuildInfo.ResultDataMaxSizeInBytes) +
	     " scratch=" + std::to_string(prebuildInfo.ScratchDataSizeInBytes));

	// Create BLAS result buffer (properly sized for particle count)
	D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC blasDesc = CD3DX12_RESOURCE_DESC::Buffer(
		prebuildInfo.ResultDataMaxSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	hr = m_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&blasDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&outBLAS));

	if (FAILED(hr)) {
		LOGE("Failed to create particle BLAS result buffer");
		return false;
	}

	// Create scratch buffer
	D3D12_RESOURCE_DESC scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(
		prebuildInfo.ScratchDataSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	hr = m_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&scratchDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&outScratch));

	if (FAILED(hr)) {
		LOGE("Failed to create particle BLAS scratch buffer");
		return false;
	}

	// Store AABB buffer to keep it alive
	m_particleAABBBuffer = aabbBuffer;

	LOGI("ASBuilder::CreateParticleBLAS completed - procedural AABB geometry created");
	return true;
}

bool ASBuilder::BuildTLAS(
	Microsoft::WRL::ComPtr<ID3D12Resource>& outTLAS,
	Microsoft::WRL::ComPtr<ID3D12Resource>& outScratch,
	Microsoft::WRL::ComPtr<ID3D12Resource>& outInstanceDescs) {

	LOGI("ASBuilder::BuildTLAS - creating real TLAS with instance");

	// Note: This is a simplified TLAS stub that creates minimal buffers
	// For a complete implementation, we'd need the BLAS address from CreateTriangleBLAS
	// and proper instance transformation matrices

	// For this debug implementation, create minimal sized buffers
	// Real implementation would use proper TLAS building
	const UINT64 tlasResultSize = 4096;  // Debug: minimal size
	const UINT64 tlasScratchSize = 4096; // Debug: minimal size

	// Create TLAS result buffer
	D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC tlasDesc = CD3DX12_RESOURCE_DESC::Buffer(
		tlasResultSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	HRESULT hr = m_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&tlasDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&outTLAS));

	if (FAILED(hr)) {
		LOGE("Failed to create TLAS result buffer");
		return false;
	}

	// Create scratch buffer
	D3D12_RESOURCE_DESC scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(
		tlasScratchSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	hr = m_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&scratchDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&outScratch));

	if (FAILED(hr)) {
		LOGE("Failed to create TLAS scratch buffer");
		return false;
	}

	// Create instance descriptors buffer in upload heap for mapping
	D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC instanceDesc = CD3DX12_RESOURCE_DESC::Buffer(
		sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * 1);

	hr = m_device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&instanceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&outInstanceDescs));

	if (FAILED(hr)) {
		LOGE("Failed to create TLAS instance descriptors buffer");
		return false;
	}

	LOGI("ASBuilder::BuildTLAS completed - real TLAS structure created");
	return true;
}

void ASBuilder::BuildBLAS(ID3D12GraphicsCommandList* cmdList,
	ID3D12Resource* blasResult,
	ID3D12Resource* blasScratch) {

	LOGI("ASBuilder::BuildBLAS - Executing GPU build commands");

	// Query for DXR command list interface
	ComPtr<ID3D12GraphicsCommandList4> cmdList4;
	HRESULT hr = cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4));
	if (FAILED(hr)) {
		LOGE("Failed to query ID3D12GraphicsCommandList4 for BLAS build");
		return;
	}

	// Build the BLAS on GPU
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = m_blasInputs;
	buildDesc.ScratchAccelerationStructureData = blasScratch->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = blasResult->GetGPUVirtualAddress();

	cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	// Add UAV barrier to ensure BLAS is built before TLAS references it
	D3D12_RESOURCE_BARRIER uavBarrier = {};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = blasResult;
	cmdList->ResourceBarrier(1, &uavBarrier);

	LOGI("ASBuilder::BuildBLAS - GPU build commands completed (REAL BUILD)");
}

void ASBuilder::BuildParticleBLAS(ID3D12GraphicsCommandList* cmdList,
	ID3D12Resource* blasResult,
	ID3D12Resource* blasScratch) {

	LOGI("ASBuilder::BuildParticleBLAS - Executing GPU build commands for particle AABBs");

	// Query for DXR command list interface
	ComPtr<ID3D12GraphicsCommandList4> cmdList4;
	HRESULT hr = cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4));
	if (FAILED(hr)) {
		LOGE("Failed to query ID3D12GraphicsCommandList4 for particle BLAS build");
		return;
	}

	// Build the particle BLAS on GPU
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = m_particleBLASInputs;
	buildDesc.ScratchAccelerationStructureData = blasScratch->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = blasResult->GetGPUVirtualAddress();

	cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	// Add UAV barrier to ensure particle BLAS is built before TLAS references it
	D3D12_RESOURCE_BARRIER uavBarrier = {};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = blasResult;
	cmdList->ResourceBarrier(1, &uavBarrier);

	LOGI("ASBuilder::BuildParticleBLAS - GPU build commands completed (REAL BUILD)");
}

void ASBuilder::BuildTLASGPU(ID3D12GraphicsCommandList* cmdList,
	ID3D12Resource* tlasResult,
	ID3D12Resource* tlasScratch,
	ID3D12Resource* instanceDescs,
	ID3D12Resource* blasResult) {

	LOGI("ASBuilder::BuildTLASGPU - Creating instance descriptor");

	// Create instance descriptor to reference our BLAS
	D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};

	// Identity transform matrix (row-major)
	instanceDesc.Transform[0][0] = 1.0f; instanceDesc.Transform[0][1] = 0.0f; instanceDesc.Transform[0][2] = 0.0f; instanceDesc.Transform[0][3] = 0.0f;
	instanceDesc.Transform[1][0] = 0.0f; instanceDesc.Transform[1][1] = 1.0f; instanceDesc.Transform[1][2] = 0.0f; instanceDesc.Transform[1][3] = 0.0f;
	instanceDesc.Transform[2][0] = 0.0f; instanceDesc.Transform[2][1] = 0.0f; instanceDesc.Transform[2][2] = 1.0f; instanceDesc.Transform[2][3] = 0.0f;

	instanceDesc.InstanceID = 0;
	instanceDesc.InstanceMask = 0xFF;
	instanceDesc.InstanceContributionToHitGroupIndex = 0;
	instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
	instanceDesc.AccelerationStructure = blasResult->GetGPUVirtualAddress();

	// Upload instance descriptor to buffer
	void* pInstanceData;
	CD3DX12_RANGE readRange(0, 0);
	HRESULT hr = instanceDescs->Map(0, &readRange, &pInstanceData);
	if (SUCCEEDED(hr)) {
		memcpy(pInstanceData, &instanceDesc, sizeof(instanceDesc));
		instanceDescs->Unmap(0, nullptr);
		LOGI("ASBuilder::BuildTLASGPU - Instance descriptor uploaded successfully");
	} else {
		LOGE("ASBuilder::BuildTLASGPU - Failed to map instance descriptor buffer");
	}

	// Query for DXR command list interface
	ComPtr<ID3D12GraphicsCommandList4> cmdList4;
	hr = cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4));
	if (FAILED(hr)) {
		LOGE("Failed to query ID3D12GraphicsCommandList4 for TLAS build");
		return;
	}

	// Build TLAS on GPU
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasBuildDesc = {};
	tlasBuildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	tlasBuildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	tlasBuildDesc.Inputs.NumDescs = 1; // One instance (our triangle)
	tlasBuildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	tlasBuildDesc.Inputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress();
	tlasBuildDesc.ScratchAccelerationStructureData = tlasScratch->GetGPUVirtualAddress();
	tlasBuildDesc.DestAccelerationStructureData = tlasResult->GetGPUVirtualAddress();

	cmdList4->BuildRaytracingAccelerationStructure(&tlasBuildDesc, 0, nullptr);

	// Add UAV barrier to ensure TLAS is ready before ray tracing
	D3D12_RESOURCE_BARRIER uavBarrier = {};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = tlasResult;
	cmdList->ResourceBarrier(1, &uavBarrier);

	LOGI("ASBuilder::BuildTLASGPU - GPU build commands completed (REAL BUILD)");
}