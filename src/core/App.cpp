#include "App.h"
#include "Camera.h"
#include "../utils/Logger.h"
#include "../utils/Env.h"
#include "../utils/DescriptorHeap.h"
#include "../dxr/ASBuilder.h"
#include "../dxr/Pipeline.h"
#include "../dxr/SBT.h"
#include "../renderer/Composite.h"
#include "../utils/FileLoader.h"
#include "../volumetric/Particles.h"
#include "../volumetric/DensityVolume.h"
#include "../volumetric/VolumeRenderer.h"
#include <d3dcompiler.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <io.h>
#include <fcntl.h>
#include <algorithm>
using Microsoft::WRL::ComPtr;

// Agility SDK exports (defined in D3D12AgilitySDK.cpp)
extern "C" {
    extern const unsigned int D3D12SDKVersion;
    extern const char* D3D12SDKPath;
}

static App* g_appInstance = nullptr;

App::App() { g_appInstance = this; std::fill(std::begin(m_frameFenceValues), std::end(m_frameFenceValues), 0ULL); }
App::~App() { cleanup(); g_appInstance = nullptr; }

LRESULT CALLBACK App::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
	if (msg == WM_SIZE && g_appInstance) {
		UINT w = LOWORD(lParam), h = HIWORD(lParam);
		if (w > 0 && h > 0) g_appInstance->onResize(w, h);
	}

	// Input toggles (DXR_0022)
	if (msg == WM_KEYDOWN && g_appInstance) {
		switch (wParam) {
		case 'P':  // Pause toggle
			{
				static bool paused = false;
				paused = !paused;
				LOGI(paused ? "Rendering PAUSED (press P to resume)" : "Rendering RESUMED");
				// TODO: Actually pause rendering when implemented
			}
			break;
		case VK_F1:  // Toggle debug layer readouts
			{
				static bool debugVerbose = false;
				debugVerbose = !debugVerbose;
				LOGI(debugVerbose ? "Debug verbose output ENABLED (F1)" : "Debug verbose output DISABLED (F1)");
				// TODO: Control debug verbosity when debug queue is working
			}
			break;
		case VK_F2:  // Save current log to timestamped file
			{
				LOGI("F2: Flushing log file");
				// The logger auto-flushes, but we can add a timestamp marker
				LOGI("=== F2 Manual Log Checkpoint ===");
			}
			break;
		case VK_F3:  // Cycle density volume presets (VOL_0002)
			if (g_appInstance->m_densityVolume) {
				g_appInstance->m_densityVolume->CyclePreset();
				g_appInstance->m_densityVolume->RecreateVolume(g_appInstance->m_device,
					g_appInstance->m_descriptorAllocator.get());
				LOGI("F3: Density volume preset changed");
			}
			break;
		case VK_F4:  // Toggle debug slice vs ray marching (VOL_0003)
			{
				static bool useRayMarching = true;
				useRayMarching = !useRayMarching;
				LOGI(useRayMarching ? "F4: Ray marching enabled" : "F4: Debug slice enabled");
				// TODO: Implement toggle in render loop
			}
			break;
		case '1':  // Decrease density scale
		case '2':  // Increase density scale
			if (g_appInstance->m_volumeRenderer) {
				auto params = g_appInstance->m_volumeRenderer->GetParams();
				float newScale = params.densityScale + (wParam == '1' ? -0.1f : 0.1f);
				newScale = max(0.1f, min(5.0f, newScale));
				g_appInstance->m_volumeRenderer->SetDensityScale(newScale);
				LOGI("Density scale changed");
			}
			break;
		case '3':  // Decrease absorption
		case '4':  // Increase absorption
			if (g_appInstance->m_volumeRenderer) {
				auto params = g_appInstance->m_volumeRenderer->GetParams();
				float newAbsorption = params.absorption + (wParam == '3' ? -0.2f : 0.2f);
				newAbsorption = max(0.1f, min(10.0f, newAbsorption));
				g_appInstance->m_volumeRenderer->SetAbsorption(newAbsorption);
				LOGI("Absorption changed");
			}
			break;
		}
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

LONG __stdcall App::UnhandledExceptionThunk(EXCEPTION_POINTERS* ex) {
	DWORD code = ex && ex->ExceptionRecord ? ex->ExceptionRecord->ExceptionCode : 0;
	char buf[256];
	std::snprintf(buf, sizeof(buf), "=== UNHANDLED EXCEPTION: 0x%08X ===", (uint32_t)code);
	LOGE(buf);

	if (ex && ex->ExceptionRecord) {
		void* addr = ex->ExceptionRecord->ExceptionAddress;
		std::snprintf(buf, sizeof(buf), "Exception address: %p", addr);
		LOGE(buf);

		// Try to identify the module
		HMODULE hMod = nullptr;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)addr, &hMod)) {
			char modName[MAX_PATH] = {};
			if (GetModuleFileNameA(hMod, modName, sizeof(modName))) {
				std::snprintf(buf, sizeof(buf), "Faulting module: %s", modName);
				LOGE(buf);
			}
		}
	}

	void* frames[16] = {};
	USHORT captured = RtlCaptureStackBackTrace(1, 16, frames, nullptr); // Skip current frame
	LOGE("Stack trace:");
	for (USHORT i = 0; i < captured; ++i) {
		char line[256];
		std::snprintf(line, sizeof(line), "  [%u] %p", (unsigned)i, frames[i]);
		LOGE(line);
	}
	LOGE("=== END EXCEPTION INFO ===");
	return EXCEPTION_CONTINUE_SEARCH;
}

