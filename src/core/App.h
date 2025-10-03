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
class DensityVolume;
class RayMarcher;
class MetaballSystem;
class ParticleSystem;

// DXR 1.2 and SER feature flags structure
struct DXRFeatures {
	// Core DXR support
	bool dxrSupported = false;
	D3D12_RAYTRACING_TIER raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

	// DXR 1.1 features
	bool inlineRaytracing = false;        // RayQuery support
	bool raytracingPipelineTracing = false;

	// DXR 1.2 features (RTX 40 series)
	bool shaderExecutionReordering = false;  // SER support
	bool opacityMicromaps = false;           // OMM support
	bool displacementMicromaps = false;      // DMM support

	// GPU Work Creation
	bool gpuWorkCreation = false;

	// Hardware capabilities
	bool isAdaLovelace = false;    // RTX 40 series detection
	bool isAmpere = false;          // RTX 30 series detection
	bool isTuring = false;          // RTX 20 series detection

	// Memory info
	uint32_t dedicatedVideoMemory = 0;  // In MB
	uint32_t l2CacheSize = 0;           // In MB (32MB for RTX 4060Ti)

	void LogFeatures() const;
};

// SER (Shader Execution Reordering) configuration
struct SERConfig {
	bool enabled = false;                // Runtime enable/disable
	uint32_t coherenceHint = 0;          // Coherence hint for SER
	uint32_t reorderingMode = 0;         // SER reordering mode

