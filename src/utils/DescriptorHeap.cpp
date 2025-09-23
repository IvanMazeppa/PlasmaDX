#include "DescriptorHeap.h"
#include "Logger.h"
#include <algorithm>
#include <stdexcept>

DescriptorHeap::DescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity, bool shaderVisible)
    : m_device(device), m_type(type), m_capacity(capacity), m_shaderVisible(shaderVisible),
      m_descriptorSize(0), m_nextIndex(0), m_allocatedCount(0) {

    if (!device) {
        throw std::runtime_error("DescriptorHeap: null device");
    }

    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);

    // Log creation
    const char* typeName = "UNKNOWN";
    switch (type) {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: typeName = "CBV_SRV_UAV"; break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: typeName = "SAMPLER"; break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: typeName = "RTV"; break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: typeName = "DSV"; break;
    }

    char logBuf[256];
    std::snprintf(logBuf, sizeof(logBuf), "Creating DescriptorHeap: type=%s capacity=%u shaderVisible=%s",
                  typeName, capacity, shaderVisible ? "true" : "false");
    LOGI(logBuf);
}

bool DescriptorHeap::Initialize() {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = m_type;
    desc.NumDescriptors = m_capacity;
    desc.Flags = m_shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;

    HRESULT hr = m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr)) {
        char errorBuf[256];
        std::snprintf(errorBuf, sizeof(errorBuf), "DescriptorHeap: CreateDescriptorHeap failed: 0x%08X",
                      static_cast<uint32_t>(hr));
        LOGE(errorBuf);
        return false;
    }

    // Cache start handles
    m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
    if (m_shaderVisible) {
        m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
    }

    LOGI("DescriptorHeap initialized successfully");
    return true;
}

UINT DescriptorHeap::Allocate() {
    // Check if we have space
    if (m_allocatedCount >= m_capacity) {
        LOGE("DescriptorHeap: Allocation failed - heap is full");
        char statsBuf[256];
        std::snprintf(statsBuf, sizeof(statsBuf), "DescriptorHeap stats: allocated=%u capacity=%u",
                      m_allocatedCount, m_capacity);
        LOGE(statsBuf);
        return UINT_MAX;
    }

    UINT allocatedIndex;

    // Try to reuse from free list first
    if (!m_freeList.empty()) {
        allocatedIndex = m_freeList.back();
        m_freeList.pop_back();
        char reuseBuf[128];
        std::snprintf(reuseBuf, sizeof(reuseBuf), "DescriptorHeap: Reused index %u from free list", allocatedIndex);
        LOGI(reuseBuf);
    } else {
        // Use bump allocator
        allocatedIndex = m_nextIndex;
        m_nextIndex++;
        char bumpBuf[128];
        std::snprintf(bumpBuf, sizeof(bumpBuf), "DescriptorHeap: Allocated new index %u (bump allocator)", allocatedIndex);
        LOGI(bumpBuf);
    }

    m_allocatedCount++;

    // Log allocation statistics
    char statsBuf[256];
    std::snprintf(statsBuf, sizeof(statsBuf), "DescriptorHeap: allocated=%u free=%u capacity=%u",
                  m_allocatedCount, GetFreeCount(), m_capacity);
    LOGI(statsBuf);

    return allocatedIndex;
}

void DescriptorHeap::Free(UINT index) {
    if (index >= m_capacity) {
        char errorBuf[128];
        std::snprintf(errorBuf, sizeof(errorBuf), "DescriptorHeap: Invalid free index %u (capacity=%u)",
                      index, m_capacity);
        LOGE(errorBuf);
        return;
    }

    // Add to free list for reuse
    m_freeList.push_back(index);

    if (m_allocatedCount > 0) {
        m_allocatedCount--;
    }

    char freeBuf[128];
    std::snprintf(freeBuf, sizeof(freeBuf), "DescriptorHeap: Freed index %u (added to free list)", index);
    LOGI(freeBuf);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::GetCPUHandle(UINT index) const {
    if (index >= m_capacity) {
        char errorBuf[128];
        std::snprintf(errorBuf, sizeof(errorBuf), "DescriptorHeap: Invalid CPU handle index %u (capacity=%u)",
                      index, m_capacity);
        LOGE(errorBuf);
        // Return invalid handle
        return D3D12_CPU_DESCRIPTOR_HANDLE{ 0 };
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cpuStart;
    handle.ptr += index * m_descriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GetGPUHandle(UINT index) const {
    if (!m_shaderVisible) {
        LOGE("DescriptorHeap: Requesting GPU handle for non-shader-visible heap");
        return D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };
    }

    if (index >= m_capacity) {
        char errorBuf[128];
        std::snprintf(errorBuf, sizeof(errorBuf), "DescriptorHeap: Invalid GPU handle index %u (capacity=%u)",
                      index, m_capacity);
        LOGE(errorBuf);
        return D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };
    }

    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_gpuStart;
    handle.ptr += index * m_descriptorSize;
    return handle;
}