bool App::initialize(HINSTANCE hInstance, int nCmdShow) {
	attachDebugConsole();

	// Install unhandled exception filter to log crash codes and a short stack
	SetUnhandledExceptionFilter(App::UnhandledExceptionThunk);

	// Behavior: control exit-on-device-removal via env var (DXR_0022 - using Env helper)
	// Default true; set PLASMADX_NO_QUIT_ON_REMOVAL=1 to keep window open
	m_quitOnRemoval = !Env::GetBool("PLASMADX_NO_QUIT_ON_REMOVAL", false);
	if (!m_quitOnRemoval) {
		LOGW("Quit-on-device-removal is DISABLED (env PLASMADX_NO_QUIT_ON_REMOVAL=1)");
	} else {
		LOGI("Quit-on-device-removal is ENABLED (default)");
	}
	LOGI("Initializing PlasmaDX with D3D12 Agility SDK...");
	// Log Agility exports if present
	{
		char buf[256];
		std::snprintf(buf, sizeof(buf), "Agility exports: D3D12SDKVersion=%u, D3D12SDKPath=%s", D3D12SDKVersion, D3D12SDKPath);
		LOGI(buf);
	}
	if (!createWindow(hInstance, nCmdShow)) {
		LOGE("Failed to create window");
		return false;
	}
	LOGI("Window creation completed, starting device creation...");
	if (!createDevice()) {
		LOGE("Failed to create D3D12 device");
		return false;
	}
	#ifndef PLASMADX_MINIMAL_BASELINE
	checkDXRSupport();
	#else
	LOGW("PLASMADX_MINIMAL_BASELINE is ON: Skipping DXR support checks and DXR initialization");
	m_dxrSupported = false;
	#endif
	if (!createSwapchain()) {
		LOGE("Failed to create swapchain");
		return false;
	}
	// Create command allocator/list and fence before any GPU work (DXR AS build uses them)
	if (!createCommandObjects()) {
		LOGE("Failed to create command objects");
		return false;
	}
	if (m_dxrSupported) {
		try {
			if (!initializeDXR()) {
				LOGW("DXR initialization failed, falling back to rasterization");
				m_dxrSupported = false;
			}
		} catch (const std::exception& e) {
			LOGE(std::string("DXR initialization threw exception: ") + e.what());
			LOGW("Falling back to rasterization");
			m_dxrSupported = false;
		}
	}
	if (!createRTVs()) {
		LOGE("Failed to create RTVs");
		return false;
	}
	LOGI("PlasmaDX initialized successfully");
	return true;
}

int App::run() {
	MSG msg{};
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			try {
				if (m_dxrSupported) {
					renderFrameDXR();
				} else {
					renderFrame();
				}
			} catch (const std::exception& e) {
				LOGE(std::string("Render error: ") + e.what());
				// Fall back to rasterization
				m_dxrSupported = false;
				renderFrame();
			}
		}
	}
	waitGPU();
	return 0;
}

void App::attachDebugConsole() {
	// Try to attach to existing console first (launched from terminal)
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		// Redirect stdout/stderr to existing console
		FILE* pCout;
		freopen_s(&pCout, "CONOUT$", "w", stdout);
		FILE* pCerr;
		freopen_s(&pCerr, "CONOUT$", "w", stderr);
		std::cout.clear();
		std::cerr.clear();
		LOGI("Debug console attached to parent process");
	} else {
		// Fall back to allocating new console if launched standalone
		AllocConsole();
		FILE* pCout;
		freopen_s(&pCout, "CONOUT$", "w", stdout);
		FILE* pCerr;
		freopen_s(&pCerr, "CONOUT$", "w", stderr);
		std::cout.clear();
		std::cerr.clear();
		LOGI("Debug console allocated");
	}
}

bool App::createWindow(HINSTANCE hInstance, int nCmdShow) {
	attachDebugConsole();

	LOGI("Starting window creation...");
	WNDCLASSW wc{};
	wc.lpfnWndProc = App::WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"PlasmaDXWindow";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

	LOGI("Registering window class...");
	RegisterClassW(&wc);

	LOGI("Creating window...");
	m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"PlasmaDX - DXR Hello Pipeline", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, m_width, m_height, nullptr, nullptr, hInstance, nullptr);
	if (!m_hwnd) {
		LOGE("CreateWindowEx failed");
		return false;
	}

	LOGI("Showing window...");
	ShowWindow(m_hwnd, nCmdShow);
	LOGI("ShowWindow completed, calling UpdateWindow...");
	UpdateWindow(m_hwnd);
	LOGI("UpdateWindow completed");
	LOGI("Window created successfully");
	return true;
}

bool App::createDevice() {
	// Device creation matrix: test combinations of Agility/Debug (DXR_0022 - using Env helper)
	bool useDebug = !Env::GetBool("PLASMADX_NO_DEBUG", false);

	LOGI("=== DEVICE CREATION MATRIX ===");
	char matrixLog[256];
	std::snprintf(matrixLog, sizeof(matrixLog), "Testing: agility=ON debug=%s",
		useDebug ? "ON" : "OFF");
	LOGI(matrixLog);

	LOGI("Creating D3D12 device...");

	if (useDebug) {
		LOGI("Enabling debug layer and DRED...");
		ComPtr<ID3D12Debug> debug;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
			debug->EnableDebugLayer();
			LOGI("D3D12 Debug Layer enabled");
		}

		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
			dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			LOGI("DRED enabled for enhanced crash diagnostics");
		}
	} else {
		LOGI("Debug layer disabled for matrix testing");
	}
	LOGI("Creating DXGI factory...");
	UINT flags = 0;
	if (useDebug) {
		flags |= DXGI_CREATE_FACTORY_DEBUG;
		LOGI("DXGI factory debug flags enabled");
	}
	HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory));
	if (FAILED(hr)) {
		LOGE("Failed to create DXGI factory: 0x" + std::to_string(static_cast<uint32_t>(hr)));
		return false;
	}
	LOGI("DXGI factory created successfully");

	LOGI("Enumerating adapters...");
	ComPtr<IDXGIAdapter1> adapter;
	for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
		DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
		// Convert wide string to string for logging
		std::wstring wname(desc.Description);
		std::string name(wname.begin(), wname.end());
		LOGI("Found adapter: " + name);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			LOGI("Skipping software adapter");
			continue;
		}
		LOGI("Attempting to create D3D12 device with adapter: " + name);
		HRESULT deviceHr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device));
		if (SUCCEEDED(deviceHr)) {
			LOGI("D3D12 device created successfully");
			setupInfoQueue();
			break;
		} else {
			LOGE("Failed to create device with adapter " + name + ": 0x" + std::to_string(static_cast<uint32_t>(deviceHr)));
		}
		adapter.Reset();
	}
	// Matrix result summary
	char result[256];
	if (m_device) {
		// Check DXR tier if device was created
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 opt5{};
		const char* tier = "unknown";
		if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opt5, sizeof(opt5)))) {
			switch (opt5.RaytracingTier) {
			case D3D12_RAYTRACING_TIER_NOT_SUPPORTED: tier = "NOT_SUPPORTED"; break;
			case D3D12_RAYTRACING_TIER_1_0: tier = "1.0"; break;
			case D3D12_RAYTRACING_TIER_1_1: tier = "1.1"; break;
			}
		}
		std::snprintf(result, sizeof(result), "MATRIX RESULT: agility=ON debug=%s → SUCCESS tier=%s",
			useDebug ? "ON" : "OFF", tier);
		LOGI(result);
	} else {
		std::snprintf(result, sizeof(result), "MATRIX RESULT: agility=ON debug=%s → FAILED",
			useDebug ? "ON" : "OFF");
		LOGE(result);
	}

	if (!m_device) {
		LOGE("Failed to create D3D12 device");
	}
	return m_device != nullptr;
}

