#include "SBT.h"
#include "../utils/Logger.h"
#include "../../include/d3dx12/d3dx12.h"
#include <algorithm>
#include <sstream>

// D3D12 raytracing alignment requirements
static const UINT D3D12_RAYTRACING_SHADER_TABLE_ALIGNMENT = 64;
static const UINT D3D12_RAYTRACING_SHADER_RECORD_ALIGNMENT = 32;

// Fixed SBT implementation with better error handling
SBT::SBT(ID3D12Device* device) : m_device(device) {
	LOGI("SBT: Creating real shader binding table");
}

void SBT::SetRaygenRecord(const ShaderRecord& rec) {
	LOGI("SBT: Setting raygen record");
	m_raygen = rec;

	if (rec.shaderIdentifier) {
		LOGI("SBT: Raygen shader identifier is valid (non-null)");
	} else {
		LOGW("SBT: Raygen shader identifier is NULL!");
	}
}

void SBT::AddMissRecord(const ShaderRecord& rec) {
	LOGI("SBT: Adding miss record");
	m_miss.push_back(rec);

	if (rec.shaderIdentifier) {
		LOGI("SBT: Miss shader identifier is valid (non-null)");
	} else {
		LOGW("SBT: Miss shader identifier is NULL!");
	}
}

void SBT::AddHitGroupRecord(const ShaderRecord& rec) {
	LOGI("SBT: Adding hit group record");
	m_hit.push_back(rec);

	if (rec.shaderIdentifier) {
		LOGI("SBT: Hit group shader identifier is valid (non-null)");
	} else {
		LOGW("SBT: Hit group shader identifier is NULL!");
	}
}

void SBT::Build() {
	LOGI("SBT: Building real GPU shader binding table");

	// Validate shader identifiers first
	if (!m_raygen.shaderIdentifier) {
		LOGE("SBT: Cannot build - raygen shader identifier is NULL");
		return;
	}

	if (m_miss.empty() || !m_miss[0].shaderIdentifier) {
		LOGE("SBT: Cannot build - miss shader identifier is NULL");
		return;
	}

	if (m_hit.empty() || !m_hit[0].shaderIdentifier) {
		LOGE("SBT: Cannot build - hit group shader identifier is NULL");
		return;
	}

	LOGI("SBT: All shader identifiers valid, proceeding with GPU allocation");

	// Calculate aligned sizes
	UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

	// For simplicity, assume no local root arguments for now
	UINT raygenRecordSize = Align(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_RECORD_ALIGNMENT);
	UINT missRecordSize = Align(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_RECORD_ALIGNMENT);
	UINT hitRecordSize = Align(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_RECORD_ALIGNMENT);

	// Calculate section sizes
	UINT raygenSectionSize = Align(raygenRecordSize, D3D12_RAYTRACING_SHADER_TABLE_ALIGNMENT);
	UINT missSectionSize = Align(missRecordSize * (UINT)m_miss.size(), D3D12_RAYTRACING_SHADER_TABLE_ALIGNMENT);
	UINT hitSectionSize = Align(hitRecordSize * (UINT)m_hit.size(), D3D12_RAYTRACING_SHADER_TABLE_ALIGNMENT);

	// Total SBT size
	UINT sbtSize = raygenSectionSize + missSectionSize + hitSectionSize;

	std::stringstream ss;
	ss << "SBT: Allocating GPU buffer of size " << sbtSize << " bytes";
	LOGI(ss.str());

	// Create upload buffer for SBT
	try {
		CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sbtSize);

		HRESULT hr = m_device->CreateCommittedResource(
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_sbtBuffer));

		if (FAILED(hr)) {
			std::stringstream err1;
			err1 << "SBT: Failed to create GPU buffer: 0x" << std::hex << hr;
			LOGE(err1.str());
			return;
		}

		LOGI("SBT: GPU buffer created successfully, mapping memory");

		// Map and write shader identifiers
		uint8_t* pData = nullptr;
		CD3DX12_RANGE readRange(0, 0); // We don't intend to read from this resource on the CPU

		hr = m_sbtBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));
		if (FAILED(hr)) {
			std::stringstream err2;
			err2 << "SBT: Failed to map buffer: 0x" << std::hex << hr;
			LOGE(err2.str());
			return;
		}

		LOGI("SBT: Memory mapped successfully, writing shader identifiers");

		// Store section info for dispatch descriptor
		m_raygenSection.StartAddress = m_sbtBuffer->GetGPUVirtualAddress();
		m_raygenSection.SizeInBytes = raygenRecordSize;

		m_missSection.StartAddress = m_raygenSection.StartAddress + raygenSectionSize;
		m_missSection.SizeInBytes = missRecordSize * (UINT)m_miss.size();
		m_missSection.StrideInBytes = missRecordSize;

		m_hitSection.StartAddress = m_missSection.StartAddress + missSectionSize;
		m_hitSection.SizeInBytes = hitRecordSize * (UINT)m_hit.size();
		m_hitSection.StrideInBytes = hitRecordSize;

		// Write raygen record
		uint8_t* currentPtr = pData;
		LOGI("SBT: Writing raygen shader identifier");
		memcpy(currentPtr, m_raygen.shaderIdentifier, shaderIdentifierSize);
		currentPtr += raygenSectionSize;

		// Write miss records
		LOGI("SBT: Writing miss shader identifiers");
		for (size_t i = 0; i < m_miss.size(); ++i) {
			memcpy(currentPtr, m_miss[i].shaderIdentifier, shaderIdentifierSize);
			currentPtr += missRecordSize;
		}
		// Align to next section
		currentPtr = pData + raygenSectionSize + missSectionSize;

		// Write hit group records
		LOGI("SBT: Writing hit group shader identifiers");
		for (size_t i = 0; i < m_hit.size(); ++i) {
			memcpy(currentPtr, m_hit[i].shaderIdentifier, shaderIdentifierSize);
			currentPtr += hitRecordSize;
		}

		LOGI("SBT: Unmapping buffer");
		m_sbtBuffer->Unmap(0, nullptr);

		std::stringstream msg;
		msg << "SBT: Build complete - GPU addresses: Raygen=0x" << std::hex << m_raygenSection.StartAddress
			<< ", Miss=0x" << m_missSection.StartAddress << ", Hit=0x" << m_hitSection.StartAddress;
		LOGI(msg.str());

	} catch (...) {
		LOGE("SBT: Exception during build - falling back to compute");
		// Clear the buffer on failure
		m_sbtBuffer.Reset();
	}
}

D3D12_DISPATCH_RAYS_DESC SBT::GetDispatchRaysDesc(UINT width, UINT height) const {
	D3D12_DISPATCH_RAYS_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.Depth = 1;

	// Only set addresses if we have a valid buffer
	if (m_sbtBuffer) {
		LOGI("SBT: Returning valid GPU addresses for dispatch");
		desc.RayGenerationShaderRecord = m_raygenSection;
		desc.MissShaderTable = m_missSection;
		desc.HitGroupTable = m_hitSection;
	} else {
		LOGI("SBT: No valid buffer - returning zeros (compute fallback)");
	}

	return desc;
}

// Helper function to align sizes
UINT SBT::Align(UINT size, UINT alignment) {
	return (size + alignment - 1) & ~(alignment - 1);
}