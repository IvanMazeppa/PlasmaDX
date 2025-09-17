#include "Composite.h"
#include "../utils/Logger.h"
#include <d3dcompiler.h>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

// Simple fullscreen vertex shader
const char* g_FullscreenVS = R"(
struct VSOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID) {
    VSOutput output;
    // Generate fullscreen triangle
    output.texcoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.texcoord * 2.0f - 1.0f, 0.0f, 1.0f);
    output.position.y = -output.position.y; // Flip Y
    return output;
}
)";

// Simple copy pixel shader
const char* g_CompositePS = R"(
Texture2D<float4> hdrTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 main(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0) : SV_TARGET {
    // Simple copy for now, tonemapping can come later
    return hdrTexture.Sample(linearSampler, texcoord);
}
)";

Composite::Composite(ID3D12Device* device) {
    if (!device) throw std::runtime_error("Composite: null device");
    m_device = device;
}

bool Composite::Initialize() {
    if (!CreateRootSignature()) {
        LOGE("Composite: Failed to create root signature");
        return false;
    }

    if (!CreatePipelineState()) {
        LOGE("Composite: Failed to create pipeline state");
        return false;
    }

    LOGI("Composite: Initialized successfully");
    return true;
}

bool Composite::CreateRootSignature() {
    // Root signature: SRV(t0) for HDR texture + Sampler(s0)
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParams[2] = {};

    // Root param 0: SRV descriptor table
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static sampler (linear clamp)
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1; // Only using the SRV table
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &error);
    if (FAILED(hr)) {
        if (error) {
            LOGE("Composite: Root signature serialization error: " + std::string((char*)error->GetBufferPointer()));
        }
        return false;
    }

    hr = m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                       IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(hr)) {
        LOGE("Composite: CreateRootSignature failed");
        return false;
    }

    return true;
}

bool Composite::CreatePipelineState() {
    // Compile shaders
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> error;

    HRESULT hr = D3DCompile(g_FullscreenVS, strlen(g_FullscreenVS), "FullscreenVS", nullptr, nullptr,
                           "main", "vs_5_0", 0, 0, &vsBlob, &error);
    if (FAILED(hr)) {
        if (error) {
            LOGE("Composite: VS compilation error: " + std::string((char*)error->GetBufferPointer()));
        }
        return false;
    }

    hr = D3DCompile(g_CompositePS, strlen(g_CompositePS), "CompositePS", nullptr, nullptr,
                   "main", "ps_5_0", 0, 0, &psBlob, &error);
    if (FAILED(hr)) {
        if (error) {
            LOGE("Composite: PS compilation error: " + std::string((char*)error->GetBufferPointer()));
        }
        return false;
    }

    // Create PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // Rasterizer state
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = 0;
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 0.0f;
    psoDesc.RasterizerState.DepthClipEnable = FALSE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Blend state (no blending, direct copy)
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // No depth-stencil
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    // Formats
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.SampleMask = UINT_MAX;

    // Topology
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
    if (FAILED(hr)) {
        LOGE("Composite: CreateGraphicsPipelineState failed");
        return false;
    }

    return true;
}

void Composite::Draw(ID3D12GraphicsCommandList* cmdList,
                    ID3D12DescriptorHeap* srvHeap,
                    D3D12_GPU_DESCRIPTOR_HANDLE hdrSrvHandle,
                    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle) {

    // Set pipeline state and root signature
    cmdList->SetPipelineState(m_pipelineState.Get());
    cmdList->SetGraphicsRootSignature(m_rootSignature.Get());

    // Set descriptor heap
    ID3D12DescriptorHeap* heaps[] = { srvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    // Set HDR texture SRV
    cmdList->SetGraphicsRootDescriptorTable(0, hdrSrvHandle);

    // Set render target
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Set viewport and scissor (should be set by caller based on swapchain size)

    // Draw fullscreen triangle (3 vertices, no vertex buffer needed)
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);
}