void App::checkDXRSupport() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 opt5{};
	if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opt5, sizeof(opt5)))) {
		m_dxrTier = opt5.RaytracingTier;
		const char* tier = "UNKNOWN";
		switch (opt5.RaytracingTier) {
		case D3D12_RAYTRACING_TIER_NOT_SUPPORTED: tier = "NOT_SUPPORTED"; break;
		case D3D12_RAYTRACING_TIER_1_0: tier = "1.0"; m_dxrSupported = true; break;
		case D3D12_RAYTRACING_TIER_1_1: tier = "1.1"; m_dxrSupported = true; break;
		}
		LOGI(std::string("DXR Tier: ") + tier);
	}
}

bool App::createSwapchain() {
	D3D12_COMMAND_QUEUE_DESC qdesc{}; qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(m_device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&m_queue)))) return false;
	DXGI_SWAP_CHAIN_DESC1 scd{};
	scd.BufferCount = kBackBufferCount;
	scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.SampleDesc.Count = 1;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	scd.Width = m_width; scd.Height = m_height;
	ComPtr<IDXGISwapChain1> temp;
	if (FAILED(m_factory->CreateSwapChainForHwnd(m_queue.Get(), m_hwnd, &scd, nullptr, nullptr, &temp))) return false;
	if (FAILED(temp.As(&m_swapchain))) return false;
	m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
	return true;
}

bool App::createRTVs() {
	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
	rtvDesc.NumDescriptors = kBackBufferCount;
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	if (FAILED(m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)))) return false;
	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE start = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < kBackBufferCount; ++i) {
		if (FAILED(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backbuffers[i])))) return false;
		D3D12_CPU_DESCRIPTOR_HANDLE dst = start; dst.ptr += SIZE_T(i) * SIZE_T(m_rtvDescriptorSize);
		m_device->CreateRenderTargetView(m_backbuffers[i].Get(), nullptr, dst);
	}
	return true;
}

bool App::createCommandObjects() {
	if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAllocator)))) return false;
	if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAllocator.Get(), nullptr, IID_PPV_ARGS(&m_cmdList)))) return false;
	m_cmdList->Close();
	if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) return false;
	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	return m_fenceEvent != nullptr;
}

