#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <vector>

class ASBuilder {
public:
	explicit ASBuilder(ID3D12Device* device);

	// Create triangle BLAS with real geometry
	bool CreateTriangleBLAS(
		Microsoft::WRL::ComPtr<ID3D12Resource>& outBLAS,
		Microsoft::WRL::ComPtr<ID3D12Resource>& outScratch);

	// Build BLAS with GPU commands
	void BuildBLAS(ID3D12GraphicsCommandList* cmdList,
		ID3D12Resource* blasResult,
		ID3D12Resource* blasScratch);

	// Create and build TLAS with instance
	bool BuildTLAS(
		Microsoft::WRL::ComPtr<ID3D12Resource>& outTLAS,
		Microsoft::WRL::ComPtr<ID3D12Resource>& outScratch,
		Microsoft::WRL::ComPtr<ID3D12Resource>& outInstanceDescs);

	// Build TLAS with GPU commands
	void BuildTLASGPU(ID3D12GraphicsCommandList* cmdList,
		ID3D12Resource* tlasResult,
		ID3D12Resource* tlasScratch,
		ID3D12Resource* instanceDescs,
		ID3D12Resource* blasResult);

private:
	Microsoft::WRL::ComPtr<ID3D12Device> m_device;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_triangleVertexBuffer;  // Keep vertex buffer alive
	D3D12_RAYTRACING_GEOMETRY_DESC m_triangleGeometry = {};         // Store geometry description
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS m_blasInputs = {};  // BLAS build inputs
};