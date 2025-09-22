#include "VolumetricPass.h"
#include "DXCCompiler.h"
#include <d3d12.h>
#include <d3d12shader.h>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;

static const wchar_t* kComputeShaderPath = L"shaders/ComputeVolumetric.hlsl";

bool VolumetricPass::Initialize(ID3D12Device* device)
{
    // Minimal root signature: b0 CB, t0 RTAS, t1 3D density, s0 sampler, u0/u1 outputs
    CD3DX12_ROOT_PARAMETER1 params[5] = {};
    params[0].InitAsConstants(sizeof(float) * 4 * 4 / 4 +  // invViewProj 16 floats
                               4 +                         // camera pos xyz + step
                               4 +                         // light pos xyz + range
                               4 +                         // light color rgb + density scale
                               4 +                         // emission scale + sigma + maxSteps + shadowStepInterval
                               4 +                         // frameIndex + outSize + jitter + anisotropy
                               1, 0);                      // instanceMask
    params[1].InitAsShaderResourceView(0); // t0 RTAS
    params[2].InitAsShaderResourceView(1); // t1 density 3D
    params[3].InitAsDescriptorTable(1, &CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0));
    params[4].InitAsDescriptorTable(1, &CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0));

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
    rsDesc.Init_1_1(_countof(params), params, 0, nullptr,
                    D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> sig, err;
    D3D12SerializeVersionedRootSignature(&rsDesc, &sig, &err);
    device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_rootSig));

    // Compile compute shader
    ComPtr<IDxcBlob> cs;
    DXCCompileFile(kComputeShaderPath, L"CSMain", L"cs_6_5", &cs);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSig.Get();
    psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    HRESULT hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pso));
    return SUCCEEDED(hr);
}

void VolumetricPass::Resize(uint32_t width, uint32_t height)
{
    m_width = width; m_height = height;
}

void VolumetricPass::Dispatch(ID3D12GraphicsCommandList* cmd,
                              D3D12_GPU_VIRTUAL_ADDRESS tlas,
                              D3D12_GPU_DESCRIPTOR_HANDLE densitySrv,
                              D3D12_GPU_DESCRIPTOR_HANDLE outRadianceUav,
                              D3D12_GPU_DESCRIPTOR_HANDLE outHitDistUav,
                              const VolumetricParams& params,
                              const float* invViewProj4x4,
                              const float* cameraPos3,
                              const float* lightPos3,
                              const float* lightColor3,
                              float lightRange,
                              uint32_t frameIndex)
{
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetComputeRootSignature(m_rootSig.Get());

    struct CB
    {
        float invViewProj[16];
        float camPos[3]; float stepSize;
        float lightPos[3]; float lightRange;
        float lightColor[3]; float densityScale;
        float emissionScale; float sigmaExtinction; uint32_t maxSteps; uint32_t shadowStepInterval;
        uint32_t frameIndex; uint32_t outSize[2]; float jitter; float anisotropy;
        uint32_t instanceMask;
    } cb = {};
    memcpy(cb.invViewProj, invViewProj4x4, sizeof(cb.invViewProj));
    cb.camPos[0]=cameraPos3[0]; cb.camPos[1]=cameraPos3[1]; cb.camPos[2]=cameraPos3[2];
    cb.stepSize = params.stepSize;
    cb.lightPos[0]=lightPos3[0]; cb.lightPos[1]=lightPos3[1]; cb.lightPos[2]=lightPos3[2];
    cb.lightRange = lightRange;
    cb.lightColor[0]=lightColor3[0]; cb.lightColor[1]=lightColor3[1]; cb.lightColor[2]=lightColor3[2];
    cb.densityScale = params.densityScale;
    cb.emissionScale = params.emissionScale;
    cb.sigmaExtinction = params.sigmaExtinction;
    cb.maxSteps = params.maxSteps;
    cb.shadowStepInterval = params.shadowStepInterval;
    cb.frameIndex = frameIndex;
    cb.outSize[0] = m_width; cb.outSize[1] = m_height;
    cb.jitter = 0.0f;
    cb.anisotropy = params.anisotropy;
    cb.instanceMask = params.instanceMask;

    cmd->SetComputeRoot32BitConstants(0, sizeof(CB)/4, &cb, 0);
    cmd->SetComputeRootShaderResourceView(1, tlas);
    // Descriptor tables for t1 (SRV), s0 (sampler), u0/u1 (UAVs) should be set by caller via descriptor heap
    // This stub assumes densitySrv is in t1, and outRadianceUav/outHitDistUav are in a contiguous UAV table starting at u0
    // Bind UAV table
    cmd->SetComputeRootDescriptorTable(4, outRadianceUav);

    uint32_t gx = (m_width + 7) / 8;
    uint32_t gy = (m_height + 7) / 8;
    cmd->Dispatch(gx, gy, 1);
}