void App::renderFrame() {
    // Ensure GPU finished with this frame's backbuffer before reusing allocator
    waitForFrame();
    m_cmdAllocator->Reset();
	m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);

	// Update particle system if available (VOL_0001 fallback)
	if (m_particles && m_hdrTexture && m_hdrUavIndex != UINT_MAX) {
		PIX_SCOPED_EVENT(m_cmdList.Get(), "Particles Update (Fallback)");

		// Set descriptor heaps for particle update
		ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
		m_cmdList->SetDescriptorHeaps(1, heaps);

		// Ensure HDR is in UAV state for compute writes
		if (m_hdrIsInSRVForRead) {
			D3D12_RESOURCE_BARRIER toUAV{};
			toUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			toUAV.Transition.pResource = m_hdrTexture.Get();
			toUAV.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			toUAV.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			toUAV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_cmdList->ResourceBarrier(1, &toUAV);
			m_hdrIsInSRVForRead = false;
		}

		// Update particle simulation
		static float totalTimeFallback = 0.0f;
		const float deltaTime = 0.016f; // ~60 FPS delta time
		totalTimeFallback += deltaTime;

		m_particles->Update(m_cmdList.Get(), deltaTime, totalTimeFallback);

		// Particle debug write to HDR texture
		m_particles->WriteDebugPattern(m_cmdList.Get(), m_hdrTexture.Get(),
			m_descriptorAllocator->GetGPUHandle(m_hdrUavIndex), m_width, m_height);

		// Ensure ordering and transition HDR to SRV for composite sampling
		{
			D3D12_RESOURCE_BARRIER uav{}; uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; uav.UAV.pResource = m_hdrTexture.Get();
			m_cmdList->ResourceBarrier(1, &uav);
			if (!m_hdrIsInSRVForRead) {
				D3D12_RESOURCE_BARRIER toSRV{};
				toSRV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				toSRV.Transition.pResource = m_hdrTexture.Get();
				toSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				toSRV.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				toSRV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				m_cmdList->ResourceBarrier(1, &toSRV);
				m_hdrIsInSRVForRead = true;
			}
		}

		// Composite HDR to backbuffer if we have composite system
		if (m_composite) {
			// Transition backbuffer to render target
			D3D12_RESOURCE_BARRIER toRT{};
			toRT.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			toRT.Transition.pResource = m_backbuffers[m_frameIndex].Get();
			toRT.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			toRT.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
			toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_cmdList->ResourceBarrier(1, &toRT);

            // Composite HDR to backbuffer
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
			rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;
			D3D12_GPU_DESCRIPTOR_HANDLE hdrSrvHandle = m_descriptorAllocator->GetGPUHandle(m_hdrSrvIndex);

            // Set viewport and scissor to swapchain size
            D3D12_VIEWPORT viewport{}; viewport.TopLeftX = 0.0f; viewport.TopLeftY = 0.0f; viewport.Width = float(m_width); viewport.Height = float(m_height); viewport.MinDepth = 0.0f; viewport.MaxDepth = 1.0f;
            D3D12_RECT scissor{}; scissor.left = 0; scissor.top = 0; scissor.right = LONG(m_width); scissor.bottom = LONG(m_height);
            m_cmdList->RSSetViewports(1, &viewport);
            m_cmdList->RSSetScissorRects(1, &scissor);

			m_composite->Draw(m_cmdList.Get(), m_srvUavHeap.Get(), hdrSrvHandle, rtvHandle);

			// Transition backbuffer to present
			D3D12_RESOURCE_BARRIER toPresent{};
			toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			toPresent.Transition.pResource = m_backbuffers[m_frameIndex].Get();
			toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			toPresent.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
			toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_cmdList->ResourceBarrier(1, &toPresent);

			HRESULT hr = m_cmdList->Close();
			if (FAILED(hr)) {
				LOGE("Failed to close command list in renderFrame (HDR path): 0x" + std::to_string(static_cast<uint32_t>(hr)));
				dumpInfoQueueMessages();
				checkDeviceRemoved(hr);
				return;
			}

			ID3D12CommandList* lists[] = { m_cmdList.Get() };
			m_queue->ExecuteCommandLists(1, lists);
			dumpInfoQueueMessages();

			hr = m_swapchain->Present(1, 0);
			if (FAILED(hr)) {
				LOGE("Present failed in renderFrame (HDR path): 0x" + std::to_string(static_cast<uint32_t>(hr)));
				dumpInfoQueueMessages();
				checkDeviceRemoved(hr);
			}

			// Signal fence for this frame index and store value per backbuffer
			const UINT64 signalValue = ++m_fenceValue;
			m_queue->Signal(m_fence.Get(), signalValue);
			m_frameFenceValues[m_frameIndex] = signalValue;
			m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
			return;
		}
	}

	D3D12_RESOURCE_BARRIER toRT{};
	toRT.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toRT.Transition.pResource = m_backbuffers[m_frameIndex].Get();
	toRT.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	toRT.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
	toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_cmdList->ResourceBarrier(1, &toRT);

	// Clear with a gradient to show fallback mode is working
	static float time = 0.0f;
	time += 0.016f;
	float clear[4] = {
		0.5f + 0.3f * sin(time),
		0.2f + 0.2f * cos(time * 1.3f),
		0.8f + 0.2f * sin(time * 0.7f),
		1.0f
	};
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvStart; rtv.ptr += SIZE_T(m_frameIndex) * SIZE_T(m_rtvDescriptorSize);
	m_cmdList->ClearRenderTargetView(rtv, clear, 0, nullptr);

	D3D12_RESOURCE_BARRIER toPresent{};
	toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toPresent.Transition.pResource = m_backbuffers[m_frameIndex].Get();
	toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	toPresent.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
	toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_cmdList->ResourceBarrier(1, &toPresent);

	HRESULT hr = m_cmdList->Close();
	if (FAILED(hr)) {
		LOGE("Failed to close command list in renderFrame: 0x" + std::to_string(static_cast<uint32_t>(hr)));
		dumpInfoQueueMessages();
		checkDeviceRemoved(hr);
		return;
	}

	ID3D12CommandList* lists[] = { m_cmdList.Get() };
	m_queue->ExecuteCommandLists(1, lists);
	dumpInfoQueueMessages();

    hr = m_swapchain->Present(1, 0);
	if (FAILED(hr)) {
		LOGE("Present failed in renderFrame: 0x" + std::to_string(static_cast<uint32_t>(hr)));
		dumpInfoQueueMessages();
		checkDeviceRemoved(hr);
	}
    // Signal fence for this frame index and store value per backbuffer
    const UINT64 signalValue = ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), signalValue);
    m_frameFenceValues[m_frameIndex] = signalValue;
    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}

void App::waitGPU() {
	const UINT64 v = ++m_fenceValue;
	m_queue->Signal(m_fence.Get(), v);
	if (m_fence->GetCompletedValue() < v) {
		m_fence->SetEventOnCompletion(v, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
}

void App::waitForFrame() {
    // Wait for the fence value associated with the current backbuffer
    const UINT64 fenceToWait = m_frameFenceValues[m_frameIndex];
    if (fenceToWait != 0 && m_fence->GetCompletedValue() < fenceToWait) {
        m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void App::onResize(UINT w, UINT h) {
	if (w == 0 || h == 0) return;
	// Ignore early WM_SIZE before device/swapchain are ready
	if (!m_swapchain || !m_queue || !m_fence) return;
	waitGPU();
	for (UINT i = 0; i < kBackBufferCount; ++i) m_backbuffers[i].Reset();
	m_width = w; m_height = h;
    m_swapchain->ResizeBuffers(kBackBufferCount, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
	createRTVs();
    // Keep HDR texture in sync with swapchain size
    if (m_hdrTexture) {
        recreateHDRTexture();
    }
}

void App::checkDeviceRemoved(HRESULT hr) {
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG) {
		LOGE("Device removed/hung detected!");

		HRESULT reason = m_device->GetDeviceRemovedReason();
		LOGE("Device removed reason: 0x" + std::to_string(static_cast<uint32_t>(reason)));

#ifdef _DEBUG
		// Get DRED data for detailed crash information
		ComPtr<ID3D12DeviceRemovedExtendedData2> dred;
		if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&dred)))) {
			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs;
			if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs))) {
				LOGI("DRED Breadcrumbs available - check debug output");
			}

			D3D12_DRED_PAGE_FAULT_OUTPUT pageFault;
			if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFault))) {
				LOGI("DRED Page fault data available - check debug output");
			}
		}
#endif
        if (m_quitOnRemoval) {
            // Exit render loop on device removal/hang to avoid infinite logging
            PostQuitMessage(0);
        } else {
            LOGW("Quit-on-removal disabled: keeping window open for investigation.");
        }
    }
}

void App::setupInfoQueue() {
	LOGI("Setting up InfoQueue for enhanced diagnostics...");

	// Setup D3D12 InfoQueue
	if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&m_d3d12InfoQueue)))) {
		// Do not break on errors; log only to avoid RaiseException from debug layer
		m_d3d12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
		m_d3d12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
		LOGI("D3D12 InfoQueue configured - logging errors (no breaks)");
	}

	// Setup DXGI InfoQueue
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_dxgiInfoQueue)))) {
		m_dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, FALSE);
		m_dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, FALSE);
		LOGI("DXGI InfoQueue configured - logging errors (no breaks)");
	}
}

