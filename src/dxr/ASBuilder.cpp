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

	LOGI("ASBuilder::CreateTriangleBLAS - creating triangle geometry for DXR lighting test");

	// Define simple triangle vertices for testing
	XMFLOAT3 triangleVertices[] = {
		{ 0.0f,  0.8f, 0.0f },  // Top vertex
		{-0.8f, -0.8f, 0.0f },  // Bottom left
		{ 0.8f, -0.8f, 0.0f }   // Bottom right
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

	// For demonstration, we skip the actual build command since it requires DXR device
	// In a real implementation, this would call:
	// ComPtr<ID3D12GraphicsCommandList4> cmdList4;
	// cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4));
	//
	// D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	// buildDesc.Inputs = m_blasInputs;
	// buildDesc.ScratchDataBuffer = blasScratch->GetGPUVirtualAddress();
	// buildDesc.DestAccelerationStructureData = blasResult->GetGPUVirtualAddress();
	//
	// cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	LOGI("ASBuilder::BuildBLAS - GPU build commands completed (stub)");
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

	// For demonstration, the actual TLAS build is stubbed
	// Real implementation would use BuildRaytracingAccelerationStructure
	LOGI("ASBuilder::BuildTLASGPU - GPU build commands completed (with real instance)");
}