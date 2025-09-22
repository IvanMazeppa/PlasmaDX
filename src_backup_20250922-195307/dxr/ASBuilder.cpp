#include "ASBuilder.h"
#include "../utils/Logger.h"

ASBuilder::ASBuilder(ID3D12Device* device) : m_device(device) {
	LOGI("ASBuilder stub created");
}

bool ASBuilder::CreateTriangleBLAS(
	Microsoft::WRL::ComPtr<ID3D12Resource>& outBLAS,
	Microsoft::WRL::ComPtr<ID3D12Resource>& outScratch) {

	LOGI("ASBuilder::CreateTriangleBLAS stub - allocating dummy buffers");

	// Create minimal dummy buffers to satisfy the interface
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = 1024; // Dummy size
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	HRESULT hr = m_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COMMON, nullptr,
		IID_PPV_ARGS(&outBLAS));

	if (FAILED(hr)) {
		LOGE("Failed to create dummy BLAS buffer");
		return false;
	}

	hr = m_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COMMON, nullptr,
		IID_PPV_ARGS(&outScratch));

	if (FAILED(hr)) {
		LOGE("Failed to create dummy scratch buffer");
		return false;
	}

	LOGI("ASBuilder::CreateTriangleBLAS stub completed successfully");
	return true;
}

bool ASBuilder::BuildTLAS(
	Microsoft::WRL::ComPtr<ID3D12Resource>& outTLAS,
	Microsoft::WRL::ComPtr<ID3D12Resource>& outScratch,
	Microsoft::WRL::ComPtr<ID3D12Resource>& outInstanceDescs) {

	LOGI("ASBuilder::BuildTLAS stub - allocating dummy buffers");

	// Create minimal dummy buffers
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = 1024; // Dummy size
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	HRESULT hr = m_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COMMON, nullptr,
		IID_PPV_ARGS(&outTLAS));

	if (FAILED(hr)) {
		LOGE("Failed to create dummy TLAS buffer");
		return false;
	}

	hr = m_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COMMON, nullptr,
		IID_PPV_ARGS(&outScratch));

	if (FAILED(hr)) {
		LOGE("Failed to create dummy TLAS scratch buffer");
		return false;
	}

	hr = m_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COMMON, nullptr,
		IID_PPV_ARGS(&outInstanceDescs));

	if (FAILED(hr)) {
		LOGE("Failed to create dummy instance descs buffer");
		return false;
	}

	LOGI("ASBuilder::BuildTLAS stub completed successfully");
	return true;
}