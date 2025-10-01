#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <vector>
#include <string>

class Pipeline {
public:
	explicit Pipeline(ID3D12Device* device);
	void AddDXILLibrary(const void* data, size_t size, const std::vector<std::wstring>& exports);
	void AddHitGroup(const std::wstring& name, const std::wstring& closestHit, const std::wstring& anyHit = L"");
	void SetShaderConfig(UINT payloadSizeBytes, UINT attribSizeBytes);
	void SetPipelineConfig(UINT maxRecursionDepth);
	void SetGlobalRootSignature(ID3D12RootSignature* rs);
	void Create();

	ID3D12StateObject* GetPSO() const { return m_pso.Get(); }
	ID3D12StateObjectProperties* GetPSOProperties() const { return m_psoProps.Get(); }

private:
	Microsoft::WRL::ComPtr<ID3D12Device5> m_device;
	Microsoft::WRL::ComPtr<ID3D12StateObject> m_pso;
	Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> m_psoProps;

	// Configuration data for state object creation
	std::vector<uint8_t> m_dxilData;
	std::vector<std::wstring> m_exports;
	std::wstring m_hitGroupName;
	std::wstring m_closestHitShader;
	std::wstring m_anyHitShader;
	UINT m_payloadSize = 32;
	UINT m_attributeSize = 8;
	UINT m_maxRecursionDepth = 1;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_globalRootSig;
};