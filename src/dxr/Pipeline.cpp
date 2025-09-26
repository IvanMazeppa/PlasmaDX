#include "Pipeline.h"
#include "../utils/Logger.h"
#include <d3d12shader.h>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

Pipeline::Pipeline(ID3D12Device* device) {
	if (!device) throw std::runtime_error("Pipeline: null device");
	device->QueryInterface(IID_PPV_ARGS(&m_device));
	LOGI("Pipeline created");
}

void Pipeline::AddDXILLibrary(const void* data, size_t size, const std::vector<std::wstring>& exports) {
	if (!data || size == 0) {
		LOGE("Pipeline::AddDXILLibrary - invalid data");
		return;
	}

	// Store DXIL data
	m_dxilData.resize(size);
	memcpy(m_dxilData.data(), data, size);
	m_exports = exports;

	LOGI("Pipeline::AddDXILLibrary - stored DXIL library with " + std::to_string(exports.size()) + " exports");
}

void Pipeline::AddHitGroup(const std::wstring& name, const std::wstring& closestHit) {
	m_hitGroupName = name;
	m_closestHitShader = closestHit;
	LOGI("Pipeline::AddHitGroup - configured hit group");
}

void Pipeline::SetShaderConfig(UINT payloadSizeBytes, UINT attribSizeBytes) {
	m_payloadSize = payloadSizeBytes;
	m_attributeSize = attribSizeBytes;
	LOGI("Pipeline::SetShaderConfig - payload:" + std::to_string(payloadSizeBytes) +
		 " attributes:" + std::to_string(attribSizeBytes));
}

void Pipeline::SetPipelineConfig(UINT maxRecursionDepth) {
	m_maxRecursionDepth = maxRecursionDepth;
	LOGI("Pipeline::SetPipelineConfig - max recursion:" + std::to_string(maxRecursionDepth));
}

void Pipeline::SetGlobalRootSignature(ID3D12RootSignature* rs) {
	m_globalRootSig = rs;
	LOGI("Pipeline::SetGlobalRootSignature - global root signature set");
}

void Pipeline::Create() {
	LOGI("Pipeline::Create - building real DXR state object...");

	try {
		// Reset any previous state
		m_pso.Reset();
		m_psoProps.Reset();

		// Validate required data
		if (m_dxilData.empty()) {
			LOGE("Pipeline::Create - no DXIL library data");
			return;
		}

		if (!m_globalRootSig) {
			LOGE("Pipeline::Create - no global root signature");
			return;
		}

		LOGI("Pipeline::Create - DXIL data size: " + std::to_string(m_dxilData.size()) + " bytes");
		LOGI("Pipeline::Create - exports count: " + std::to_string(m_exports.size()));
		for (size_t i = 0; i < m_exports.size(); i++) {
			std::wstring ws = m_exports[i];
			std::string exportName(ws.begin(), ws.end());
			LOGI("Pipeline::Create - export[" + std::to_string(i) + "]: " + exportName);
		}

		// Build subobjects array
		std::vector<D3D12_STATE_SUBOBJECT> subobjects;
		subobjects.reserve(7); // Ensure no reallocation invalidates association pointers

		// 1. DXIL Library subobject
		D3D12_DXIL_LIBRARY_DESC dxilLibDesc = {};
		dxilLibDesc.DXILLibrary.pShaderBytecode = m_dxilData.data();
		dxilLibDesc.DXILLibrary.BytecodeLength = m_dxilData.size();
		dxilLibDesc.NumExports = static_cast<UINT>(m_exports.size());

		std::vector<D3D12_EXPORT_DESC> exports(m_exports.size());
		std::vector<LPCWSTR> exportNames(m_exports.size());
		for (size_t i = 0; i < m_exports.size(); i++) {
			exports[i].Name = m_exports[i].c_str();
			exports[i].ExportToRename = nullptr;
			exports[i].Flags = D3D12_EXPORT_FLAG_NONE;
			exportNames[i] = m_exports[i].c_str();
		}
		dxilLibDesc.pExports = exports.data();

		subobjects.push_back({});
		subobjects.back().Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		subobjects.back().pDesc = &dxilLibDesc;

		// 2. Hit Group subobject (if configured)
		D3D12_HIT_GROUP_DESC hitGroupDesc = {};
		if (!m_hitGroupName.empty() && !m_closestHitShader.empty()) {
			hitGroupDesc.HitGroupExport = m_hitGroupName.c_str();
			hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
			hitGroupDesc.ClosestHitShaderImport = m_closestHitShader.c_str();

			subobjects.push_back({});
			subobjects.back().Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
			subobjects.back().pDesc = &hitGroupDesc;
		}

		// 3. Shader Config subobject
		D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
		shaderConfig.MaxPayloadSizeInBytes = m_payloadSize;
		shaderConfig.MaxAttributeSizeInBytes = m_attributeSize;

		subobjects.push_back({});
		subobjects.back().Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
		subobjects.back().pDesc = &shaderConfig;
		size_t shaderConfigIndex = subobjects.size() - 1;

		// 3a. Shader Config Association (associate all shaders with shader config)
		std::vector<LPCWSTR> shaderConfigExports;
		for (const auto& exportName : m_exports) {
			shaderConfigExports.push_back(exportName.c_str());
		}
		if (!m_hitGroupName.empty()) {
			shaderConfigExports.push_back(m_hitGroupName.c_str());
		}

		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderConfigAssoc = {};
		shaderConfigAssoc.pSubobjectToAssociate = &subobjects[shaderConfigIndex]; // Stable pointer due to reserve()
		shaderConfigAssoc.NumExports = static_cast<UINT>(shaderConfigExports.size());
		shaderConfigAssoc.pExports = shaderConfigExports.data();

		subobjects.push_back({});
		subobjects.back().Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		subobjects.back().pDesc = &shaderConfigAssoc;

		// 4. Pipeline Config subobject
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
		pipelineConfig.MaxTraceRecursionDepth = m_maxRecursionDepth;

		subobjects.push_back({});
		subobjects.back().Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
		subobjects.back().pDesc = &pipelineConfig;

		// 5. Global Root Signature subobject
		ID3D12RootSignature* pGlobalRootSig = m_globalRootSig.Get();
		subobjects.push_back({});
		subobjects.back().Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
		subobjects.back().pDesc = &pGlobalRootSig;

		// Create the state object
		D3D12_STATE_OBJECT_DESC stateObjectDesc = {};
		stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
		stateObjectDesc.NumSubobjects = static_cast<UINT>(subobjects.size());
		stateObjectDesc.pSubobjects = subobjects.data();

		HRESULT hr = m_device->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&m_pso));
		if (FAILED(hr)) {
			char errorMsg[256];
			std::snprintf(errorMsg, sizeof(errorMsg), "Pipeline::Create - CreateStateObject failed: 0x%08X", (uint32_t)hr);
			LOGE(errorMsg);
			return;
		}

		// Query state object properties
		hr = m_pso->QueryInterface(IID_PPV_ARGS(&m_psoProps));
		if (FAILED(hr)) {
			char errorMsg[256];
			std::snprintf(errorMsg, sizeof(errorMsg), "Pipeline::Create - QueryInterface for StateObjectProperties failed: 0x%08X", (uint32_t)hr);
			LOGE(errorMsg);
			return;
		}

		LOGI("Pipeline::Create - DXR state object created successfully with " + std::to_string(subobjects.size()) + " subobjects");

	} catch (const std::exception& e) {
		LOGE(std::string("Pipeline::Create - exception: ") + e.what());
		m_pso.Reset();
		m_psoProps.Reset();
	}
}