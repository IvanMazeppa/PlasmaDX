#include "Pipeline.h"
#include "../utils/Logger.h"
#include <d3d12shader.h>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

Pipeline::Pipeline(ID3D12Device* device) {
	if (!device) throw std::runtime_error("Pipeline: null device");
	device->QueryInterface(IID_PPV_ARGS(&m_device));
	LOGI("Pipeline stub created");
}

void Pipeline::AddDXILLibrary(const void* /*data*/, size_t /*size*/, const std::vector<std::wstring>& /*exports*/) {
	LOGI("Pipeline::AddDXILLibrary stub called");
}

void Pipeline::AddHitGroup(const std::wstring& /*name*/, const std::wstring& /*closestHit*/) {
	LOGI("Pipeline::AddHitGroup stub called");
}

void Pipeline::SetShaderConfig(UINT /*payloadSizeBytes*/, UINT /*attribSizeBytes*/) {
	LOGI("Pipeline::SetShaderConfig stub called");
}

void Pipeline::SetPipelineConfig(UINT /*maxRecursionDepth*/) {
	LOGI("Pipeline::SetPipelineConfig stub called");
}

void Pipeline::SetGlobalRootSignature(ID3D12RootSignature* /*rs*/) {
	LOGI("Pipeline::SetGlobalRootSignature stub called");
}

void Pipeline::Create() {
	LOGW("Pipeline::Create stub - skipping state object; forcing raster fallback");
	m_pso.Reset();
	m_psoProps.Reset();
}