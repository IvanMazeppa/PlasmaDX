#pragma once
#include <windows.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgidebug.h>
#include <cstdint>
#include <memory>

class Renderer;
class ASBuilder;
class Pipeline;
class SBT;

class App {
public:
	App();
	~App();

	bool initialize(HINSTANCE hInstance, int nCmdShow);
	int run();

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
};
