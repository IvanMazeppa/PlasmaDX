#pragma once
#include <windows.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgidebug.h>
#include <cstdint>
#include <memory>

// PIX for Windows
#ifdef USE_PIX
#include <pix.h>
#define PIX_EVENT(cmdList, name) PIXBeginEvent(cmdList, PIX_COLOR_DEFAULT, name)
#define PIX_EVENT_END(cmdList) PIXEndEvent(cmdList)
#define PIX_SET_MARKER(cmdList, name) PIXSetMarker(cmdList, PIX_COLOR_DEFAULT, name)
#else
#define PIX_EVENT(cmdList, name) ((void)0)
#define PIX_EVENT_END(cmdList) ((void)0)
#define PIX_SET_MARKER(cmdList, name) ((void)0)
#endif

// RAII PIX scoped event helper
class ScopedPixEvent {
public:
    ScopedPixEvent(ID3D12GraphicsCommandList* cmdList, const char* name) : m_cmdList(cmdList) {
        PIX_EVENT(m_cmdList, name);
    }
    ~ScopedPixEvent() {
        PIX_EVENT_END(m_cmdList);
    }
private:
    ID3D12GraphicsCommandList* m_cmdList;
};

#define PIX_SCOPED_EVENT(cmdList, name) ScopedPixEvent _pix_event(cmdList, name)

class Renderer;
class ASBuilder;
class Pipeline;
class SBT;
class Composite;
class Camera;
class DescriptorHeap;
class Particles;

class App {
public:
	App();
	~App();

	bool initialize(HINSTANCE hInstance, int nCmdShow);
	int run();

	// Input handling (DXR_0019)
	void onKeyDown(UINT8 key);
	void onKeyUp(UINT8 key);
	void onMouseMove(int deltaX, int deltaY);

private:
	bool createWindow(HINSTANCE hInstance, int nCmdShow);
	bool createDevice();
	bool createSwapchain();
	bool createRTVs();
	bool createCommandObjects();
	void attachDebugConsole();
	void renderFrame();
	void waitGPU();
	void waitForFrame();
	void cleanup();
	void onResize(UINT w, UINT h);
	void checkDXRSupport();
	void checkDeviceRemoved(HRESULT hr);
	void setupInfoQueue();
	void dumpInfoQueueMessages();
	static LONG __stdcall UnhandledExceptionThunk(EXCEPTION_POINTERS* ex);

	static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
	HWND m_hwnd = nullptr;
	UINT m_width = 1280;
	UINT m_height = 720;

	Microsoft::WRL::ComPtr<IDXGIFactory7> m_factory;
	Microsoft::WRL::ComPtr<ID3D12Device5> m_device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
	Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapchain;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	UINT m_rtvDescriptorSize = 0;
	static const UINT kBackBufferCount = 2;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_backbuffers[kBackBufferCount];
	UINT m_frameIndex = 0;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_cmdAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> m_cmdList;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue = 0;
	HANDLE m_fenceEvent = nullptr;
	UINT64 m_frameFenceValues[kBackBufferCount] = {};

	Renderer* m_renderer = nullptr;

	// DXR support
	bool m_dxrSupported = false;
	D3D12_RAYTRACING_TIER m_dxrTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

	// DXR objects
	std::unique_ptr<ASBuilder> m_asBuilder;
	std::unique_ptr<Pipeline> m_dxrPipeline;
	std::unique_ptr<SBT> m_sbt;

	// DXR resources
	Microsoft::WRL::ComPtr<ID3D12Resource> m_blasResult;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_blasScratch;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_tlasResult;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_tlasScratch;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_instanceDescs;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_globalRootSignature;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;
	Microsoft::WRL::ComPtr<ID3DBlob> m_dxrShaderBlob;

	// HDR output resources (DXR_0018)
	std::unique_ptr<Composite> m_composite;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_hdrTexture;
	UINT m_hdrSrvIndex = UINT_MAX;  // Allocated via descriptor heap allocator
	UINT m_hdrUavIndex = UINT_MAX;  // Allocated via descriptor heap allocator

	// Descriptor heap allocator (DXR_0021)
	std::unique_ptr<DescriptorHeap> m_descriptorAllocator;

	// Camera system (DXR_0019)
	std::unique_ptr<Camera> m_camera;

	// Particle system (VOL_0001)
	std::unique_ptr<Particles> m_particles;

	// Debug/diagnostics interfaces
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> m_d3d12InfoQueue;
	Microsoft::WRL::ComPtr<IDXGIInfoQueue> m_dxgiInfoQueue;

	// Behavior toggles
	bool m_quitOnRemoval = true;

	// DXR methods
	bool initializeDXR();
	void createDXRPipeline();
	void buildAccelerationStructures();
	void createShaderBindingTable();
	void renderFrameDXR();

	// HDR output methods (DXR_0018)
	bool createHDRTexture();
	void recreateHDRTexture();
};