void App::dumpInfoQueueMessages() {
	// Dump D3D12 messages
	if (m_d3d12InfoQueue) {
		UINT64 numMessages = m_d3d12InfoQueue->GetNumStoredMessages();
		for (UINT64 i = 0; i < numMessages; ++i) {
			size_t messageLength = 0;
			m_d3d12InfoQueue->GetMessage(i, nullptr, &messageLength);
			if (messageLength > 0) {
				std::vector<uint8_t> buffer(messageLength);
				auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
				if (SUCCEEDED(m_d3d12InfoQueue->GetMessage(i, message, &messageLength))) {
					LOGE(std::string("D3D12: ") + message->pDescription);
				}
			}
		}
		m_d3d12InfoQueue->ClearStoredMessages();
	}

	// Dump DXGI messages
	if (m_dxgiInfoQueue) {
		UINT64 numMessages = m_dxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
		for (UINT64 i = 0; i < numMessages; ++i) {
			size_t messageLength = 0;
			m_dxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &messageLength);
			if (messageLength > 0) {
				std::vector<uint8_t> buffer(messageLength);
				auto* message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(buffer.data());
				if (SUCCEEDED(m_dxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, message, &messageLength))) {
					LOGE(std::string("DXGI: ") + message->pDescription);
				}
			}
		}
		m_dxgiInfoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);
	}
}

void App::cleanup() {
	if (m_queue && m_fence) waitGPU();
	if (m_fenceEvent) CloseHandle(m_fenceEvent), m_fenceEvent = nullptr;
#ifdef _DEBUG
	FreeConsole();
#endif
}

bool App::initializeDXR() {
	LOGI("Initializing DXR...");

	// Check if DXR is actually supported
	if (m_dxrTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
		LOGW("DXR not supported on this device");
		return false;
	}

	// DXC compilation is now handled offline during build

	// Create AS builder
	try {
		m_asBuilder = std::make_unique<ASBuilder>(m_device.Get());
	} catch (const std::exception& e) {
		LOGE(std::string("Failed to create AS builder: ") + e.what());
		return false;
	}

	// Create descriptor heap allocator (DXR_0021)
	m_descriptorAllocator = std::make_unique<DescriptorHeap>(
		m_device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		64,  // Capacity: plenty of room for growth (TLAS, HDR, volume, samplers, etc.)
		true // Shader visible
	);

	if (!m_descriptorAllocator->Initialize()) {
		LOGE("Failed to initialize descriptor heap allocator");
		return false;
	}

	// Keep reference to underlying heap for compatibility
	m_srvUavHeap = m_descriptorAllocator->GetHeap();

	// Build acceleration structures
	buildAccelerationStructures();

	// Create DXR pipeline
	createDXRPipeline();

	// Create shader binding table
	createShaderBindingTable();

	// Initialize Camera and Composite (DXR_0018 & 0019)
	m_camera = std::make_unique<Camera>();
	m_camera->Initialize(float(m_width) / float(m_height));
	m_camera->CreateConstantBuffer(m_device.Get());

	m_composite = std::make_unique<Composite>(m_device.Get());
	m_composite->Initialize();

	createHDRTexture();

	// Initialize particle system (VOL_0001)
	m_particles = std::make_unique<Particles>(m_device.Get());
	if (!m_particles->Initialize(65536)) { // 65k particles as per spec
		LOGE("Failed to initialize particle system");
		return false;
	}
	LOGI("Particle system initialized (65536 particles)");

	// Initialize density volume (VOL_0002)
	m_densityVolume = std::make_unique<DensityVolume>();
	if (!m_densityVolume->Initialize(m_device, m_descriptorAllocator.get(), DensityVolume::VolumePreset::Medium)) {
		LOGE("Failed to initialize density volume");
		return false;
	}
	LOGI("Density volume initialized");

	// Initialize volume renderer (VOL_0003)
	m_volumeRenderer = std::make_unique<VolumeRenderer>();
	if (!m_volumeRenderer->Initialize(m_device, m_descriptorAllocator.get())) {
		LOGE("Failed to initialize volume renderer");
		return false;
	}
	LOGI("Volume renderer initialized");

	LOGI("DXR initialized successfully with HDR pipeline");
	return true;
}

void App::buildAccelerationStructures() {
	LOGI("Building acceleration structures...");

	// Reset command list
	m_cmdAllocator->Reset();
	m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);

	PIX_SCOPED_EVENT(m_cmdList.Get(), "Build Acceleration Structures");

	// Build BLAS for triangle (stub)
	{
		PIX_SCOPED_EVENT(m_cmdList.Get(), "Build BLAS");
		if (!m_asBuilder->CreateTriangleBLAS(m_blasResult, m_blasScratch)) {
			LOGE("Failed to create triangle BLAS stub");
			return;
		}
	}

	// Build TLAS (stub - no instances needed for stub)
	{
		PIX_SCOPED_EVENT(m_cmdList.Get(), "Build TLAS");
		if (!m_asBuilder->BuildTLAS(m_tlasResult, m_tlasScratch, m_instanceDescs)) {
			LOGE("Failed to create TLAS stub");
			return;
		}
	}

	// Execute and wait
	m_cmdList->Close();
	ID3D12CommandList* lists[] = { m_cmdList.Get() };
	m_queue->ExecuteCommandLists(1, lists);
	waitGPU();
}

