#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <vector>

class ASBuilder {
public:
	explicit ASBuilder(ID3D12Device* device);

	// Minimal stub - creates dummy buffers and returns success
	bool CreateTriangleBLAS(
		Microsoft::WRL::ComPtr<ID3D12Resource>& outBLAS,
		Microsoft::WRL::ComPtr<ID3D12Resource>& outScratch);

	bool BuildTLAS(
		Microsoft::WRL::ComPtr<ID3D12Resource>& outTLAS,
		Microsoft::WRL::ComPtr<ID3D12Resource>& outScratch,
		Microsoft::WRL::ComPtr<ID3D12Resource>& outInstanceDescs);

private:
	Microsoft::WRL::ComPtr<ID3D12Device> m_device;
};