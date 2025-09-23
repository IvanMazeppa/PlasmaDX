#pragma once
#include <wrl.h>
#include <d3d12.h>

class Composite {
public:
    explicit Composite(ID3D12Device* device);
    ~Composite() = default;

    // Initialize the composite pipeline
    bool Initialize();

    // Draw fullscreen triangle to composite HDR to backbuffer
    void Draw(ID3D12GraphicsCommandList* cmdList,
              ID3D12DescriptorHeap* srvHeap,
              D3D12_GPU_DESCRIPTOR_HANDLE hdrSrvHandle,
              D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle);

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;

    bool CreateRootSignature();
    bool CreatePipelineState();
};