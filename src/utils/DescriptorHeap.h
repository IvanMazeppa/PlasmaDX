#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <vector>
#include <cstdint>

class DescriptorHeap {
public:
    explicit DescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity, bool shaderVisible = false);
    ~DescriptorHeap() = default;

    // Initialize the heap
    bool Initialize();

    // Allocate a descriptor index (returns UINT_MAX on failure)
    UINT Allocate();

    // Free a descriptor index (optional - simple bump allocator may not support this)
    void Free(UINT index);

    // Get CPU handle for an allocated index
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(UINT index) const;

    // Get GPU handle for an allocated index (only valid if shader visible)
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(UINT index) const;

    // Get the underlying heap
    ID3D12DescriptorHeap* GetHeap() const { return m_heap.Get(); }

    // Get statistics
    UINT GetCapacity() const { return m_capacity; }
    UINT GetAllocatedCount() const { return m_allocatedCount; }
    UINT GetFreeCount() const { return m_capacity - m_allocatedCount; }

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;

    D3D12_DESCRIPTOR_HEAP_TYPE m_type;
    UINT m_capacity;
    bool m_shaderVisible;
    UINT m_descriptorSize;

    // Simple bump allocator state
    UINT m_nextIndex;
    UINT m_allocatedCount;

    // Optional: Free list for more sophisticated allocation
    std::vector<UINT> m_freeList;

    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart;
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart;
};