void App::createDXRPipeline() {
	LOGI("Creating DXR pipeline with offline compiled shaders...");

	// Load pre-compiled DXIL shader
	std::string shaderPath = "shaders/dxr/raytracing_lib.dxil";
	std::string errorMessage;

	if (!FileLoader::LoadDXILShader(shaderPath, m_dxrShaderBlob, errorMessage)) {
		LOGE("Failed to load DXR shader: " + errorMessage);
		LOGW("Falling back to rasterization mode");
		m_dxrSupported = false;
		return;
	}

	LOGI("DXR shader loaded successfully (" + std::to_string(m_dxrShaderBlob->GetBufferSize()) + " bytes)");

    // Create global root signature (DXR_0023): TLAS as root SRV, HDR UAV via descriptor table
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0; // u0
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[2]{};

    // TLAS SRV as root SRV (t0)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[0].Descriptor.ShaderRegister = 0;  // t0
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // HDR UAV as descriptor table (u0)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &uavRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = params;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	// Create root signature directly (stub doesn't provide utility method)
	ComPtr<ID3DBlob> serializedRootSig;
	ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob);
	if (FAILED(hr)) {
		LOGE("Failed to serialize root signature");
		return;
	}

	hr = m_device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&m_globalRootSignature));
	if (FAILED(hr)) {
		LOGE("Failed to create root signature");
		return;
	}

	// Create pipeline
	m_dxrPipeline = std::make_unique<Pipeline>(m_device.Get());

	// Add shader library
	std::vector<std::wstring> exports = { L"RayGen", L"Miss", L"ClosestHit" };
	m_dxrPipeline->AddDXILLibrary(
		m_dxrShaderBlob->GetBufferPointer(),
		m_dxrShaderBlob->GetBufferSize(),
		exports);

	// Add hit group
	m_dxrPipeline->AddHitGroup(L"HitGroup", L"ClosestHit");

	// Set shader config
	m_dxrPipeline->SetShaderConfig(sizeof(float) * 4, sizeof(float) * 2);

	// Set pipeline config
	m_dxrPipeline->SetPipelineConfig(1);

	// Set global root signature
	m_dxrPipeline->SetGlobalRootSignature(m_globalRootSignature.Get());

	// Create PSO
	LOGI("PIX: PSO Create Start");
	m_dxrPipeline->Create();
	LOGI("PIX: PSO Create Complete");
}

void App::createShaderBindingTable() {
	LOGI("Creating shader binding table...");

	// Note: SBT creation doesn't use command list, so we log it instead of using PIX events
	LOGI("PIX: SBT Build Start");

	m_sbt = std::make_unique<SBT>(m_device.Get());

	auto psoProps = m_dxrPipeline->GetPSOProperties();
	if (!psoProps) {
		LOGW("PSO properties are null; DXR pipeline incomplete. Falling back to rasterization.");
		m_dxrSupported = false;
		return;
	}

	// Raygen
	SBT::ShaderRecord raygenRecord;
	raygenRecord.shaderIdentifier = psoProps->GetShaderIdentifier(L"RayGen");
	if (!raygenRecord.shaderIdentifier) {
		LOGE("GetShaderIdentifier('RayGen') returned null; falling back to rasterization.");
		m_dxrSupported = false;
		return;
	}
	m_sbt->SetRaygenRecord(raygenRecord);

	// Miss
	SBT::ShaderRecord missRecord;
	missRecord.shaderIdentifier = psoProps->GetShaderIdentifier(L"Miss");
	if (!missRecord.shaderIdentifier) {
		LOGE("GetShaderIdentifier('Miss') returned null; falling back to rasterization.");
		m_dxrSupported = false;
		return;
	}
	m_sbt->AddMissRecord(missRecord);

	// Hit group
	SBT::ShaderRecord hitGroupRecord;
	hitGroupRecord.shaderIdentifier = psoProps->GetShaderIdentifier(L"HitGroup");
	if (!hitGroupRecord.shaderIdentifier) {
		LOGE("GetShaderIdentifier('HitGroup') returned null; falling back to rasterization.");
		m_dxrSupported = false;
		return;
	}
	m_sbt->AddHitGroupRecord(hitGroupRecord);

	// Build SBT
	m_sbt->Build();
	LOGI("PIX: SBT Build Complete");
}