	// Performance metrics
	float lastFrameTimeMs = 0.0f;
	float serGainPercent = 0.0f;
};

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
	bool testRTVCreation();
	bool createCommandObjects();
	void attachDebugConsole();
	void renderFrame();
	void waitGPU();
	void waitForFrame();
	void cleanup();
	void onResize(UINT w, UINT h);
	void checkDXRSupport();
	void checkAdvancedDXRFeatures();
	void detectGPUArchitecture();
	void toggleSER();  // Toggle SER at runtime
	bool initializeDXR12Features();
	void checkDeviceRemoved(HRESULT hr);
	void setupInfoQueue();
	void dumpInfoQueueMessages();
	static LONG __stdcall UnhandledExceptionThunk(EXCEPTION_POINTERS* ex);

	static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
	HWND m_hwnd = nullptr;
	UINT m_width = 1920;
	UINT m_height = 1080;

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

	// DXR support and features
	DXRFeatures m_dxrFeatures;
	SERConfig m_serConfig;

	// Legacy compatibility flags (kept for backward compatibility)
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

	// Mode 9.1: Shadow map resources (DXR 1.1 RayQuery compute shader)
	Microsoft::WRL::ComPtr<ID3D12Resource> m_shadowMapTexture;
	UINT m_shadowMapSrvIndex = UINT_MAX;
	UINT m_shadowMapUavIndex = UINT_MAX;

	// RayQuery compute pipeline (replaces DispatchRays RTPSO/SBT)
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_shadowComputePSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_shadowComputeRootSignature;
	Microsoft::WRL::ComPtr<ID3DBlob> m_shadowComputeShaderBlob;

	// DXR SRV descriptors for descriptor table binding
	UINT m_tlasSrvIndex = UINT_MAX;     // TLAS SRV for descriptor table (t0)
	UINT m_densityVolumeSrvIndex = UINT_MAX;  // Density volume SRV for descriptor table (t1)
	bool m_hdrIsInSRVForRead = false; // Tracks HDR state for correct transitions

	// Descriptor heap allocator (DXR_0021)
	std::unique_ptr<DescriptorHeap> m_descriptorAllocator;

	// Camera system (DXR_0019)
	std::unique_ptr<Camera> m_camera;

    // Torchlight state
    bool m_torchOn = false;       // Torch controlled by LMB
    bool m_torchAttached = true;  // Keep for F key toggle
    float m_innerConeDeg = 12.0f;
    float m_outerConeDeg = 20.0f;
    float m_mouseX = 0.5f, m_mouseY = 0.5f; // Normalized mouse position
    int m_lightColorIndex = 0;    // For C key cycling

    // DXR additive blend over compute
    bool m_dxrBlend = false;
    float m_dxrBlendScale = 1.0f;

    // Demo mode control
    enum class DemoMode {
        SphereRT = 1,           // Pure DXR sphere baseline
        TorchlightDemo = 2,     // Interactive torch control
        VolumetricDemo = 3,     // Compact volumetric with moving elements
        VolumetricSculpture = 4, // Static complex volumetric shape with sweeping RT lighting
        PlasmaAccretion = 5,     // Orbital plasma accretion disk with volumetric self-shadowing
        VoxelParticles = 6,      // Voxel-based particle simulation (debug/development)
        MetaballSPH = 7,         // SPH metaball physics with density field rendering
        DXR12Test = 8,          // DXR 1.2 feature testing (SER, OMM, clean pipeline validation)
        AccretionMeshParticles = 9  // NASA-quality accretion disk with 100K mesh shader particles
    };
    DemoMode m_demoMode = DemoMode::SphereRT;

    // Mode 9 sub-modes: RT technique testing
    enum class Mode9SubMode {
        Baseline = 0,        // Pure mesh particles, no RT
        ShadowMap = 1,       // DXR shadow map to separate texture
        ParticleRelight = 2, // Apply shadow map to particles
        SelfShadow = 3,      // RayQuery inline self-shadowing
        OMM = 4,             // Opacity Micromap testing
        SER = 5,             // Full DXR 1.2 with SER
        Recording = 6        // Offline quality with accumulation
    };
    Mode9SubMode m_mode9SubMode = Mode9SubMode::Baseline;
    uint32_t m_mode9ParticleCount = 100000;  // Runtime adjustable (10K-500K)

    // Plasma Accretion Disk physics controls (Mode 5)
    float m_plasmaAngularVel = 0.8f;      // Angular velocity multiplier (0.1-2.0)
    float m_plasmaGravityExp = 1.5f;      // Gravity strength exponent (0.5-2.5)
    float m_plasmaDensity = 1.0f;         // Particle density multiplier (0.5-3.0)
    float m_plasmaDiskThickness = 0.8f;   // Disk thickness multiplier (0.5-2.0)
    float m_plasmaCoreTemp = 2.0f;        // Core temperature intensity
    float m_plasmaMidTemp = 1.5f;         // Mid-disk temperature intensity
    float m_plasmaEdgeTemp = 1.0f;        // Edge temperature intensity
    int m_plasmaQuality = 200;            // Ray marching steps (100-300)
    float m_plasmaOffsetX = 0.0f;         // Gravity center X offset
    float m_plasmaOffsetY = 0.0f;         // Gravity center Y offset
    float m_plasmaOffsetZ = 0.0f;         // Gravity center Z offset

    // Voxel Particle System controls (Mode 6)
    int m_voxelResolution = 64;           // Voxel grid size (32, 64, 128)
    float m_voxelWorldSize = 4.0f;        // World space size of voxel grid
    float m_voxelParticleDensity = 0.8f;  // Base particle density (0.1-2.0)
    float m_voxelTurbulence = 1.2f;       // Turbulence intensity (0.5-3.0)
    float m_voxelDissipation = 0.02f;     // Particle dissipation rate (0.01-0.1)
    float m_voxelAdvection = 1.0f;        // Velocity advection strength (0.5-2.0)
    float m_voxelTemperature = 1.5f;      // Temperature simulation intensity (0.5-3.0)
    bool m_voxelReset = false;            // Reset voxel grid flag

	// Particle system (VOL_0001)
	std::unique_ptr<Particles> m_particles;

	// Density volume (VOL_0002)
	std::unique_ptr<DensityVolume> m_densityVolume;

	// Volume renderer (VOL_0003)
	std::unique_ptr<RayMarcher> m_rayMarcher;

	// Metaballs (lava lamp)
	std::unique_ptr<MetaballSystem> m_metaballSystem;

	// Mesh shader particle system (Mode 9)
	std::unique_ptr<ParticleSystem> m_meshParticleSystem;

	// Voxel particle system (Mode 6)
	Microsoft::WRL::ComPtr<ID3D12Resource> m_voxelDensityTexture;     // 3D texture for particle density
	Microsoft::WRL::ComPtr<ID3D12Resource> m_voxelVelocityTexture;    // 3D texture for velocity field
	Microsoft::WRL::ComPtr<ID3D12Resource> m_voxelTemperatureTexture; // 3D texture for temperature
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_voxelUpdatePSO;     // Compute PSO for voxel updates
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_voxelAdvectPSO;     // Compute PSO for advection
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_voxelComputeRS;     // Root signature for voxel compute
	UINT m_voxelDensitySRVIndex = UINT_MAX;      // SRV index for density texture
	UINT m_voxelDensityUAVIndex = UINT_MAX;      // UAV index for density texture
	UINT m_voxelVelocitySRVIndex = UINT_MAX;     // SRV index for velocity texture
	UINT m_voxelVelocityUAVIndex = UINT_MAX;     // UAV index for velocity texture
	UINT m_voxelTempSRVIndex = UINT_MAX;         // SRV index for temperature texture
	UINT m_voxelTempUAVIndex = UINT_MAX;         // UAV index for temperature texture

	// Debug/diagnostics interfaces
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> m_d3d12InfoQueue;
	Microsoft::WRL::ComPtr<IDXGIInfoQueue> m_dxgiInfoQueue;

	// Behavior toggles
	bool m_quitOnRemoval = true;

	// DXR methods
	bool initializeDXRCore();
	bool initializeDXR();
	void createDXRPipeline();
	void buildAccelerationStructures();
	void createShaderBindingTable();
	void renderFrameDXR();

	// HDR output methods (DXR_0018)
	bool createHDRTexture();
	void recreateHDRTexture();

	// Mode 9.1: Shadow map methods (RayQuery compute shader)
	bool createShadowMapTexture();
	bool createShadowComputePipeline();
	void renderShadowMap();

	// Mode 9.2: Emission buffer for particle lighting
	bool createEmissionTexture();
	Microsoft::WRL::ComPtr<ID3D12Resource> m_emissionTexture;
	D3D12_CPU_DESCRIPTOR_HANDLE m_emissionRtvHandle = {};
	UINT m_emissionSrvIndex = UINT_MAX;
	bool m_showEmissionDebug = false;  // F8: Toggle emission buffer visualization

	// Mode 9.2 Milestone 2-3: Spatial grid lighting system
	bool createEmissionGridResources();
	bool createLightingComputePipelines();
	void computeEmissionGrid();
	void computeParticleLighting();

	static constexpr UINT EMISSION_GRID_RESOLUTION = 32;  // 32^3 = 32,768 cells (better spatial precision)
	Microsoft::WRL::ComPtr<ID3D12Resource> m_emissionGridBuffer;  // float4 per cell: rgb=emission, w=count
	Microsoft::WRL::ComPtr<ID3D12Resource> m_particleLightingBuffer;  // float4 per particle: rgb=light
	UINT m_emissionGridUavIndex = UINT_MAX;
	UINT m_emissionGridSrvIndex = UINT_MAX;
	UINT m_particleLightingUavIndex = UINT_MAX;
	UINT m_particleLightingSrvIndex = UINT_MAX;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_gridClearPSO;  // Clear grid to zero
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_gridClearRootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_emissionGridPSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_emissionGridRootSig;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_gridConstantsBuffer;  // CBV for grid builder shader
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_particleLightingPSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_particleLightingRootSig;

	// Voxel particle system methods (Mode 6)
	bool initializeVoxelSystem();
	bool createVoxelTextures();
	bool createVoxelComputePipelines();
	void updateVoxelSystem(float deltaTime);
	void resetVoxelGrid();
	void cleanupVoxelSystem();
};
