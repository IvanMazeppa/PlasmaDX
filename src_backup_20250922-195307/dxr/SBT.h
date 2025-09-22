#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <vector>

class SBT {
public:
	struct ShaderRecord {
		const void* shaderIdentifier = nullptr;
		std::vector<uint8_t> localRootArgs; // optional
	};

	explicit SBT(ID3D12Device* device);

	void SetRaygenRecord(const ShaderRecord& rec);
	void AddMissRecord(const ShaderRecord& rec);
	void AddHitGroupRecord(const ShaderRecord& rec);
	void Build();

	D3D12_DISPATCH_RAYS_DESC GetDispatchRaysDesc(UINT width, UINT height) const;
	ID3D12StateObjectProperties* GetPSOProperties() const { return m_psoProps.Get(); }
	void SetPSOProperties(ID3D12StateObjectProperties* props) { m_psoProps = props; }

private:
	static UINT Align(UINT size, UINT alignment);

	Microsoft::WRL::ComPtr<ID3D12Device> m_device;
	ShaderRecord m_raygen;
	std::vector<ShaderRecord> m_miss;
	std::vector<ShaderRecord> m_hit;
	Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> m_psoProps;

	// Real SBT GPU resources
	Microsoft::WRL::ComPtr<ID3D12Resource> m_sbtBuffer;
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE m_raygenSection = {};
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE m_missSection = {};
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE m_hitSection = {};
};