void App::renderFrameDXR() {
    // Ensure GPU finished with this frame's backbuffer before reusing allocator
    waitForFrame();
    m_cmdAllocator->Reset();
	m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);

	PIX_SCOPED_EVENT(m_cmdList.Get(), "DXR Frame");

	// Check if we have HDR pipeline or fallback to direct backbuffer rendering
	bool useHDRPipeline = (m_hdrTexture != nullptr && m_composite != nullptr);

    if (useHDRPipeline) {
        bool wroteHDRThisFrame = false;
		// Update particle system (VOL_0001)
		if (m_particles) {
			PIX_SCOPED_EVENT(m_cmdList.Get(), "Particles Update");

			// Set descriptor heaps for particle update
			ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
			m_cmdList->SetDescriptorHeaps(1, heaps);

			// Update particle simulation
			static float totalTime = 0.0f;
			const float deltaTime = 0.016f; // ~60 FPS delta time
			totalTime += deltaTime;

			m_particles->Update(m_cmdList.Get(), deltaTime, totalTime);

			// VOL_0002 & VOL_0003: Fill density volume and ray march
			if (m_densityVolume && m_volumeRenderer && m_hdrUavIndex != UINT_MAX) {
				// Fill density volume with analytic field
				m_densityVolume->FillAnalytic(m_cmdList.Get(), totalTime);

				// VOL_0003: Ray march through the density volume
				m_volumeRenderer->RenderVolume(m_cmdList.Get(),
					m_densityVolume.get(),
					m_hdrTexture,
					m_descriptorAllocator->GetGPUHandle(m_hdrUavIndex),
					m_camera.get(),
					totalTime);
				wroteHDRThisFrame = true;

				// Optional: Still render debug slice with F4 key
				// (keeping old debug slice code available but not active by default)
			}

			// Particle debug write to HDR texture (disabled for now, using density slice instead)
			// if (m_hdrUavIndex != UINT_MAX) {
			//     m_particles->WriteDebugPattern(m_cmdList.Get(), m_hdrTexture.Get(),
			//         m_descriptorAllocator->GetGPUHandle(m_hdrUavIndex), m_width, m_height);
			//     wroteHDRThisFrame = true;
			// }

			// VOL_0001: Particle debug pattern is calculated (in WriteDebugPattern)
			// The HDR texture should contain some content from DXR or compute operations
			// With GPT-5's viewport/scissor fix, this should now be visible
		}

		// HDR Pipeline: Render to HDR texture then composite to backbuffer
		{
			PIX_SCOPED_EVENT(m_cmdList.Get(), "HDR Content Generation");

			// Set descriptor heaps
			ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
			m_cmdList->SetDescriptorHeaps(1, heaps);

            // APP_0003: Guard DXR dispatch behind validity checks and env override
            bool dxrDisabled = Env::GetBool("PLASMADX_DISABLE_DXR", true); // default: compute-only
            bool canDoDXR = (!dxrDisabled && m_dxrPipeline && m_dxrPipeline->GetPSO() && m_sbt && m_tlasResult);

            if (canDoDXR) {
				// DXR path: dispatch rays to HDR texture
				PIX_SCOPED_EVENT(m_cmdList.Get(), "DXR to HDR");
                auto dispatchDesc = m_sbt->GetDispatchRaysDesc(m_width, m_height);
                // Validate SBT addresses; if missing, skip DXR
                bool sbtValid =
                    dispatchDesc.RayGenerationShaderRecord.StartAddress != 0 &&
                    dispatchDesc.MissShaderTable.StartAddress != 0 &&
                    dispatchDesc.HitGroupTable.StartAddress != 0;

                if (!sbtValid) {
                    LOGW("DXR dispatch skipped: SBT addresses are not set (compute-only fallback)");
                } else {
                    m_cmdList->SetComputeRootSignature(m_globalRootSignature.Get());
                    m_cmdList->SetPipelineState1(m_dxrPipeline->GetPSO());
                    m_cmdList->SetComputeRootShaderResourceView(0, m_tlasResult->GetGPUVirtualAddress());
                    // Ensure HDR UAV is in UAV state before binding
                    if (m_hdrIsInSRVForRead) {
                        D3D12_RESOURCE_BARRIER toUAV{};
                        toUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        toUAV.Transition.pResource = m_hdrTexture.Get();
                        toUAV.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                        toUAV.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                        toUAV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        m_cmdList->ResourceBarrier(1, &toUAV);
                        m_hdrIsInSRVForRead = false;
                    }
                    // Bind HDR UAV descriptor table (u0)
                    m_cmdList->SetComputeRootDescriptorTable(1, m_descriptorAllocator->GetGPUHandle(m_hdrUavIndex));
                    m_cmdList->DispatchRays(&dispatchDesc);
                }
            } else {
				// APP_0003: Fallback path - clear HDR with time-varying color
                if (wroteHDRThisFrame) {
                    PIX_SCOPED_EVENT(m_cmdList.Get(), "HDR Compute Content - Skip Fallback Clear");
                    // Keep computed content; do not overwrite with fallback color
                } else {
                    PIX_SCOPED_EVENT(m_cmdList.Get(), "HDR Clear Fallback");

				// Compute time-varying color
				static float time = 0.0f;
				time += 0.016f; // ~60 FPS
				float r = 0.5f + 0.5f * sinf(time);
				float g = 0.5f + 0.5f * sinf(time * 1.3f);
				float b = 0.5f + 0.5f * sinf(time * 0.7f);

                    // Get CPU and GPU descriptor handles for HDR UAV
                    D3D12_CPU_DESCRIPTOR_HANDLE hdrUavCpuHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
                    hdrUavCpuHandle.ptr += m_hdrUavIndex * m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    D3D12_GPU_DESCRIPTOR_HANDLE hdrUavGpuHandle = m_descriptorAllocator->GetGPUHandle(m_hdrUavIndex);

                    // Clear HDR texture with animated color
                    FLOAT clearColor[4] = { r, g, b, 1.0f };
                    m_cmdList->ClearUnorderedAccessViewFloat(hdrUavGpuHandle, hdrUavCpuHandle,
                        m_hdrTexture.Get(), clearColor, 0, nullptr);

                    // Log the fallback (every 60 frames)
                    static int fallbackFrames = 0;
                    if (++fallbackFrames % 60 == 0) {
                        LOGI("APP_0003: HDR clear fallback frame " + std::to_string(fallbackFrames) +
                             " RGB(" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ")");
                    }
                }
			}
		}

        // Barriers: ensure ordering, then transition HDR for SRV sampling
        {
            PIX_SCOPED_EVENT(m_cmdList.Get(), "HDR UAV→SRV Barriers");
            // UAV barrier for ordering
            D3D12_RESOURCE_BARRIER uav{};
            uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uav.UAV.pResource = m_hdrTexture.Get();
            m_cmdList->ResourceBarrier(1, &uav);

            // Transition to SRV states for Composite sampling if not already
            if (!m_hdrIsInSRVForRead) {
                D3D12_RESOURCE_BARRIER toSRV{};
                toSRV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toSRV.Transition.pResource = m_hdrTexture.Get();
                toSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                toSRV.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                toSRV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                m_cmdList->ResourceBarrier(1, &toSRV);
                m_hdrIsInSRVForRead = true;
            }
        }

		// Transition backbuffer PRESENT -> RTV
		{
			PIX_SCOPED_EVENT(m_cmdList.Get(), "Backbuffer PRESENT->RTV");
			D3D12_RESOURCE_BARRIER toRTV{};
			toRTV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			toRTV.Transition.pResource = m_backbuffers[m_frameIndex].Get();
			toRTV.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			toRTV.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
			toRTV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_cmdList->ResourceBarrier(1, &toRTV);
		}

        // HDR Composite Pass
		{
			PIX_SCOPED_EVENT(m_cmdList.Get(), "HDR Composite");
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
			rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;

			D3D12_GPU_DESCRIPTOR_HANDLE hdrSrvHandle = m_descriptorAllocator->GetGPUHandle(m_hdrSrvIndex);

            // Set viewport and scissor to swapchain size
            D3D12_VIEWPORT viewport{}; viewport.TopLeftX = 0.0f; viewport.TopLeftY = 0.0f; viewport.Width = float(m_width); viewport.Height = float(m_height); viewport.MinDepth = 0.0f; viewport.MaxDepth = 1.0f;
            D3D12_RECT scissor{}; scissor.left = 0; scissor.top = 0; scissor.right = LONG(m_width); scissor.bottom = LONG(m_height);
            m_cmdList->RSSetViewports(1, &viewport);
            m_cmdList->RSSetScissorRects(1, &scissor);

			m_composite->Draw(m_cmdList.Get(), m_srvUavHeap.Get(), hdrSrvHandle, rtvHandle);
		}

		// Transition backbuffer RTV -> PRESENT
		{
			PIX_SCOPED_EVENT(m_cmdList.Get(), "Backbuffer RTV->PRESENT");
			D3D12_RESOURCE_BARRIER toPresent{};
			toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			toPresent.Transition.pResource = m_backbuffers[m_frameIndex].Get();
			toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			toPresent.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
			toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			m_cmdList->ResourceBarrier(1, &toPresent);
		}
	} else {
		// Fallback: Direct backbuffer rendering (legacy path)
		PIX_SCOPED_EVENT(m_cmdList.Get(), "DXR Direct to Backbuffer");

		// Transition backbuffer PRESENT -> UAV
		D3D12_RESOURCE_BARRIER toUAV{};
		toUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		toUAV.Transition.pResource = m_backbuffers[m_frameIndex].Get();
		toUAV.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		toUAV.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		toUAV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_cmdList->ResourceBarrier(1, &toUAV);

		// Set descriptor heaps
		ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
		m_cmdList->SetDescriptorHeaps(1, heaps);

		// Set pipeline state
		m_cmdList->SetComputeRootSignature(m_globalRootSignature.Get());
		m_cmdList->SetPipelineState1(m_dxrPipeline->GetPSO());

		// Set resources (guard: ensure TLAS and backbuffer exist)
		if (!m_tlasResult || !m_backbuffers[m_frameIndex]) {
			LOGE("DXR render: missing TLAS or backbuffer; aborting DXR frame");
			m_cmdList->Close();
			return;
		}
		m_cmdList->SetComputeRootShaderResourceView(0, m_tlasResult->GetGPUVirtualAddress());
		m_cmdList->SetComputeRootUnorderedAccessView(1, m_backbuffers[m_frameIndex]->GetGPUVirtualAddress());

		// Dispatch rays
		if (!m_sbt) {
			LOGE("DXR render: SBT is null; aborting DXR frame");
			m_cmdList->Close();
			return;
		}
		auto dispatchDesc = m_sbt->GetDispatchRaysDesc(m_width, m_height);
		m_cmdList->DispatchRays(&dispatchDesc);

		// Transition backbuffer UAV -> PRESENT
		D3D12_RESOURCE_BARRIER toPresent{};
		toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		toPresent.Transition.pResource = m_backbuffers[m_frameIndex].Get();
		toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		toPresent.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
		toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_cmdList->ResourceBarrier(1, &toPresent);
	}

	HRESULT hr = m_cmdList->Close();
	if (FAILED(hr)) {
		LOGE("Failed to close command list in renderFrameDXR: 0x" + std::to_string(static_cast<uint32_t>(hr)));
		dumpInfoQueueMessages();
		checkDeviceRemoved(hr);
		return;
	}

	ID3D12CommandList* lists[] = { m_cmdList.Get() };
	m_queue->ExecuteCommandLists(1, lists);
	dumpInfoQueueMessages();

	// Present (logging marker since Present doesn't use command lists)
	LOGI("PIX: Present Start");
    hr = m_swapchain->Present(1, 0);
	if (FAILED(hr)) {
		LOGE("Present failed in renderFrameDXR: 0x" + std::to_string(static_cast<uint32_t>(hr)));
		dumpInfoQueueMessages();
		checkDeviceRemoved(hr);
	} else {
		LOGI("PIX: Present Complete");
	}
    // Before next frame's writes, reset HDR state tracking
    m_hdrIsInSRVForRead = false; // Next frame will write HDR as UAV again

    // Signal fence for this frame index and store value per backbuffer
    const UINT64 signalValue = ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), signalValue);
    m_frameFenceValues[m_frameIndex] = signalValue;
    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}

