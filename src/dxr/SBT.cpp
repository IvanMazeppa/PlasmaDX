#include "SBT.h"
#include "../utils/Logger.h"

SBT::SBT(ID3D12Device* device) : m_device(device) {
	LOGI("SBT stub created");
}

void SBT::SetRaygenRecord(const ShaderRecord& rec) {
	LOGI("SBT::SetRaygenRecord stub called");
	m_raygen = rec;
}

void SBT::AddMissRecord(const ShaderRecord& rec) {
	LOGI("SBT::AddMissRecord stub called");
	m_miss.push_back(rec);
}

void SBT::AddHitGroupRecord(const ShaderRecord& rec) {
	LOGI("SBT::AddHitGroupRecord stub called");
	m_hit.push_back(rec);
}

void SBT::Build() {
	LOGI("SBT::Build stub - no actual SBT built");
}

D3D12_DISPATCH_RAYS_DESC SBT::GetDispatchRaysDesc(UINT width, UINT height) const {
	LOGI("SBT::GetDispatchRaysDesc stub called");

	// Return a minimal dispatch rays descriptor
	D3D12_DISPATCH_RAYS_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.Depth = 1;

	// Leave all shader table addresses as 0 for stub
	// In real implementation, these would point to the SBT buffer

	return desc;
}