bool App::createHDRTexture() {
    // Create HDR texture (R16G16B16A16_FLOAT) for DXR output
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr, // No optimized clear value for UAV-only textures
        IID_PPV_ARGS(&m_hdrTexture));

    if (FAILED(hr)) {
        char errorMsg[256];
        std::snprintf(errorMsg, sizeof(errorMsg), "Failed to create HDR texture: 0x%08X", (uint32_t)hr);
        LOGE(errorMsg);
        return false;
    }

    // Allocate descriptor indices via allocator (DXR_0021)
    if (m_hdrSrvIndex == UINT_MAX) {
        m_hdrSrvIndex = m_descriptorAllocator->Allocate();
        if (m_hdrSrvIndex == UINT_MAX) {
            LOGE("Failed to allocate SRV index for HDR texture");
            return false;
        }
    }

    if (m_hdrUavIndex == UINT_MAX) {
        m_hdrUavIndex = m_descriptorAllocator->Allocate();
        if (m_hdrUavIndex == UINT_MAX) {
            LOGE("Failed to allocate UAV index for HDR texture");
            return false;
        }
    }

    // Create SRV for HDR texture (for composite pass)
    if (m_descriptorAllocator) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_descriptorAllocator->GetCPUHandle(m_hdrSrvIndex);
        m_device->CreateShaderResourceView(m_hdrTexture.Get(), &srvDesc, srvHandle);

        // Create UAV for HDR texture (for DXR output)
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;

        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = m_descriptorAllocator->GetCPUHandle(m_hdrUavIndex);
        m_device->CreateUnorderedAccessView(m_hdrTexture.Get(), nullptr, &uavDesc, uavHandle);

        char allocMsg[256];
        std::snprintf(allocMsg, sizeof(allocMsg), "HDR texture descriptors allocated: SRV[%u] UAV[%u]",
                      m_hdrSrvIndex, m_hdrUavIndex);
        LOGI(allocMsg);
    }

    LOGI("HDR texture created (" + std::to_string(m_width) + "x" + std::to_string(m_height) + ")");
    return true;
}

void App::recreateHDRTexture() {
    // Release old HDR texture
    m_hdrTexture.Reset();

    // Note: We reuse the allocated descriptor indices (m_hdrSrvIndex and m_hdrUavIndex)
    // This is safe since we're just updating the descriptors at the same locations
    LOGI("Recreating HDR texture (reusing allocated descriptor indices)");

    // Recreate with new size
    createHDRTexture();
}
