#include "App.h"
#include "Camera.h"
#include "../utils/Logger.h"
#include <sstream>
#include <cmath>
#include "../utils/Env.h"
#include "../utils/DescriptorHeap.h"
#include "../dxr/ASBuilder.h"
#include "../dxr/Pipeline.h"
#include "../dxr/SBT.h"
#include "../renderer/Composite.h"
#include "../utils/FileLoader.h"
#include "../volumetric/Particles.h"
#include "../volumetric/DensityVolume.h"
#include "../volumetric/RayMarcher.h"
#include "../volumetric/MetaballSystem.h"
#include "../particles/ParticleSystem.h"
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
		case VK_PAUSE:  // Pause toggle (moved off 'P' to free it for metaballs)
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
		case VK_F4:  // VOL_0003A/B: Cycle marcher debug modes (Off->RayDir->Bounds->Probe)
			{
				if (g_appInstance->m_rayMarcher) {
					g_appInstance->m_rayMarcher->CycleDebugMode();
					uint32_t mode = g_appInstance->m_rayMarcher->GetDebugMode();
					if (mode == 0) LOGI("F4: DebugMode=Off (Ray March)");
					else if (mode == 1) LOGI("F4: DebugMode=RayDir");
					else if (mode == 2) LOGI("F4: DebugMode=Bounds");
					else if (mode == 3) LOGI("F4: DebugMode=DensityProbe");
				}
			}
			break;

		case VK_F5:  // Toggle SER (Shader Execution Reordering) for DXR 1.2
			if (g_appInstance) {
				g_appInstance->toggleSER();
			}
			break;

		case VK_F7:  // Cycle Mode 9 sub-modes (RT technique testing)
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles) {
				int currentSubMode = static_cast<int>(g_appInstance->m_mode9SubMode);
				currentSubMode = (currentSubMode + 1) % 7;  // 0-6 sub-modes
				g_appInstance->m_mode9SubMode = static_cast<App::Mode9SubMode>(currentSubMode);

				const char* subModeNames[] = {
					"Baseline (No RT)",
					"Shadow Map (DXR)",
					"Particle Relight (Screen-space)",
					"Self-Shadow (RayQuery)",
					"OMM (Opacity Micromap)",
					"SER (DXR 1.2 Optimized)",
					"Recording (Offline Quality)"
				};
				LOGI(std::string("Mode 9 Sub-mode: ") + subModeNames[currentSubMode]);
			}
			break;

		case VK_F8:  // Toggle emission buffer debug view (Mode 9.2+)
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles) {
				g_appInstance->m_showEmissionDebug = !g_appInstance->m_showEmissionDebug;
				LOGI(g_appInstance->m_showEmissionDebug ? "Emission Debug: ON (showing emission buffer)" : "Emission Debug: OFF (normal rendering)");
			}
			break;

		case VK_F9:  // Mode 10: Toggle compute + traditional VS/PS rendering
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles) {
				if (g_appInstance->m_meshParticleSystem && !g_appInstance->m_meshParticleSystem->IsMode10Available()) {
					LOGW("Mode 10 is not available (PSO creation failed) - cannot enable");
				} else {
					g_appInstance->m_mode10Active = !g_appInstance->m_mode10Active;
					LOGI(g_appInstance->m_mode10Active ? "Mode 10: ON (Compute + Traditional VS/PS)" : "Mode 10: OFF (Mesh Shader)");
				}
			}
			break;

		case VK_F10:  // Mode 10: Cycle sub-modes
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_mode10Active) {
				int currentSubMode = static_cast<int>(g_appInstance->m_mode10SubMode);
				currentSubMode = (currentSubMode + 1) % 4;  // 0-3 sub-modes
				g_appInstance->m_mode10SubMode = static_cast<App::Mode10SubMode>(currentSubMode);

				const char* subModeNames[] = {
					"Baseline (No lighting)",
					"RT Lighting",
					"RT Shadows",
					"Full Pipeline"
				};
				LOGI(std::string("Mode 10 Sub-mode: ") + subModeNames[currentSubMode]);
			}
			break;

		case VK_F11:  // Mode 10: Reserved for future features
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_mode10Active) {
				LOGI("F11: Reserved for Mode 10 features");
			}
			break;

		case VK_F12:  // Mode 10: Reserved for future features
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_mode10Active) {
				LOGI("F12: Reserved for Mode 10 features");
			}
			break;

		case 'F':  // Toggle torchlight attach/detach (only in torchlight demo mode)
			if (g_appInstance && getenv("PLASMADX_TORCHLIGHT_DEMO")) {
				g_appInstance->m_torchAttached = !g_appInstance->m_torchAttached;
				LOGI(g_appInstance->m_torchAttached ? "Torch: Attached to camera" : "Torch: Detached (sweeping)");
			}
			break;

		case 'C':  // Mode 9: Cycle constraints OR Torchlight: colors OR Raymarcher: colors
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				// Mode 9: Cycle constraint shape
				g_appInstance->m_meshParticleSystem->CycleConstraintShape();
				LOGI("Constraint shape cycled");
			}
			else if (g_appInstance && getenv("PLASMADX_TORCHLIGHT_DEMO")) {
				// Torchlight demo mode: cycle colors for torchlight
				g_appInstance->m_lightColorIndex = (g_appInstance->m_lightColorIndex + 1) % 5;
				const char* colors[] = {"Warm torch", "Cool blue", "Red", "Green", "Purple"};
				std::string msg = "Light color: " + std::string(colors[g_appInstance->m_lightColorIndex]);
				LOGI(msg);
			}
			else if (g_appInstance && g_appInstance->m_rayMarcher) {
				// Normal mode: cycle ray marcher colors
				static int colorMode = 0;
				colorMode = (colorMode + 1) % 5;
				XMFLOAT3 colors[] = {
					{1.0f, 0.9f, 0.7f},  // Warm white (default)
					{0.4f, 0.7f, 1.0f},  // Cool blue
					{1.0f, 0.4f, 0.2f},  // Orange/red plasma
					{0.2f, 1.0f, 0.4f},  // Green plasma
					{0.8f, 0.2f, 1.0f}   // Purple plasma
				};
				g_appInstance->m_rayMarcher->SetLightColor(colors[colorMode]);
				LOGI("Color mode " + std::to_string(colorMode) + " selected");
			}
			break;
		case 'B':  // Mode 9: Increase angular momentum OR Toggle DXR blend
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustAngularMomentum(0.1f);
				LOGI("Angular momentum increased");
			}
			else if (g_appInstance) {
				g_appInstance->m_dxrBlend = !g_appInstance->m_dxrBlend;
				LOGI(g_appInstance->m_dxrBlend ? "DXR blend ENABLED (additive)" : "DXR blend DISABLED");
			}
			break;
		case 'N':  // Mode 9: Decrease particle size OR DXR blend scale down
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustParticleSize(-0.5f);
				LOGI("Particle size decreased");
			}
			else if (g_appInstance) {
				g_appInstance->m_dxrBlendScale = std::max(0.1f, g_appInstance->m_dxrBlendScale - 0.1f);
				LOGI("DXR blend scale: " + std::to_string(g_appInstance->m_dxrBlendScale));
			}
			break;
		case 'M':  // Mode 9: Increase particle size OR DXR blend scale up
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustParticleSize(0.5f);
				LOGI("Particle size increased");
			}
			else if (g_appInstance) {
				g_appInstance->m_dxrBlendScale = std::min(2.0f, g_appInstance->m_dxrBlendScale + 0.1f);
				LOGI("DXR blend scale: " + std::to_string(g_appInstance->m_dxrBlendScale));
			}
			break;

		case '1':  // Decrease density scale
		case '2':  // Increase density scale
			if (g_appInstance->m_rayMarcher) {
				auto params = g_appInstance->m_rayMarcher->GetParams();
				float newScale = params.densityScale + (wParam == '1' ? -0.1f : 0.1f);
				newScale = std::max(0.1f, std::min(5.0f, newScale));
				g_appInstance->m_rayMarcher->SetDensityScale(newScale);
				LOGI("Density scale changed");
			}
			break;
		case '3':  // Decrease absorption
		case '4':  // Increase absorption
			if (g_appInstance->m_rayMarcher) {
				auto params = g_appInstance->m_rayMarcher->GetParams();
				float newAbsorption = params.absorption + (wParam == '3' ? -0.2f : 0.2f);
				newAbsorption = std::max(0.1f, std::min(10.0f, newAbsorption));
				g_appInstance->m_rayMarcher->SetAbsorption(newAbsorption);
				LOGI("Absorption changed");
			}
			break;
		case '5': // Decrease anisotropy g
		case '6': // Increase anisotropy g
			if (g_appInstance->m_rayMarcher) {
				auto params = g_appInstance->m_rayMarcher->GetParams();
				float newG = params.phaseG + (wParam == '5' ? -0.05f : 0.05f);
				g_appInstance->m_rayMarcher->SetPhaseG(newG);
				LOGI("Phase g changed");
			}
			break;
		case 'P': // Increase metaball count (torchlight demo or lava lamp)
			if (g_appInstance->m_metaballSystem) {
				g_appInstance->m_metaballSystem->IncreaseCount();
			}
			break;
		case 'O': // Decrease metaball count (Shift-P alternative: O)
			if (g_appInstance->m_metaballSystem) {
				g_appInstance->m_metaballSystem->DecreaseCount();
			}
			break;
        case VK_ADD:      // + key: Increase exposure
        case VK_OEM_PLUS: // = key (also +)
			if (g_appInstance->m_rayMarcher) {
				auto params = g_appInstance->m_rayMarcher->GetParams();
				float newExposure = std::min(20.0f, params.exposure + 1.0f);
				g_appInstance->m_rayMarcher->SetExposure(newExposure);
				LOGI("Exposure: " + std::to_string(newExposure));
			}
			break;
        case VK_SUBTRACT:  // - key: Decrease exposure
        case VK_OEM_MINUS: // - key
			if (g_appInstance->m_rayMarcher) {
				auto params = g_appInstance->m_rayMarcher->GetParams();
				float newExposure = std::max(0.1f, params.exposure - 1.0f);
				g_appInstance->m_rayMarcher->SetExposure(newExposure);
				LOGI("Exposure: " + std::to_string(newExposure));
			}
			break;
		// Camera movement controls (WASD + QE)
		case 'W': case 'A': case 'S': case 'D': case 'Q': case 'E':
			if (g_appInstance && g_appInstance->m_camera) {
				// Check if Ctrl is pressed for rotation mode
				bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
				if (ctrlPressed) {
					// Ctrl+WASD: Camera rotation
					constexpr float ROTATE_SPEED = 0.05f;
					switch (wParam) {
						case 'A': g_appInstance->m_camera->RotateYaw(-ROTATE_SPEED); break;   // Turn left
						case 'D': g_appInstance->m_camera->RotateYaw(ROTATE_SPEED); break;    // Turn right
						case 'W': g_appInstance->m_camera->RotatePitch(ROTATE_SPEED); break;  // Tilt up
						case 'S': g_appInstance->m_camera->RotatePitch(-ROTATE_SPEED); break; // Tilt down
					}
				} else {
					// Normal WASD: Camera movement
					g_appInstance->m_camera->SetKeyState((char)wParam, true);
				}
			}
			break;

		// Plasma Accretion Disk controls (Mode 5)
		case VK_LEFT:  // Decrease angular velocity
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaAngularVel = std::max(0.1f, g_appInstance->m_plasmaAngularVel - 0.1f);
				LOGI("Plasma Angular Velocity: " + std::to_string(g_appInstance->m_plasmaAngularVel));
			}
			break;
		case VK_RIGHT: // Increase angular velocity
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaAngularVel = std::min(2.0f, g_appInstance->m_plasmaAngularVel + 0.1f);
				LOGI("Plasma Angular Velocity: " + std::to_string(g_appInstance->m_plasmaAngularVel));
			}
			break;
		case VK_DOWN:  // Mode 9: Decrease particle count OR Mode 5: Decrease density
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles) {
				g_appInstance->m_mode9ParticleCount = std::max(10000u, g_appInstance->m_mode9ParticleCount - 10000);
				LOGI("Mode 9 Particle Count: " + std::to_string(g_appInstance->m_mode9ParticleCount) + " (restart required)");
			}
			else if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaDensity = std::max(0.5f, g_appInstance->m_plasmaDensity - 0.1f);
				LOGI("Plasma Density: " + std::to_string(g_appInstance->m_plasmaDensity));
			}
			break;
		case VK_UP:    // Mode 9: Increase particle count OR Mode 5: Increase density
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles) {
				g_appInstance->m_mode9ParticleCount = std::min(500000u, g_appInstance->m_mode9ParticleCount + 10000);
				LOGI("Mode 9 Particle Count: " + std::to_string(g_appInstance->m_mode9ParticleCount) + " (restart required)");
			}
			else if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaDensity = std::min(3.0f, g_appInstance->m_plasmaDensity + 0.1f);
				LOGI("Plasma Density: " + std::to_string(g_appInstance->m_plasmaDensity));
			}
			break;
		case VK_OEM_4: // [ key - Mode 9: Decrease color scale OR Mode 5: Decrease gravity
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustColorTempScale(-0.1f);
				LOGI("Color temperature scale decreased");
			}
			else if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaGravityExp = std::max(0.5f, g_appInstance->m_plasmaGravityExp - 0.1f);
				LOGI("Plasma Gravity Exponent: " + std::to_string(g_appInstance->m_plasmaGravityExp));
			}
			break;
		case VK_OEM_6: // ] key - Mode 9: Increase color scale OR Mode 5: Increase gravity
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustColorTempScale(0.1f);
				LOGI("Color temperature scale increased");
			}
			else if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaGravityExp = std::min(2.5f, g_appInstance->m_plasmaGravityExp + 0.1f);
				LOGI("Plasma Gravity Exponent: " + std::to_string(g_appInstance->m_plasmaGravityExp));
			}
			break;
		case VK_PRIOR: // Page Up - Increase simulation quality
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaQuality = std::min(300, g_appInstance->m_plasmaQuality + 25);
				LOGI("Plasma Quality (ray steps): " + std::to_string(g_appInstance->m_plasmaQuality));
			}
			break;
		case VK_NEXT:  // Page Down - Decrease simulation quality
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaQuality = std::max(100, g_appInstance->m_plasmaQuality - 25);
				LOGI("Plasma Quality (ray steps): " + std::to_string(g_appInstance->m_plasmaQuality));
			}
			break;
		case '7':  // Decrease core temperature
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaCoreTemp = std::max(1.0f, g_appInstance->m_plasmaCoreTemp - 0.2f);
				LOGI("Plasma Core Temperature: " + std::to_string(g_appInstance->m_plasmaCoreTemp));
			}
			break;
		case '8':  // Increase core temperature
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaCoreTemp = std::min(5.0f, g_appInstance->m_plasmaCoreTemp + 0.2f);
				LOGI("Plasma Core Temperature: " + std::to_string(g_appInstance->m_plasmaCoreTemp));
			}
			break;
		case '9':  // Decrease disk thickness
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaDiskThickness = std::max(0.5f, g_appInstance->m_plasmaDiskThickness - 0.1f);
				LOGI("Plasma Disk Thickness: " + std::to_string(g_appInstance->m_plasmaDiskThickness));
			}
			break;
		case '0':  // Increase disk thickness
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaDiskThickness = std::min(2.0f, g_appInstance->m_plasmaDiskThickness + 0.1f);
				LOGI("Plasma Disk Thickness: " + std::to_string(g_appInstance->m_plasmaDiskThickness));
			}
			break;
		case 'R':  // Reset parameters to defaults
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::PlasmaAccretion) {
				g_appInstance->m_plasmaAngularVel = 0.8f;
				g_appInstance->m_plasmaGravityExp = 1.5f;
				g_appInstance->m_plasmaDensity = 1.0f;
				g_appInstance->m_plasmaDiskThickness = 0.8f;
				g_appInstance->m_plasmaCoreTemp = 2.0f;
				g_appInstance->m_plasmaMidTemp = 1.5f;
				g_appInstance->m_plasmaEdgeTemp = 1.0f;
				g_appInstance->m_plasmaQuality = 200;
				g_appInstance->m_plasmaOffsetX = 0.0f;
				g_appInstance->m_plasmaOffsetY = 0.0f;
				g_appInstance->m_plasmaOffsetZ = 0.0f;
				LOGI("Plasma parameters reset to defaults");
			}
			else if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->ResetParticles();
				LOGI("Particles reset!");
			}
			break;

		// MODE 9: Mesh Particle System Controls (AccretionMeshParticles)
		case 'G':  // Decrease gravity
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustGravity(-50.0f);
				LOGI("Gravity decreased");
			}
			break;
		case 'H':  // Increase gravity
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustGravity(50.0f);
				LOGI("Gravity increased");
			}
			break;
		case 'J':  // Decrease turbulence
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustTurbulence(-1.0f);
				LOGI("Turbulence decreased");
			}
			break;
		case 'K':  // Increase turbulence
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustTurbulence(1.0f);
				LOGI("Turbulence increased");
			}
			break;
		case 'Z':  // Decrease damping
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustDamping(-0.01f);
				LOGI("Damping decreased");
			}
			break;
		case 'X':  // Increase damping
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustDamping(0.01f);
				LOGI("Damping increased");
			}
			break;
		case 'V':  // Decrease angular momentum
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustAngularMomentum(-0.1f);
				LOGI("Angular momentum decreased");
			}
			break;
		case VK_OEM_COMMA:  // , key: Decrease color temperature offset
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustColorTempOffset(-100.0f);
				LOGI("Color temperature offset decreased");
			}
			break;
		case VK_OEM_PERIOD:  // . key: Increase color temperature offset
			if (g_appInstance && g_appInstance->m_demoMode == App::DemoMode::AccretionMeshParticles && g_appInstance->m_meshParticleSystem) {
				g_appInstance->m_meshParticleSystem->AdjustColorTempOffset(100.0f);
				LOGI("Color temperature offset increased");
			}
			break;
		// Note: 'C' key handled earlier with torchlight demo mode
		}
	}

	// Handle key release for camera movement
	if (msg == WM_KEYUP && g_appInstance && g_appInstance->m_camera) {
		char key = (char)wParam;
		if (key == 'W' || key == 'A' || key == 'S' || key == 'D' || key == 'Q' || key == 'E') {
			g_appInstance->m_camera->SetKeyState(key, false);
		}
	}

	// Mouse input handling with Ctrl modifier
	static bool mouseCapturing = false;
	static POINT lastMousePos = {0, 0};

	if (msg == WM_LBUTTONDOWN) {
		if (g_appInstance && getenv("PLASMADX_TORCHLIGHT_DEMO")) {
			// Torchlight mode: LMB turns torch on
			g_appInstance->m_torchOn = true;
		}
	}
	else if (msg == WM_LBUTTONUP) {
		if (g_appInstance && getenv("PLASMADX_TORCHLIGHT_DEMO")) {
			// Torchlight mode: LMB release turns torch off
			g_appInstance->m_torchOn = false;
		}
	}
	else if (msg == WM_MOUSEMOVE) {
		static int s_mouseMoveCount = 0;
		if (s_mouseMoveCount < 3) {
			LOGI("WM_MOUSEMOVE received #" + std::to_string(s_mouseMoveCount));
			s_mouseMoveCount++;
		}

		// Check if Ctrl key is pressed
		bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		if (s_mouseMoveCount < 3) {
			LOGI("Ctrl pressed: " + std::string(ctrlPressed ? "YES" : "NO"));
		}

		if (g_appInstance && getenv("PLASMADX_TORCHLIGHT_DEMO")) {
			// Torchlight mode: track mouse position for light direction
			RECT clientRect;
			GetClientRect(hWnd, &clientRect);
			POINT mousePos = { LOWORD(lParam), HIWORD(lParam) };
			g_appInstance->m_mouseX = float(mousePos.x) / float(clientRect.right);
			g_appInstance->m_mouseY = float(mousePos.y) / float(clientRect.bottom);
		}
		else if (ctrlPressed && g_appInstance && g_appInstance->m_camera) {
			// Ctrl+Mouse drag for camera control
			if (!mouseCapturing) {
				// Start capturing
				mouseCapturing = true;
				GetCursorPos(&lastMousePos);
				SetCapture(hWnd);
				ShowCursor(FALSE);
				LOGI("Ctrl+Mouse: Camera control started");
			} else {
				// Continue capturing
				POINT currentMousePos;
				GetCursorPos(&currentMousePos);

				int deltaX = currentMousePos.x - lastMousePos.x;
				int deltaY = currentMousePos.y - lastMousePos.y;

				if (deltaX != 0 || deltaY != 0) {
					static int s_mouseLogCount = 0;
					if (s_mouseLogCount < 5) {
						LOGI("Mouse move: deltaX=" + std::to_string(deltaX) + " deltaY=" + std::to_string(deltaY));
						s_mouseLogCount++;
					}
					g_appInstance->m_camera->OnMouseMove(deltaX, deltaY);
					SetCursorPos(lastMousePos.x, lastMousePos.y);
				}
			}
		}
		else if (mouseCapturing) {
			// Ctrl released - stop capturing
			mouseCapturing = false;
			ReleaseCapture();
			ShowCursor(TRUE);
			LOGI("Ctrl+Mouse: Camera control stopped");
		}
	}
	else if (msg == WM_MOUSEWHEEL && g_appInstance) {
		// Mouse wheel for zoom
		short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		if (g_appInstance->m_camera) {
			// Use W/S keys to simulate forward/backward movement for zoom
			if (wheelDelta > 0) {
				g_appInstance->m_camera->SetKeyState('W', true);
				g_appInstance->m_camera->Update(0.1f);  // Small step for smooth zoom
				g_appInstance->m_camera->SetKeyState('W', false);
			} else {
				g_appInstance->m_camera->SetKeyState('S', true);
				g_appInstance->m_camera->Update(0.1f);
				g_appInstance->m_camera->SetKeyState('S', false);
			}
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

	// Initialize DXR 1.2 features if available
	initializeDXR12Features();
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
	// Initialize DXR pipeline (confirmed working from step-by-step tests)
	if (m_dxrSupported) {
		try {
			if (!initializeDXRCore()) {
				LOGW("DXR core initialization failed, falling back to rasterization");
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
    // FPS tracking
    LARGE_INTEGER freq{}; QueryPerformanceFrequency(&freq);
    LARGE_INTEGER last{}; QueryPerformanceCounter(&last);
    double acc = 0.0; int frames = 0;
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
        // Update FPS title once per second
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        double dt = double(now.QuadPart - last.QuadPart) / double(freq.QuadPart);
        last = now; acc += dt; frames++;
        if (acc >= 1.0) {
            double fps = frames / acc;
            wchar_t title[256];

            // Mode 9: Show sub-mode and particle count
            if (m_demoMode == DemoMode::AccretionMeshParticles) {
                const wchar_t* subModeNames[] = {
                    L"Baseline", L"ShadowMap", L"Relight", L"SelfShadow", L"OMM", L"SER", L"Recording"
                };
                swprintf_s(title, L"PlasmaDX - Mode 9.%d (%s) [%.1f FPS] [%uK particles]",
                          static_cast<int>(m_mode9SubMode),
                          subModeNames[static_cast<int>(m_mode9SubMode)],
                          fps,
                          m_mode9ParticleCount / 1000);
            }
            // Include SER status in title if available
            else if (m_dxrFeatures.shaderExecutionReordering) {
                swprintf_s(title, L"PlasmaDX - DXR %s [%.1f FPS] [SER: %s]",
                          m_dxrFeatures.raytracingTier >= D3D12_RAYTRACING_TIER_1_1 ? L"1.1+" : L"1.0",
                          fps,
                          m_serConfig.enabled ? L"ON" : L"OFF");
            } else {
                swprintf_s(title, L"PlasmaDX - DXR %s [%.1f FPS]",
                          m_dxrFeatures.raytracingTier >= D3D12_RAYTRACING_TIER_1_1 ? L"1.1+" : L"1.0",
                          fps);
            }
            SetWindowTextW(m_hwnd, title);
            acc = 0.0; frames = 0;
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
	// TEMPORARY: Force debug layer ON for PSO creation diagnostics
	bool useDebug = true;  // Override: always enable debug layer for now
	//bool useDebug = !Env::GetBool("PLASMADX_NO_DEBUG", false);

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

			// GPU-based validation disabled - causes crashes with some PSOs
			// ComPtr<ID3D12Debug1> debug1;
			// if (SUCCEEDED(debug.As(&debug1))) {
			// 	debug1->SetEnableGPUBasedValidation(TRUE);
			// 	debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
			// 	LOGI("GPU-based validation enabled for PSO diagnostics");
			// }
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

void DXRFeatures::LogFeatures() const {
	LOGI("===== DXR Feature Detection Report =====");

	// Core DXR support
	LOGI(std::string("DXR Supported: ") + (dxrSupported ? "YES" : "NO"));
	if (dxrSupported) {
		std::string tierStr = "Unknown";
		switch (raytracingTier) {
		case D3D12_RAYTRACING_TIER_1_0: tierStr = "1.0"; break;
		case D3D12_RAYTRACING_TIER_1_1: tierStr = "1.1"; break;
		default:
			if (raytracingTier > D3D12_RAYTRACING_TIER_1_1) tierStr = "1.2+";
			break;
		}
		LOGI(std::string("  Raytracing Tier: ") + tierStr);
	}

	// DXR 1.1 features
	if (inlineRaytracing) {
		LOGI("  DXR 1.1 Features:");
		LOGI("    - Inline Raytracing (RayQuery): SUPPORTED");
		LOGI("    - RT Pipeline Tracing: " + std::string(raytracingPipelineTracing ? "SUPPORTED" : "NOT SUPPORTED"));
	}

	// DXR 1.2 features
	if (shaderExecutionReordering || opacityMicromaps || displacementMicromaps) {
		LOGI("  DXR 1.2 Features:");
		LOGI("    - Shader Execution Reordering (SER): " + std::string(shaderExecutionReordering ? "SUPPORTED" : "NOT SUPPORTED"));
		LOGI("    - Opacity Micromaps (OMM): " + std::string(opacityMicromaps ? "SUPPORTED" : "NOT SUPPORTED"));
		LOGI("    - Displacement Micromaps (DMM): " + std::string(displacementMicromaps ? "SUPPORTED" : "NOT SUPPORTED"));
	}

	// GPU Work Creation
	if (gpuWorkCreation) {
		LOGI("  GPU Work Creation: SUPPORTED");
	}

	// Hardware info
	LOGI("  Hardware Architecture:");
	if (isAdaLovelace) {
		LOGI("    - Ada Lovelace (RTX 40 series): DETECTED");
		LOGI("    - L2 Cache Size: " + std::to_string(l2CacheSize) + " MB");
	} else if (isAmpere) {
		LOGI("    - Ampere (RTX 30 series): DETECTED");
	} else if (isTuring) {
		LOGI("    - Turing (RTX 20 series): DETECTED");
	} else {
		LOGI("    - Unknown GPU architecture");
	}

	if (dedicatedVideoMemory > 0) {
		LOGI("    - Dedicated Video Memory: " + std::to_string(dedicatedVideoMemory) + " MB");
	}

	LOGI("=======================================");
}

void App::checkAdvancedDXRFeatures() {
	// Check OPTIONS7 for DXR 1.2 features (if available with newer SDK)
	// Note: These structures may not be available without the preview SDK
	// We'll use a try-compile approach with fallback

#ifdef D3D12_FEATURE_D3D12_OPTIONS7
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 opt7{};
	if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opt7, sizeof(opt7)))) {
		// Check for mesh shader pipeline stats (indicator of newer features)
		if (opt7.MeshShaderPipelineStatsSupported) {
			LOGI("D3D12_OPTIONS7 detected - Advanced features available");
		}
	}
#else
	LOGI("D3D12_OPTIONS7 not available - Using OPTIONS5 features only");
#endif

	// Check OPTIONS10 for SER support (DXR 1.2)
#ifdef D3D12_FEATURE_D3D12_OPTIONS10
	D3D12_FEATURE_DATA_D3D12_OPTIONS10 opt10{};
	if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS10, &opt10, sizeof(opt10)))) {
		// Mesh shader per-primitive SV_RenderTargetArrayIndex
		if (opt10.MeshShaderPerPrimitiveShadingRateSupported) {
			LOGI("D3D12_OPTIONS10 features detected");
		}
	}
#endif

	// Check OPTIONS21 for Work Graphs and advanced features
#ifdef D3D12_FEATURE_D3D12_OPTIONS21
	D3D12_FEATURE_DATA_D3D12_OPTIONS21 opt21{};
	if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS21, &opt21, sizeof(opt21)))) {
		m_dxrFeatures.gpuWorkCreation = (opt21.WorkGraphsTier != D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED);
		if (m_dxrFeatures.gpuWorkCreation) {
			LOGI("Work Graphs (GPU Work Creation) supported");
		}

		// Check for SER support via OPTIONS21
		m_dxrFeatures.shaderExecutionReordering = opt21.ExecuteIndirectTier == D3D12_EXECUTE_INDIRECT_TIER_1_1;
	}
#else
	// Fallback: Try vendor-specific detection for SER
	// SER is available on RTX 40 series (Ada Lovelace) and newer
	if (m_dxrFeatures.isAdaLovelace) {
		m_dxrFeatures.shaderExecutionReordering = true;
		LOGI("SER assumed available on Ada Lovelace architecture");
	}
#endif

	// Check for Opacity Micromaps support (part of DXR 1.2)
	// This requires checking for specific RT state object features
	if (m_dxrFeatures.raytracingTier >= D3D12_RAYTRACING_TIER_1_1) {
		// OMM and DMM are typically available with DXR 1.2
		// We'll enable these based on architecture detection for now
		if (m_dxrFeatures.isAdaLovelace) {
			m_dxrFeatures.opacityMicromaps = true;
			m_dxrFeatures.displacementMicromaps = false; // DMM support is limited
		}
	}
}

void App::detectGPUArchitecture() {
	// Get adapter description to identify GPU
	ComPtr<IDXGIAdapter1> adapter;
	if (SUCCEEDED(m_factory->EnumAdapters1(0, &adapter))) {
		DXGI_ADAPTER_DESC1 desc{};
		if (SUCCEEDED(adapter->GetDesc1(&desc))) {
			std::wstring deviceName(desc.Description);

			// Convert to string for logging
			std::string name(deviceName.begin(), deviceName.end());
			LOGI("GPU: " + name);

			// Store dedicated video memory
			m_dxrFeatures.dedicatedVideoMemory = static_cast<uint32_t>(desc.DedicatedVideoMemory / (1024 * 1024));

			// Detect NVIDIA architectures
			if (deviceName.find(L"NVIDIA") != std::wstring::npos ||
			    deviceName.find(L"GeForce") != std::wstring::npos ||
			    deviceName.find(L"RTX") != std::wstring::npos) {

				// RTX 40 series (Ada Lovelace)
				if (deviceName.find(L"RTX 40") != std::wstring::npos ||
				    deviceName.find(L"RTX 4060") != std::wstring::npos ||
				    deviceName.find(L"RTX 4070") != std::wstring::npos ||
				    deviceName.find(L"RTX 4080") != std::wstring::npos ||
				    deviceName.find(L"RTX 4090") != std::wstring::npos) {
					m_dxrFeatures.isAdaLovelace = true;
					m_dxrFeatures.l2CacheSize = 32;  // RTX 4060Ti has 32MB L2 cache
					LOGI("Ada Lovelace architecture detected (RTX 40 series)");
				}
				// RTX 30 series (Ampere)
				else if (deviceName.find(L"RTX 30") != std::wstring::npos ||
				         deviceName.find(L"RTX 3060") != std::wstring::npos ||
				         deviceName.find(L"RTX 3070") != std::wstring::npos ||
				         deviceName.find(L"RTX 3080") != std::wstring::npos ||
				         deviceName.find(L"RTX 3090") != std::wstring::npos) {
					m_dxrFeatures.isAmpere = true;
					LOGI("Ampere architecture detected (RTX 30 series)");
				}
				// RTX 20 series (Turing)
				else if (deviceName.find(L"RTX 20") != std::wstring::npos ||
				         deviceName.find(L"RTX 2060") != std::wstring::npos ||
				         deviceName.find(L"RTX 2070") != std::wstring::npos ||
				         deviceName.find(L"RTX 2080") != std::wstring::npos) {
					m_dxrFeatures.isTuring = true;
					LOGI("Turing architecture detected (RTX 20 series)");
				}
			}
			// Detect AMD architectures
			else if (deviceName.find(L"AMD") != std::wstring::npos ||
			         deviceName.find(L"Radeon") != std::wstring::npos) {
				if (deviceName.find(L"RX 7") != std::wstring::npos) {
					LOGI("AMD RDNA3 architecture detected");
				} else if (deviceName.find(L"RX 6") != std::wstring::npos) {
					LOGI("AMD RDNA2 architecture detected");
				}
			}
			// Detect Intel Arc
			else if (deviceName.find(L"Intel") != std::wstring::npos &&
			         deviceName.find(L"Arc") != std::wstring::npos) {
				LOGI("Intel Arc architecture detected");
			}
		}
	}
}

void App::toggleSER() {
	if (!m_dxrFeatures.shaderExecutionReordering) {
		LOGI("SER not supported on this GPU");
		return;
	}

	m_serConfig.enabled = !m_serConfig.enabled;
	LOGI(m_serConfig.enabled ? "SER: ENABLED" : "SER: DISABLED");

	// Update window title to show SER status
	if (m_hwnd) {
		wchar_t title[256];
		swprintf_s(title, L"PlasmaDX - DXR 1.2 [SER: %s]",
		           m_serConfig.enabled ? L"ON" : L"OFF");
		SetWindowTextW(m_hwnd, title);
	}

	// TODO: Recreate RT pipeline state with SER flags when implemented
	if (m_serConfig.enabled) {
		LOGI("SER will be applied in next RT pipeline rebuild");
	}
}

bool App::initializeDXR12Features() {
	if (!m_dxrFeatures.dxrSupported) {
		LOGE("DXR not supported - cannot initialize DXR 1.2 features");
		return false;
	}

	LOGI("Initializing DXR 1.2 features...");

	// Initialize SER if supported
	if (m_dxrFeatures.shaderExecutionReordering) {
		LOGI("Shader Execution Reordering (SER) available");

		// Check if user wants SER enabled by default
		if (Env::GetBool("PLASMADX_ENABLE_SER", false)) {
			m_serConfig.enabled = true;
			LOGI("SER enabled via environment variable");
		}

		// Set SER coherence hints for volumetric rendering
		// These hints help the GPU group similar rays together
		m_serConfig.coherenceHint = 1;  // Spatial coherence for volume rays
		m_serConfig.reorderingMode = 0;  // Default reordering mode
	}

	// Initialize OMM if supported
	if (m_dxrFeatures.opacityMicromaps) {
		LOGI("Opacity Micromaps (OMM) available for alpha-tested geometry");
	}

	// Initialize GPU Work Creation if supported
	if (m_dxrFeatures.gpuWorkCreation) {
		LOGI("GPU Work Creation available for dynamic workload generation");
	}

	return true;
}

void App::checkDXRSupport() {
	// Check basic DXR support via OPTIONS5
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 opt5{};
	if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opt5, sizeof(opt5)))) {
		m_dxrFeatures.raytracingTier = opt5.RaytracingTier;
		m_dxrTier = opt5.RaytracingTier;  // Legacy compatibility

		const char* tier = "UNKNOWN";
		switch (opt5.RaytracingTier) {
		case D3D12_RAYTRACING_TIER_NOT_SUPPORTED:
			tier = "NOT_SUPPORTED";
			break;
		case D3D12_RAYTRACING_TIER_1_0:
			tier = "1.0";
			m_dxrFeatures.dxrSupported = true;
			m_dxrSupported = true;  // Legacy compatibility
			break;
		case D3D12_RAYTRACING_TIER_1_1:
			tier = "1.1";
			m_dxrFeatures.dxrSupported = true;
			m_dxrFeatures.inlineRaytracing = true;
			m_dxrFeatures.raytracingPipelineTracing = true;
			m_dxrSupported = true;  // Legacy compatibility
			break;
		// Note: DXR 1.2 is identified via tier 1.1 + additional feature checks
		default:
			// Future tiers (placeholder for DXR 1.2+)
			if (opt5.RaytracingTier > D3D12_RAYTRACING_TIER_1_1) {
				tier = "1.2+";
				m_dxrFeatures.dxrSupported = true;
				m_dxrFeatures.inlineRaytracing = true;
				m_dxrFeatures.raytracingPipelineTracing = true;
				m_dxrSupported = true;
			}
			break;
		}
		LOGI(std::string("DXR Tier: ") + tier);
	}

	// Detect GPU architecture first (needed for SER fallback detection)
	detectGPUArchitecture();

	// Check for advanced DXR features (uses architecture info for SER detection)
	checkAdvancedDXRFeatures();

	// Log all detected features
	m_dxrFeatures.LogFeatures();
}

bool App::createSwapchain() {
	LOGI("Creating swapchain...");

	LOGI("Creating command queue...");
	D3D12_COMMAND_QUEUE_DESC qdesc{}; qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(m_device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&m_queue)))) {
		LOGE("Failed to create command queue");
		return false;
	}
	LOGI("Command queue created successfully");

	LOGI("Creating DXGI swapchain...");
	DXGI_SWAP_CHAIN_DESC1 scd{};
	scd.BufferCount = kBackBufferCount;
	scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.SampleDesc.Count = 1;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	scd.Width = m_width; scd.Height = m_height;
	ComPtr<IDXGISwapChain1> temp;
	if (FAILED(m_factory->CreateSwapChainForHwnd(m_queue.Get(), m_hwnd, &scd, nullptr, nullptr, &temp))) {
		LOGE("Failed to create DXGI swapchain");
		return false;
	}
	LOGI("DXGI swapchain created successfully");

	LOGI("Converting to IDXGISwapChain4...");
	if (FAILED(temp.As(&m_swapchain))) {
		LOGE("Failed to convert to IDXGISwapChain4");
		return false;
	}
	LOGI("Swapchain conversion successful");

	m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
	LOGI("Swapchain created successfully with frame index: " + std::to_string(m_frameIndex));
	return true;
}

// Test function to check if RTV creation would work
bool App::testRTVCreation() {
	LOGI("Testing RTV descriptor heap creation...");

	ComPtr<ID3D12DescriptorHeap> testRtvHeap;
	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
	rtvDesc.NumDescriptors = 2;
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvDesc.NodeMask = 0;

	HRESULT hr = m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&testRtvHeap));
	if (FAILED(hr)) {
		LOGE("Test RTV descriptor heap creation failed, HRESULT: 0x" + std::to_string(hr));
		return false;
	}
	LOGI("Test RTV descriptor heap creation succeeded");
	return true;
}

bool App::createRTVs() {
	LOGI("Creating RTVs...");

	// Check if we already have an RTV heap (shouldn't happen but let's be safe)
	if (m_rtvHeap) {
		LOGW("RTV heap already exists, releasing it first");
		m_rtvHeap.Reset();
	}

	// Mode 9.2: Need extra RTV for emission texture (indices 0,1=backbuffers, 2=emission)
	const UINT numRTVs = kBackBufferCount + 1;  // 2 backbuffers + 1 emission = 3
	LOGI("Creating RTV descriptor heap with " + std::to_string(numRTVs) + " descriptors");
	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
	rtvDesc.NumDescriptors = numRTVs;
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvDesc.NodeMask = 0;

	HRESULT hr = m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap));
	if (FAILED(hr)) {
		LOGE("Failed to create RTV descriptor heap, HRESULT: 0x" + std::to_string(hr));
		LOGE("Device state check - device valid: " + std::string(m_device ? "YES" : "NO"));
		LOGE("Requested descriptors: " + std::to_string(kBackBufferCount));
		return false;
	}
	LOGI("RTV descriptor heap created successfully");

	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	LOGI("RTV descriptor size: " + std::to_string(m_rtvDescriptorSize));

	D3D12_CPU_DESCRIPTOR_HANDLE start = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	LOGI("RTV heap start handle: " + std::to_string(start.ptr));

	for (UINT i = 0; i < kBackBufferCount; ++i) {
		LOGI("Getting backbuffer " + std::to_string(i) + "...");
		if (FAILED(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backbuffers[i])))) {
			LOGE("Failed to get backbuffer " + std::to_string(i));
			return false;
		}
		LOGI("Backbuffer " + std::to_string(i) + " retrieved successfully");

		D3D12_CPU_DESCRIPTOR_HANDLE dst = start;
		dst.ptr += SIZE_T(i) * SIZE_T(m_rtvDescriptorSize);
		LOGI("Creating RTV " + std::to_string(i) + " at handle: " + std::to_string(dst.ptr));
		m_device->CreateRenderTargetView(m_backbuffers[i].Get(), nullptr, dst);
		LOGI("RTV " + std::to_string(i) + " created successfully");
	}

	// Mode 9.2: Create emission texture RTV if emission texture exists
	if (m_emissionTexture) {
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = start;
		rtvHandle.ptr += SIZE_T(2) * SIZE_T(m_rtvDescriptorSize);  // Index 2 (after 2 backbuffers)
		m_emissionRtvHandle = rtvHandle;
		m_device->CreateRenderTargetView(m_emissionTexture.Get(), &rtvDesc, m_emissionRtvHandle);
		LOGI("Emission RTV created at index 2");
	}

	LOGI("All RTVs created successfully");
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

bool App::initializeDXRCore() {
	LOGI("Initializing DXR core (without volumetric systems)...");

	// Check if DXR is actually supported
	if (m_dxrTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
		LOGW("DXR not supported on this device");
		return false;
	}

	// Create AS builder
	try {
		m_asBuilder = std::make_unique<ASBuilder>(m_device.Get());
	} catch (const std::exception& e) {
		LOGE(std::string("Failed to create AS builder: ") + e.what());
		return false;
	}

	// Create descriptor heap allocator
	m_descriptorAllocator = std::make_unique<DescriptorHeap>(
		m_device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		64,  // Capacity
		true // Shader visible
	);

	if (!m_descriptorAllocator->Initialize()) {
		LOGE("Failed to initialize descriptor heap allocator");
		return false;
	}

	// Keep reference to underlying heap for compatibility
	m_srvUavHeap = m_descriptorAllocator->GetHeap();

	// NOTE: buildAccelerationStructures() moved to after particle system initialization
	// (see line ~1636 in mode 9 case) because particle BLAS requires particle buffer to exist

	// Create DXR pipeline
	createDXRPipeline();

	// Create shader binding table
	createShaderBindingTable();

	// Initialize Camera and Composite (these work fine)
	m_camera = std::make_unique<Camera>();
	m_camera->Initialize(float(m_width) / float(m_height));
	m_camera->CreateConstantBuffer(m_device.Get());

	m_composite = std::make_unique<Composite>(m_device.Get());
	m_composite->Initialize();

	createHDRTexture();

	// Demo mode selection (safe modes only - no volumetrics in initializeDXRCore)
	int debugMode = Env::GetInt("PLASMADX_DEBUG_MODE", 8); // Default to DXR12Test
	switch (debugMode) {
		case 1:
			m_demoMode = DemoMode::SphereRT;
			LOGI("Demo Mode: Sphere RT (Pure DXR baseline)");
			break;
		case 2:
			m_demoMode = DemoMode::TorchlightDemo;
			LOGI("Demo Mode: Torchlight Demo (Interactive)");
			break;
		case 8:
			m_demoMode = DemoMode::DXR12Test;
			LOGI("Demo Mode: DXR 1.2 Test");
			break;
		case 9:
		{
			// Mode 9: Mesh shader particle system (isolated, no volumetrics)
			m_demoMode = DemoMode::AccretionMeshParticles;
			const UINT particleCount = 100000;
			LOGI("Demo Mode: Accretion Mesh Particles (NASA-quality 100K particle system)");
			try {
				m_meshParticleSystem = std::make_unique<ParticleSystem>();
				if (!m_meshParticleSystem->Initialize(m_device.Get(), particleCount)) {
					LOGE("Failed to initialize mesh particle system - falling back to DXR12Test");
					m_meshParticleSystem.reset();
					m_demoMode = DemoMode::DXR12Test;
				} else {
					LOGI("Mesh particle system initialized successfully (100K particles)");

					// Mode 9.1+: Build particle BLAS for self-shadowing (MUST come before shadow map setup)
					buildAccelerationStructures();

					// Mode 9.1+: Create shadow map texture and pipeline for RT lighting
					if (!createShadowMapTexture()) {
						LOGW("Failed to create shadow map texture - Mode 9.1+ will not work");
					} else if (!createShadowComputePipeline()) {
						LOGW("Failed to create shadow compute pipeline - Mode 9.1+ will not work");
					} else {
						LOGI("Mode 9 RT lighting ready (shadow map + RayQuery compute pipeline initialized)");
					}

					// Mode 9.2+: Create emission texture for particle lighting
					if (!createEmissionTexture()) {
						LOGW("Failed to create emission texture - Mode 9.2+ will not work");
					} else {
						LOGI("Mode 9.2 emission buffer ready");
					
					// Mode 9.2 Milestone 2-3: Create spatial grid lighting resources
					if (!createEmissionGridResources()) {
						LOGW("Failed to create emission grid resources - Mode 9.2 lighting disabled");
					} else {
						LOGI("Mode 9.2 spatial grid lighting ready");
					
					// Create compute pipelines for grid building and lighting
					if (!createLightingComputePipelines()) {
						LOGW("Failed to create lighting compute pipelines - Mode 9.2 lighting disabled");
					} else {
						LOGI("Mode 9.2 lighting compute pipelines ready");
					}

					// Create particle buffer SRV for lighting compute shader
					UINT particleSrvIndex = m_descriptorAllocator->Allocate();
					if (particleSrvIndex != UINT_MAX) {
						D3D12_CPU_DESCRIPTOR_HANDLE particleSrvHandle = m_descriptorAllocator->GetCPUHandle(particleSrvIndex);
						if (m_meshParticleSystem->CreateParticleBufferSRV(m_device.Get(), particleSrvHandle)) {
							m_meshParticleSystem->SetParticleBufferSRVIndex(particleSrvIndex);
							LOGI("Particle buffer SRV created at index " + std::to_string(particleSrvIndex));
						}
					}

					// Mode 9.2 RT: Create per-particle BLAS and RT lighting pipeline
					if (!createPerParticleBLASResources()) {
						LOGW("Failed to create per-particle BLAS - Mode 9.2 RT lighting disabled");
					} else {
						LOGI("Mode 9.2 per-particle BLAS ready");

						if (!createRTLightingPipeline()) {
							LOGW("Failed to create RT lighting pipeline - Mode 9.2 RT disabled");
						} else {
							LOGI("Mode 9.2 RT lighting pipeline ready");
						}
					}
					}  // End else for createEmissionGridResources
					}  // End else for createEmissionTexture
				}  // End else for mesh particle system initialization
			} catch (const std::exception& e) {
				LOGE(std::string("Mesh particle system initialization threw exception: ") + e.what());
				m_meshParticleSystem.reset();
				m_demoMode = DemoMode::DXR12Test;
			}
		}
			break;
		default:
			m_demoMode = DemoMode::DXR12Test;
			LOGI("Demo Mode: Default DXR 1.2 Test");
			break;
	}

	// Create SRV descriptors for DXR descriptor table binding
	if (m_tlasSrvIndex == UINT_MAX) {
		m_tlasSrvIndex = m_descriptorAllocator->Allocate();
		if (m_tlasSrvIndex == UINT_MAX) {
			LOGE("Failed to allocate SRV index for TLAS");
			return false;
		}
	}

	// Create TLAS SRV (for descriptor table binding)
	D3D12_SHADER_RESOURCE_VIEW_DESC tlasSrvDesc = {};
	tlasSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	tlasSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	tlasSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	tlasSrvDesc.RaytracingAccelerationStructure.Location = m_tlasResult->GetGPUVirtualAddress();

	D3D12_CPU_DESCRIPTOR_HANDLE tlasSrvHandle = m_descriptorAllocator->GetCPUHandle(m_tlasSrvIndex);
	m_device->CreateShaderResourceView(nullptr, &tlasSrvDesc, tlasSrvHandle);

	LOGI("DXR core initialized successfully - ready for DXR 1.2 testing");
	return true;
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

	// Initialize ray marcher (VOL_0003)
	m_rayMarcher = std::make_unique<RayMarcher>();
	if (!m_rayMarcher->Initialize(m_device, m_descriptorAllocator.get())) {
		LOGE("Failed to initialize ray marcher");
		return false;
	}
	LOGI("Ray marcher initialized");

	// Initialize metaball lava lamp system
	m_metaballSystem = std::make_unique<MetaballSystem>();
	if (!m_metaballSystem->Initialize(m_device, m_descriptorAllocator.get())) {
		LOGE("Failed to initialize metaball system");
		return false;
	}
	LOGI("Metaball lava lamp system initialized");

	// Initialize demo mode from environment variable
	int debugMode = Env::GetInt("PLASMADX_DEBUG_MODE", 1); // Default to Sphere RT
	switch (debugMode) {
		case 1: m_demoMode = DemoMode::SphereRT; LOGI("Demo Mode: Sphere RT (Pure DXR baseline)"); break;
		case 2: m_demoMode = DemoMode::TorchlightDemo; LOGI("Demo Mode: Torchlight Demo (Interactive)"); break;
		case 3: m_demoMode = DemoMode::VolumetricDemo; LOGI("Demo Mode: Volumetric Demo (Compact moving)"); break;
		case 4: m_demoMode = DemoMode::VolumetricSculpture; LOGI("Demo Mode: Volumetric Sculpture (Static complex shape with sweeping RT lighting)"); break;
		case 5: m_demoMode = DemoMode::PlasmaAccretion; LOGI("Demo Mode: Plasma Accretion Disk (Orbital plasma with volumetric self-shadowing)"); break;
		case 6:
			m_demoMode = DemoMode::VoxelParticles;
			LOGI("Demo Mode: Voxel Particles (Debug particle simulation with 3D grid)");
			if (!initializeVoxelSystem()) {
				LOGE("Failed to initialize voxel system - falling back to Sphere RT");
				m_demoMode = DemoMode::SphereRT;
			}
			break;
		case 7:
			m_demoMode = DemoMode::MetaballSPH;
			LOGI("Demo Mode: Metaball SPH (SPH physics with metaball density field rendering)");
			// Note: Metaball system is already initialized, no additional setup needed
			break;
		default: m_demoMode = DemoMode::SphereRT; LOGI("Demo Mode: Default Sphere RT"); break;
	}

	// Create SRV descriptors for DXR descriptor table binding
	if (m_tlasSrvIndex == UINT_MAX) {
		m_tlasSrvIndex = m_descriptorAllocator->Allocate();
		if (m_tlasSrvIndex == UINT_MAX) {
			LOGE("Failed to allocate SRV index for TLAS");
			return false;
		}
	}

	if (m_densityVolumeSrvIndex == UINT_MAX) {
		m_densityVolumeSrvIndex = m_descriptorAllocator->Allocate();
		if (m_densityVolumeSrvIndex == UINT_MAX) {
			LOGE("Failed to allocate SRV index for density volume");
			return false;
		}
	}

	// Create TLAS SRV (for descriptor table binding)
	D3D12_SHADER_RESOURCE_VIEW_DESC tlasSrvDesc = {};
	tlasSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	tlasSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	tlasSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	tlasSrvDesc.RaytracingAccelerationStructure.Location = m_tlasResult->GetGPUVirtualAddress();

	D3D12_CPU_DESCRIPTOR_HANDLE tlasSrvHandle = m_descriptorAllocator->GetCPUHandle(m_tlasSrvIndex);
	m_device->CreateShaderResourceView(nullptr, &tlasSrvDesc, tlasSrvHandle);

	// Create density volume SRV (for descriptor table binding)
	if (m_densityVolume && m_densityVolume->GetResource()) {
		D3D12_SHADER_RESOURCE_VIEW_DESC densitySrvDesc = {};
		densitySrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		densitySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
		densitySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		densitySrvDesc.Texture3D.MostDetailedMip = 0;
		densitySrvDesc.Texture3D.MipLevels = 1;
		densitySrvDesc.Texture3D.ResourceMinLODClamp = 0.0f;

		D3D12_CPU_DESCRIPTOR_HANDLE densitySrvHandle = m_descriptorAllocator->GetCPUHandle(m_densityVolumeSrvIndex);
		m_device->CreateShaderResourceView(m_densityVolume->GetResource(), &densitySrvDesc, densitySrvHandle);
	}

	LOGI("DXR initialized successfully with HDR pipeline");
	return true;
}

void App::buildAccelerationStructures() {
	LOGI("Building acceleration structures...");

	// Reset command list
	m_cmdAllocator->Reset();
	m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);

	PIX_SCOPED_EVENT(m_cmdList.Get(), "Build Acceleration Structures");

	// Build BLAS for debug triangle (temporary - conservative AABB causes 100% shadow)
	{
		PIX_SCOPED_EVENT(m_cmdList.Get(), "Build Triangle BLAS");
		if (!m_asBuilder->CreateTriangleBLAS(m_blasResult, m_blasScratch)) {
			LOGE("Failed to create triangle BLAS");
			return;
		}

		// Execute BLAS build on GPU
		m_asBuilder->BuildBLAS(m_cmdList.Get(), m_blasResult.Get(), m_blasScratch.Get());
	}

	// Build TLAS
	{
		PIX_SCOPED_EVENT(m_cmdList.Get(), "Build TLAS");
		if (!m_asBuilder->BuildTLAS(m_tlasResult, m_tlasScratch, m_instanceDescs)) {
			LOGE("Failed to create TLAS");
			return;
		}

		// Execute TLAS build on GPU
		m_asBuilder->BuildTLASGPU(m_cmdList.Get(), m_tlasResult.Get(), m_tlasScratch.Get(),
			m_instanceDescs.Get(), m_blasResult.Get());
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

    // Create global root signature: Use descriptor tables for SRVs and UAVs
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 2; // t0 (TLAS) and t1 (density volume)
    srvRange.BaseShaderRegister = 0; // t0
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0; // u0
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[4]{};

    // SRV descriptor table (t0, t1)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &srvRange;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // HDR UAV as descriptor table (u0)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &uavRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root constants for b0 (GlobalParams: 16 floats)
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.Num32BitValues = 16;
    params[2].Constants.ShaderRegister = 0; // b0
    params[2].Constants.RegisterSpace = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static sampler for density volume (s0)
    D3D12_STATIC_SAMPLER_DESC staticSampler{};
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.MinLOD = 0.0f;
    staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister = 0; // s0
    staticSampler.RegisterSpace = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = params;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &staticSampler;
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

	// Set shader config - Updated for RayPayload with coherenceHint (float4 + uint = 20 bytes)
	m_dxrPipeline->SetShaderConfig(20, sizeof(float) * 2);

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

	// Pass PSO properties to SBT for shader identifier retrieval
	m_sbt->SetPSOProperties(psoProps);

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

			// Update camera with deltaTime for smooth movement
			if (m_camera) {
				m_camera->Update(deltaTime);
			}

			m_particles->Update(m_cmdList.Get(), deltaTime, totalTime);

            // VOL_0002 & VOL_0003 & VOL_0004: Density generation (analytic or curl-advect) and ray march
			if (m_densityVolume && m_rayMarcher && m_hdrUavIndex != UINT_MAX) {
                // Choose density update path
                int useCurl = Env::GetInt("PLASMADX_USE_CURL", 1); // default ON
                if (useCurl != 0) {
                    // VOL_0004: curl-advection ping-pong
                    m_densityVolume->AdvectCurl(m_cmdList.Get(), deltaTime, totalTime);
                } else {
                    // Choose between lava lamp metaballs or legacy fills
                    bool useLavaLamp = Env::GetBool("PLASMADX_LAVA_LAMP", true); // Default to lava lamp
                    if (useLavaLamp && m_metaballSystem) {
                        // Update physics simulation
                        m_metaballSystem->UpdatePhysics(deltaTime);

                        // Fill density volume with metaball shapes
                        m_densityVolume->FillMetaballs(m_cmdList.Get(), m_metaballSystem.get());
                    } else {
                        // Legacy analytic fill or sphere baseline
                        int useSphere = Env::GetInt("PLASMADX_FILL_SPHERE", 0);
                        static bool s_sphereFilled = false;
                        if (useSphere != 0) {
                            if (!s_sphereFilled) {
                                m_densityVolume->FillAnalyticSphere(
                                    m_cmdList.Get(), DirectX::XMFLOAT3(0.5f, 0.5f, 0.5f), 0.30f, 1.0f);
                                s_sphereFilled = true;
                            }
                        } else {
                            m_densityVolume->FillAnalytic(m_cmdList.Get(), totalTime);
                        }
                    }
                }

				// Ensure density is in SRV state for sampling during ray march
				m_densityVolume->TransitionToSRV(m_cmdList.Get());

				// Update ray marcher screen size
				m_rayMarcher->SetScreenSize(float(m_width), float(m_height));

                // VOL_0003C: Allow automation to set debug mode via environment
                {
                    int dbg = Env::GetInt("PLASMADX_DEBUG_MODE", -1);
                    if (dbg >= 0) {
                        m_rayMarcher->SetDebugMode(static_cast<uint32_t>(dbg));
                    }
                }

				// VOL_0003: Ray march through the density volume
				m_rayMarcher->March(m_cmdList.Get(),
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

		// MODE 9: Mesh shader particle rendering (isolated path, no volumetrics)
		if (m_demoMode == DemoMode::AccretionMeshParticles && m_meshParticleSystem) {
			// FIX: Get frame index FIRST, then wait for that frame to complete
			m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
			waitForFrame();
			m_cmdAllocator->Reset();
			m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);

			PIX_SCOPED_EVENT(m_cmdList.Get(), "Mesh Particle System");

			static bool s_firstFrame = true;
			if (s_firstFrame) {
				LOGI("MODE 9: Starting mesh particle render loop");

				// Position camera to view accretion disk from above and behind
				// Disk is in XZ plane (Y=0), radius 6-100 units
				// Place camera at (0, 80, -120) looking toward origin
				if (m_camera) {
					// We need to manually set camera position/rotation
					// For now, just log that we're using default camera
					LOGI("MODE 9: Camera viewing accretion disk from elevated position");
				}
				s_firstFrame = false;
			}

			// Update camera with deltaTime for smooth movement
			static float totalTime = 0.0f;
			const float deltaTime = 0.016f; // ~60 FPS
			totalTime += deltaTime;

			if (m_camera) {
				m_camera->Update(deltaTime);
			}

			try {
				// Update particle physics (accretion disk simulation)
				m_meshParticleSystem->UpdatePhysics(m_cmdList.Get(), deltaTime);

				// UAV barrier: Ensure particle physics writes complete before subsequent reads
				D3D12_RESOURCE_BARRIER uavBarrier{};
				uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
				uavBarrier.UAV.pResource = m_meshParticleSystem->GetParticleBuffer();
				m_cmdList->ResourceBarrier(1, &uavBarrier);

				// Mode 9.1+: Generate shadow map before particle rendering (RayQuery compute shader)
				if (m_mode9SubMode >= Mode9SubMode::ShadowMap) {
					renderShadowMap();  // Runs on main command list, no fence wait needed
				}

				// Get current backbuffer for direct rendering (frame index already set above)
				auto backbuffer = m_backbuffers[m_frameIndex];

				static int s_frameCount = 0;
				if (s_frameCount < 3) {
					LOGI("MODE 9: Rendering frame " + std::to_string(s_frameCount) + " to backbuffer " + std::to_string(m_frameIndex));
					s_frameCount++;
				}

				// Transition backbuffer to render target
				D3D12_RESOURCE_BARRIER toRTV{};
				toRTV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				toRTV.Transition.pResource = backbuffer.Get();
				toRTV.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				toRTV.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
				toRTV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				m_cmdList->ResourceBarrier(1, &toRTV);

				// Get RTV handle for backbuffer
				D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
				rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;

				// Clear backbuffer to black
				float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				m_cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

				// Mode 9.2: Clear emission texture to zero before particle rendering
				if (m_mode9SubMode >= Mode9SubMode::ParticleRelight && m_emissionTexture) {
					float emissionClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					m_cmdList->ClearRenderTargetView(m_emissionRtvHandle, emissionClear, 0, nullptr);
				}

				// Render particles with mesh shaders
				DirectX::XMMATRIX viewMatrix = m_camera->GetViewMatrix();
				DirectX::XMMATRIX projMatrix = m_camera->GetProjectionMatrix();
				DirectX::XMFLOAT3 cameraPos = m_camera->GetPosition();

				// Bind descriptor heap for shadow map SRV (required for descriptor table access)
				ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
				m_cmdList->SetDescriptorHeaps(1, heaps);

				// Get shadow map GPU handle for Mode 9.1+
				D3D12_GPU_DESCRIPTOR_HANDLE shadowMapGpuHandle = {};
				if (m_shadowMapSrvIndex != UINT_MAX) {
					shadowMapGpuHandle = m_descriptorAllocator->GetGPUHandle(m_shadowMapSrvIndex);
				}

				// Mode 9.2: Get particle lighting SRV GPU handle
				D3D12_GPU_DESCRIPTOR_HANDLE lightingSrvGpuHandle = m_descriptorAllocator->GetGPUHandle(m_particleLightingSrvIndex);

				// DIAGNOSTIC: Log SRV/UAV indices for lighting buffer
				static bool s_indicesLogged = false;
				if (!s_indicesLogged && m_mode9SubMode >= Mode9SubMode::ParticleRelight) {
					LOGI("RT Lighting buffer descriptors: UAV index=" + std::to_string(m_particleLightingUavIndex) +
					     " SRV index=" + std::to_string(m_particleLightingSrvIndex) +
					     " SRV GPU handle=0x" + std::to_string(lightingSrvGpuHandle.ptr));
					s_indicesLogged = true;
				}

				// Mode 9.2 RT: Compute lighting BEFORE rendering particles (FIX: eliminates one-frame delay)
				if (m_mode9SubMode >= Mode9SubMode::ParticleRelight && m_emissionGridPSO) {
					// Transition emission texture to SRV for compute reading
					D3D12_RESOURCE_BARRIER emissionToSRV{};
					emissionToSRV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					emissionToSRV.Transition.pResource = m_emissionTexture.Get();
					emissionToSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
					emissionToSRV.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
					emissionToSRV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					m_cmdList->ResourceBarrier(1, &emissionToSRV);

					// Build spatial grid from emission buffer
					// DISABLED for RT lighting testing
					// computeEmissionGrid();

					// Transition lighting buffer from SRV (previous frame) back to UAV for clearing/writing
					// Track which mode we were in last frame to detect mode changes
					static Mode9SubMode s_lastMode = Mode9SubMode::Baseline;
					bool modeJustChanged = (s_lastMode != m_mode9SubMode);
					s_lastMode = m_mode9SubMode;

					if (!modeJustChanged) {
						// Not first frame of mode 9.2, need to transition from SRV back to UAV
						D3D12_RESOURCE_BARRIER lightingToUAV{};
						lightingToUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
						lightingToUAV.Transition.pResource = m_particleLightingBuffer.Get();
						lightingToUAV.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
						lightingToUAV.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
						lightingToUAV.Transition.Subresource = 0;
						m_cmdList->ResourceBarrier(1, &lightingToUAV);
					}
					// else: first frame of mode 9.2, buffer is already in UAV state (initial state)

					// Clear particle lighting buffer to zero before computing new lighting
					float clearZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					m_cmdList->ClearUnorderedAccessViewFloat(
						m_descriptorAllocator->GetGPUHandle(m_particleLightingUavIndex),
						m_descriptorAllocator->GetCPUHandle(m_particleLightingUavIndex),
						m_particleLightingBuffer.Get(),
						clearZero,
						0, nullptr);

					// Mode 9.2 Milestone 3: Apply grid-based lighting to particles
					// DISABLED: Spatial grid conflicts with RT lighting (both write to same buffer)
					// computeParticleLighting();

					// Mode 9.2 RT: Per-particle BLAS and ray traced lighting (GENUINE DXR)
					if (m_perParticleBLAS && m_rtLightingPSO) {
						// Update particle AABBs from current positions
						updateParticleAABBs();

						// Compute ray traced lighting (RayQuery)
						computeRTLighting();

						// CRITICAL: Transition lighting buffer from UAV (write) to SRV (read) for particle rendering
						D3D12_RESOURCE_BARRIER lightingToSRV = {};
						lightingToSRV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
						lightingToSRV.Transition.pResource = m_particleLightingBuffer.Get();
						lightingToSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
						lightingToSRV.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
						lightingToSRV.Transition.Subresource = 0;
						m_cmdList->ResourceBarrier(1, &lightingToSRV);
					}

					// Transition emission texture back to RENDER_TARGET for next frame
					emissionToSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
					emissionToSRV.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
					m_cmdList->ResourceBarrier(1, &emissionToSRV);
				}

				// Render particles AFTER RT lighting is computed (FIX: eliminates one-frame delay)
				// Mode 10: Use compute + traditional VS/PS if enabled, otherwise use mesh shaders
				if (m_mode10Active) {
					// Mode 10: Compute shader builds vertices, traditional VS/PS renders
					// Get particle buffer GPU handle
					UINT particleBufferSrvIndex = m_meshParticleSystem->GetParticleBufferSRVIndex();
					D3D12_GPU_DESCRIPTOR_HANDLE particleBufferGpuHandle = m_descriptorAllocator->GetGPUHandle(particleBufferSrvIndex);

					m_meshParticleSystem->RenderComputeParticles(m_cmdList.Get(),
						viewMatrix, projMatrix, cameraPos, rtvHandle, m_width, m_height,
						particleBufferGpuHandle,  // Particle buffer
						lightingSrvGpuHandle,  // RT lighting buffer
						static_cast<uint32_t>(m_mode10SubMode));
				} else {
					// Mode 9: Mesh shader rendering (original path)
					m_meshParticleSystem->RenderParticles(m_cmdList.Get(),
						viewMatrix, projMatrix, cameraPos, rtvHandle, m_width, m_height,
						shadowMapGpuHandle, static_cast<uint32_t>(m_mode9SubMode),
						m_emissionRtvHandle,  // Mode 9.2: Pass emission RTV for dual RT output
						lightingSrvGpuHandle);  // Mode 9.2: Pass particle lighting SRV
				}

				// F8 Debug: Copy emission buffer to backbuffer for visualization
				if (m_showEmissionDebug && m_emissionTexture) {
					// Transition emission texture from RENDER_TARGET to COPY_SOURCE
					D3D12_RESOURCE_BARRIER emissionToSource{};
					emissionToSource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					emissionToSource.Transition.pResource = m_emissionTexture.Get();
					emissionToSource.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
					emissionToSource.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
					emissionToSource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

					// Transition backbuffer from RENDER_TARGET to COPY_DEST (temporarily)
					D3D12_RESOURCE_BARRIER bbToDest{};
					bbToDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					bbToDest.Transition.pResource = backbuffer.Get();
					bbToDest.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
					bbToDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
					bbToDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

					D3D12_RESOURCE_BARRIER barriers[2] = { emissionToSource, bbToDest };
					m_cmdList->ResourceBarrier(2, barriers);

					// Copy emission texture to backbuffer
					m_cmdList->CopyResource(backbuffer.Get(), m_emissionTexture.Get());

					// Transition emission texture back to RENDER_TARGET
					emissionToSource.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
					emissionToSource.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

					// Transition backbuffer to RENDER_TARGET (before final PRESENT transition)
					bbToDest.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
					bbToDest.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

					D3D12_RESOURCE_BARRIER restoreBarriers[2] = { emissionToSource, bbToDest };
					m_cmdList->ResourceBarrier(2, restoreBarriers);
				}

				// Transition backbuffer back to present
				D3D12_RESOURCE_BARRIER toPresent{};
				toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				toPresent.Transition.pResource = backbuffer.Get();
				toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
				toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
				toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				m_cmdList->ResourceBarrier(1, &toPresent);

				wroteHDRThisFrame = true; // Mark that we rendered something

			} catch (const std::exception& e) {
				LOGE(std::string("Mesh particle rendering failed: ") + e.what());
			}

			// Skip DXR and composite for Mode 9 - go straight to present
			m_cmdList->Close();
			ID3D12CommandList* lists[] = { m_cmdList.Get() };
			m_queue->ExecuteCommandLists(1, lists);

			// Present (PIX event can't be used after Close())
			HRESULT presentHr = m_swapchain->Present(1, 0);

			// FIX: Check for device removal to prevent infinite error loop
			if (FAILED(presentHr)) {
				LOGE("Present failed in Mode 9: 0x" + std::to_string(static_cast<uint32_t>(presentHr)));
				if (presentHr == DXGI_ERROR_DEVICE_REMOVED || presentHr == DXGI_ERROR_DEVICE_RESET) {
					HRESULT reason = m_device->GetDeviceRemovedReason();
					LOGE("DEVICE REMOVED! Reason: 0x" + std::to_string(static_cast<uint32_t>(reason)));
					dumpInfoQueueMessages();
					// Exit application immediately instead of continuing infinite loop
					PostQuitMessage(static_cast<int>(reason));
					return;
				}
			}

			m_frameFenceValues[m_frameIndex] = ++m_fenceValue;
			m_queue->Signal(m_fence.Get(), m_fenceValue);
			return; // Early return for Mode 9 - skip normal DXR/composite path
		}

		// HDR Pipeline: Render to HDR texture then composite to backbuffer
		{
			PIX_SCOPED_EVENT(m_cmdList.Get(), "HDR Content Generation");

			// Set descriptor heaps
			ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
			m_cmdList->SetDescriptorHeaps(1, heaps);

            // APP_0003: Guard DXR dispatch behind validity checks and env override
            bool dxrDisabled = Env::GetBool("PLASMADX_DISABLE_DXR", false); // default: DXR enabled for RT lighting
            static bool s_dxrBlend = Env::GetBool("PLASMADX_DXR_BLEND", false); // additive blend over compute
            bool canDoDXR = (!dxrDisabled && m_dxrPipeline && m_dxrPipeline->GetPSO() && m_sbt && m_tlasResult);
            // Prefer compute metaball (lava lamp) path when requested to avoid DXR raygen overwriting HDR
            if (!dxrDisabled && !s_dxrBlend) {
                int useCurl = Env::GetInt("PLASMADX_USE_CURL", 1);
                bool useLavaLamp = Env::GetBool("PLASMADX_LAVA_LAMP", true);
                if (useCurl == 0 && useLavaLamp) {
                    canDoDXR = false; // keep compute HDR content
                    LOGI("DXR: Disabled this frame (Lava Lamp mode active)");
                }
            }

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
                    LOGI("DXR: Starting DispatchRays with valid SBT addresses");

                    // Set descriptor heaps immediately before DXR dispatch (critical for UAV access)
                    ID3D12DescriptorHeap* dxrHeaps[] = { m_srvUavHeap.Get() };
                    m_cmdList->SetDescriptorHeaps(1, dxrHeaps);
                    LOGI("DXR: Reset descriptor heaps for raygen UAV access");

                    // Set DXR pipeline state and root signature
                    m_cmdList->SetPipelineState1(m_dxrPipeline->GetPSO());
                    m_cmdList->SetComputeRootSignature(m_globalRootSignature.Get());

                    // Bind SRV descriptor table - check for DXR12Test mode
                    if (m_demoMode == DemoMode::DXR12Test) {
                        LOGI("DXR: Binding TLAS only (DXR12Test mode)");
                        D3D12_GPU_DESCRIPTOR_HANDLE srvTableHandle = m_descriptorAllocator->GetGPUHandle(m_tlasSrvIndex);
                        m_cmdList->SetComputeRootDescriptorTable(0, srvTableHandle);
                    } else {
                        LOGI("DXR: Binding SRV descriptor table (TLAS + density volume)");
                        D3D12_GPU_DESCRIPTOR_HANDLE srvTableHandle = m_descriptorAllocator->GetGPUHandle(m_tlasSrvIndex);
                        m_cmdList->SetComputeRootDescriptorTable(0, srvTableHandle);
                    }
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
                    LOGI("DXR: Binding HDR texture as UAV for output");
                    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = m_descriptorAllocator->GetGPUHandle(m_hdrUavIndex);
                    std::stringstream uavMsg;
                    uavMsg << "DXR: UAV handle ptr=0x" << std::hex << uavHandle.ptr << ", index=" << std::dec << m_hdrUavIndex;
                    LOGI(uavMsg.str());
                    m_cmdList->SetComputeRootDescriptorTable(1, uavHandle);

                    // Set per-frame root constants (b0)
                    struct GlobalParams {
                        float lightPos[3]; float time;
                        float lightDir[3]; float innerCos;
                        float lightColor[3]; float outerCos;
                        float mode; float bg; float blendEnabled; float blendScale;
                    } gp{};

                    // Demo mode-based lighting setup
                    static float tAccum = 0.0f; tAccum += 0.016f;

                    if (m_demoMode == DemoMode::TorchlightDemo) {
                        // TORCHLIGHT DEMO MODE: Mouse-controlled spotlight

                        if (m_torchOn) {
                            // Mouse-controlled torch: map mouse to 3D sphere surface
                            float mouseNDCX = (m_mouseX - 0.5f) * 2.0f;  // -1 to 1
                            float mouseNDCY = (0.5f - m_mouseY) * 2.0f;  // -1 to 1 (flip Y)

                            // Convert 2D mouse to spherical coordinates for realistic 3D movement
                            float azimuth = mouseNDCX * 3.14159f;      // -π to π (horizontal rotation)
                            float elevation = mouseNDCY * 1.57079f;   // -π/2 to π/2 (vertical angle)

                            // Clamp elevation to reasonable range to prevent light going behind sphere
                            elevation = std::max(-1.2f, std::min(1.2f, elevation));

                            // Position light on a sphere around the volume (radius = 3.0)
                            float lightRadius = 3.0f;
                            gp.lightPos[0] = lightRadius * cos(elevation) * sin(azimuth);
                            gp.lightPos[1] = lightRadius * sin(elevation);
                            gp.lightPos[2] = lightRadius * cos(elevation) * cos(azimuth);

                            // Light direction points toward sphere center (0,0,0)
                            float len = sqrt(gp.lightPos[0]*gp.lightPos[0] + gp.lightPos[1]*gp.lightPos[1] + gp.lightPos[2]*gp.lightPos[2]);
                            gp.lightDir[0] = -gp.lightPos[0] / len;
                            gp.lightDir[1] = -gp.lightPos[1] / len;
                            gp.lightDir[2] = -gp.lightPos[2] / len;

                            // Color cycling with C key
                            float colors[][3] = {
                                {1.2f, 1.0f, 0.8f},  // Warm torch
                                {0.8f, 1.0f, 1.2f},  // Cool blue
                                {1.5f, 0.3f, 0.3f},  // Red
                                {0.3f, 1.5f, 0.3f},  // Green
                                {1.2f, 0.3f, 1.2f}   // Purple
                            };
                            gp.lightColor[0] = colors[m_lightColorIndex][0];
                            gp.lightColor[1] = colors[m_lightColorIndex][1];
                            gp.lightColor[2] = colors[m_lightColorIndex][2];
                        } else {
                            // Torch off: no light
                            gp.lightPos[0] = 0.0f; gp.lightPos[1] = 0.0f; gp.lightPos[2] = -10.0f; // Far away
                            gp.lightDir[0] = 0.0f; gp.lightDir[1] = 0.0f; gp.lightDir[2] = 1.0f;
                            gp.lightColor[0] = 0.0f; gp.lightColor[1] = 0.0f; gp.lightColor[2] = 0.0f; // Black
                        }

                        // Torch settings
                        gp.innerCos = cosf(12.0f * 3.14159265f / 180.0f); // 12 degrees
                        gp.outerCos = cosf(25.0f * 3.14159265f / 180.0f); // 25 degrees
                        gp.bg = 0.02f; // Very dark background
                    } else if (m_demoMode == DemoMode::SphereRT) {
                        // SPHERE RT BASELINE: Static camera with fixed lighting for pure DXR demo

                        // Static light position - positioned to show sphere clearly
                        gp.lightPos[0] = -1.5f;
                        gp.lightPos[1] = 1.0f;
                        gp.lightPos[2] = -2.0f;

                        // Light direction points toward sphere center (0,0,0)
                        float len = sqrt(gp.lightPos[0]*gp.lightPos[0] + gp.lightPos[1]*gp.lightPos[1] + gp.lightPos[2]*gp.lightPos[2]);
                        gp.lightDir[0] = -gp.lightPos[0] / len;
                        gp.lightDir[1] = -gp.lightPos[1] / len;
                        gp.lightDir[2] = -gp.lightPos[2] / len;

                        // High-contrast white light for clear visibility
                        gp.lightColor[0] = 1.2f;
                        gp.lightColor[1] = 1.1f;
                        gp.lightColor[2] = 1.0f;

                        // Tight spotlight for good definition
                        gp.innerCos = cosf(8.0f * 3.14159265f / 180.0f);  // 8 degrees
                        gp.outerCos = cosf(15.0f * 3.14159265f / 180.0f); // 15 degrees
                        gp.bg = Env::GetInt("PLASMADX_YELLOW_BG", 0) ? 1.0f : 0.01f; // Very dark or yellow safeguard
                    } else if (m_demoMode == DemoMode::VolumetricSculpture) {
                        // VOLUMETRIC SCULPTURE: Sweeping directional light with figure-8 pattern
                        float angle = tAccum * 0.8f; // Slightly faster than fallback
                        float radius = 2.5f; // Larger radius for dramatic sweeping

                        // Smaller circular movement pattern around volume center
                        float volumeCenterX = 0.0f;   // Match shader volume center
                        float volumeCenterY = 0.0f;
                        float volumeCenterZ = -1.5f;
                        float moveRadius = 0.30f;  // Slightly larger for better visibility

                        gp.lightPos[0] = sin(angle * 0.45f) * moveRadius + volumeCenterX;
                        gp.lightPos[1] = cos(angle * 0.45f) * moveRadius * 0.5f + volumeCenterY;
                        gp.lightPos[2] = volumeCenterZ + 0.8f;  // Position in front of volume

                        // Light direction points toward volume center
                        float dx = volumeCenterX - gp.lightPos[0];
                        float dy = volumeCenterY - gp.lightPos[1];
                        float dz = volumeCenterZ - gp.lightPos[2];
                        float len = sqrt(dx*dx + dy*dy + dz*dz);
                        gp.lightDir[0] = dx / len;
                        gp.lightDir[1] = dy / len;
                        gp.lightDir[2] = dz / len;

                        // Moderate torch light for direct illumination (no accumulation)
                        gp.lightColor[0] = 2.0f;  // Reduced for direct model
                        gp.lightColor[1] = 1.9f;  // Warmer torch colors
                        gp.lightColor[2] = 1.8f;

                        // MUCH tighter spotlight cone like torch beam (user feedback: too wide)
                        gp.innerCos = cosf(2.0f * 3.14159265f / 180.0f);  // 2 degrees inner (was 8)
                        gp.outerCos = cosf(6.0f * 3.14159265f / 180.0f);  // 6 degrees outer (was 15)
                        gp.bg = 0.0f; // Pure black background
                    } else if (m_demoMode == DemoMode::PlasmaAccretion) {
                        // === PLASMA ACCRETION DISK: Physics-based controls via lighting parameters ===
                        // Map lighting parameters to plasma physics for interactive control

                        // Gravity center offset (lightPos controls disk center offset)
                        gp.lightPos[0] = m_plasmaOffsetX;  // Disk center X offset
                        gp.lightPos[1] = m_plasmaOffsetY;  // Disk center Y offset
                        gp.lightPos[2] = m_plasmaOffsetZ;  // Disk center Z offset

                        // Angular velocity control (lightDir.x controls rotation speed)
                        // lightDir.y controls particle density multiplier
                        // lightDir.z controls disk thickness
                        gp.lightDir[0] = m_plasmaAngularVel;  // Angular velocity base (0.1-2.0)
                        gp.lightDir[1] = m_plasmaDensity;     // Particle density multiplier (0.5-3.0)
                        gp.lightDir[2] = m_plasmaDiskThickness; // Disk thickness multiplier (0.5-2.0)

                        // Temperature control (lightColor controls emission intensity)
                        gp.lightColor[0] = m_plasmaCoreTemp;  // Core temperature (blue-white intensity)
                        gp.lightColor[1] = m_plasmaMidTemp;   // Mid-disk temperature (yellow-orange intensity)
                        gp.lightColor[2] = m_plasmaEdgeTemp;  // Outer edge temperature (red-orange intensity)

                        // Gravity strength (innerCos) and simulation quality (outerCos)
                        gp.innerCos = m_plasmaGravityExp;   // Gravity strength (Keplerian exponent: 0.5-2.5)
                        gp.outerCos = static_cast<float>(m_plasmaQuality); // Max ray marching steps (100-300)
                        gp.bg = 0.01f; // Dark space background
                    } else {
                        // Default: sweeping spotlight animation (fallback)
                        float angle = tAccum * 0.7f;
                        float radius = 2.0f;
                        gp.lightPos[0] = -cosf(angle) * radius;
                        gp.lightPos[1] = 0.9f + 0.2f * sinf(angle * 0.5f);
                        gp.lightPos[2] = -2.0f + 0.3f * sinf(angle);
                        // Light looks at origin (sphere center)
                        gp.lightDir[0] = 0.0f - gp.lightPos[0];
                        gp.lightDir[1] = 0.0f - gp.lightPos[1];
                        gp.lightDir[2] = 0.0f - gp.lightPos[2];

                        // Standard settings
                        gp.innerCos = cosf(12.0f * 3.14159265f / 180.0f);
                        gp.outerCos = cosf(20.0f * 3.14159265f / 180.0f);
                        gp.lightColor[0] = 1.0f; gp.lightColor[1] = 0.95f; gp.lightColor[2] = 0.85f;
                        gp.bg = 1.0f; // Normal background
                    }

                    gp.time = tAccum;
                    gp.mode = static_cast<float>(m_demoMode); // Pass demo mode to shader

                    // Mode 1 (SphereRT): pure DXR, Mode 4 (VolumetricSculpture): blend enabled for better surface contact
                    if (m_demoMode == DemoMode::SphereRT) {
                        gp.blendEnabled = 0.0f;  // Force OFF for pure DXR
                        gp.blendScale = 1.0f;
                    } else if (m_demoMode == DemoMode::VolumetricSculpture) {
                        gp.blendEnabled = 0.0f;  // Disable blending to prevent burn-in accumulation
                        gp.blendScale = 1.0f;    // Replace mode for immediate torch response
                    } else {
                        gp.blendEnabled = m_dxrBlend ? 1.0f : 0.0f;
                        gp.blendScale = m_dxrBlendScale;
                    }

                    m_cmdList->SetComputeRoot32BitConstants(2, sizeof(GlobalParams)/4, &gp, 0);

                    // Add UAV barrier before DispatchRays (keeps ordering)
                    LOGI("DXR: Adding UAV barrier before DispatchRays");
                    D3D12_RESOURCE_BARRIER uavBarrier{};
                    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    uavBarrier.UAV.pResource = m_hdrTexture.Get();
                    m_cmdList->ResourceBarrier(1, &uavBarrier);

                    // Dispatch rays with dimension verification
                    std::stringstream dispatchMsg;
                    dispatchMsg << "DXR: DispatchRays " << dispatchDesc.Width << "x" << dispatchDesc.Height;
                    dispatchMsg << " (HDR texture: " << m_width << "x" << m_height << ")";
                    LOGI(dispatchMsg.str());

                    // MCP Debug: Verify dimensions match
                    if (dispatchDesc.Width != m_width || dispatchDesc.Height != m_height) {
                        LOGW("DXR: Dimension mismatch detected - this could cause partial rendering!");
                    }
                    m_cmdList->DispatchRays(&dispatchDesc);
                    LOGI("DXR: DispatchRays completed - testing magenta raygen output");
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
		// Bind SRV descriptor table (TLAS + density volume)
		D3D12_GPU_DESCRIPTOR_HANDLE srvTableHandle = m_descriptorAllocator->GetGPUHandle(m_tlasSrvIndex);
		m_cmdList->SetComputeRootDescriptorTable(0, srvTableHandle);
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

// MODE 9.1: Shadow Map Implementation
// Insert this into App.cpp before VOXEL PARTICLE SYSTEM section

bool App::createShadowMapTexture() {
    // Create shadow map texture (R16_FLOAT, 1024x1024) for Mode 9.1
    // This is a dedicated resource that avoids the HDR texture driver bug
    constexpr UINT SHADOW_MAP_SIZE = 1024;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = SHADOW_MAP_SIZE;
    texDesc.Height = SHADOW_MAP_SIZE;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R16_FLOAT;  // Single channel for shadow visibility
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
        nullptr,
        IID_PPV_ARGS(&m_shadowMapTexture));

    if (FAILED(hr)) {
        LOGE("Failed to create shadow map texture: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    // Allocate descriptor indices
    if (m_shadowMapSrvIndex == UINT_MAX) {
        m_shadowMapSrvIndex = m_descriptorAllocator->Allocate();
        if (m_shadowMapSrvIndex == UINT_MAX) {
            LOGE("Failed to allocate SRV index for shadow map");
            return false;
        }
    }

    if (m_shadowMapUavIndex == UINT_MAX) {
        m_shadowMapUavIndex = m_descriptorAllocator->Allocate();
        if (m_shadowMapUavIndex == UINT_MAX) {
            LOGE("Failed to allocate UAV index for shadow map");
            return false;
        }
    }

    // Create SRV for shadow map (for particle pixel shader sampling)
    if (m_descriptorAllocator) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_descriptorAllocator->GetCPUHandle(m_shadowMapSrvIndex);
        m_device->CreateShaderResourceView(m_shadowMapTexture.Get(), &srvDesc, srvHandle);
    }

    // Create UAV for shadow map (for DXR raygen write)
    if (m_descriptorAllocator) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;

        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = m_descriptorAllocator->GetCPUHandle(m_shadowMapUavIndex);
        m_device->CreateUnorderedAccessView(m_shadowMapTexture.Get(), nullptr, &uavDesc, uavHandle);
    }

    LOGI("Shadow map texture created (1024x1024 R16) - SRV[" + std::to_string(m_shadowMapSrvIndex) + "] UAV[" + std::to_string(m_shadowMapUavIndex) + "]");
    return true;
}

bool App::createEmissionTexture() {
    // Create emission texture (R11G11B10_FLOAT, screen resolution) for Mode 9.2
    // Stores emissive light from hot particles (T > 15000K)

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;  // HDR format, no alpha needed
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&m_emissionTexture));

    if (FAILED(hr)) {
        LOGE("Failed to create emission texture: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    // NOTE: RTV will be created later in createRTVs() after RTV heap is initialized
    // (RTV heap doesn't exist yet at this point in initialization)

    // Allocate SRV for emission texture (for future lighting passes)
    if (m_emissionSrvIndex == UINT_MAX) {
        m_emissionSrvIndex = m_descriptorAllocator->Allocate();
        if (m_emissionSrvIndex == UINT_MAX) {
            LOGE("Failed to allocate SRV index for emission texture");
            return false;
        }
    }

    // Create SRV for emission texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_descriptorAllocator->GetCPUHandle(m_emissionSrvIndex);
    m_device->CreateShaderResourceView(m_emissionTexture.Get(), &srvDesc, srvHandle);

    LOGI("Emission texture created (" + std::to_string(m_width) + "x" + std::to_string(m_height) + " R11G11B10) - SRV[" + std::to_string(m_emissionSrvIndex) + "] RTV[2]");
    return true;
}

bool App::createEmissionGridResources() {
    // Mode 9.2 Milestone 2-3: Create GPU buffers for spatial lighting system

    // 1. Emission Grid Buffer: 64^3 cells, float4 per cell (rgb=emission, w=count)
    const UINT gridCellCount = EMISSION_GRID_RESOLUTION * EMISSION_GRID_RESOLUTION * EMISSION_GRID_RESOLUTION;
    const UINT gridBufferSize = gridCellCount * sizeof(float) * 4;  // float4 per cell

    D3D12_RESOURCE_DESC gridBufferDesc = {};
    gridBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    gridBufferDesc.Width = gridBufferSize;
    gridBufferDesc.Height = 1;
    gridBufferDesc.DepthOrArraySize = 1;
    gridBufferDesc.MipLevels = 1;
    gridBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    gridBufferDesc.SampleDesc.Count = 1;
    gridBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    gridBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &gridBufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_emissionGridBuffer));

    if (FAILED(hr)) {
        LOGE("Failed to create emission grid buffer: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    // Allocate UAV for grid building
    m_emissionGridUavIndex = m_descriptorAllocator->Allocate();
    if (m_emissionGridUavIndex == UINT_MAX) {
        LOGE("Failed to allocate UAV for emission grid");
        return false;
    }

    // Create structured UAV for RWStructuredBuffer<uint> with working atomic operations
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;  // Structured buffer uses UNKNOWN format
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = gridBufferSize / 4;  // Size in DWORDs (4 uints per cell)
    uavDesc.Buffer.StructureByteStride = 4;  // 4 bytes per uint element
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;  // No RAW flag for structured buffer

    D3D12_CPU_DESCRIPTOR_HANDLE gridUavHandle = m_descriptorAllocator->GetCPUHandle(m_emissionGridUavIndex);
    m_device->CreateUnorderedAccessView(m_emissionGridBuffer.Get(), nullptr, &uavDesc, gridUavHandle);

    // Allocate SRV for lighting shader
    m_emissionGridSrvIndex = m_descriptorAllocator->Allocate();
    if (m_emissionGridSrvIndex == UINT_MAX) {
        LOGE("Failed to allocate SRV for emission grid");
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;  // Structured buffer uses UNKNOWN format
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = gridBufferSize / 4;  // Size in uints
    srvDesc.Buffer.StructureByteStride = 4;  // 4 bytes per uint element
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE gridSrvHandle = m_descriptorAllocator->GetCPUHandle(m_emissionGridSrvIndex);
    m_device->CreateShaderResourceView(m_emissionGridBuffer.Get(), &srvDesc, gridSrvHandle);

    // 2. Particle Lighting Buffer: float4 per particle (rgb=additive light, w=unused)
    const UINT particleCount = m_mode9ParticleCount;
    const UINT lightingBufferSize = particleCount * sizeof(float) * 4;

    D3D12_RESOURCE_DESC lightingBufferDesc = {};
    lightingBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    lightingBufferDesc.Width = lightingBufferSize;
    lightingBufferDesc.Height = 1;
    lightingBufferDesc.DepthOrArraySize = 1;
    lightingBufferDesc.MipLevels = 1;
    lightingBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    lightingBufferDesc.SampleDesc.Count = 1;
    lightingBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    lightingBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &lightingBufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_particleLightingBuffer));

    if (FAILED(hr)) {
        LOGE("Failed to create particle lighting buffer: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    // Allocate UAV for lighting compute
    m_particleLightingUavIndex = m_descriptorAllocator->Allocate();
    if (m_particleLightingUavIndex == UINT_MAX) {
        LOGE("Failed to allocate UAV for particle lighting");
        return false;
    }

    // Create structured UAV for particle lighting buffer (float4 per particle)
    D3D12_UNORDERED_ACCESS_VIEW_DESC lightingUavDesc = {};
    lightingUavDesc.Format = DXGI_FORMAT_UNKNOWN;  // Structured buffer uses UNKNOWN
    lightingUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    lightingUavDesc.Buffer.FirstElement = 0;
    lightingUavDesc.Buffer.NumElements = particleCount;
    lightingUavDesc.Buffer.StructureByteStride = 16;  // sizeof(float4) = 16 bytes
    lightingUavDesc.Buffer.CounterOffsetInBytes = 0;
    lightingUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE lightingUavHandle = m_descriptorAllocator->GetCPUHandle(m_particleLightingUavIndex);
    m_device->CreateUnorderedAccessView(m_particleLightingBuffer.Get(), nullptr, &lightingUavDesc, lightingUavHandle);

    // Allocate SRV for particle rendering
    m_particleLightingSrvIndex = m_descriptorAllocator->Allocate();
    if (m_particleLightingSrvIndex == UINT_MAX) {
        LOGE("Failed to allocate SRV for particle lighting");
        return false;
    }

    // CRITICAL FIX: Create structured SRV descriptor (matches UAV format)
    D3D12_SHADER_RESOURCE_VIEW_DESC lightingSrvDesc = {};
    lightingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;  // Structured buffer uses UNKNOWN
    lightingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    lightingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    lightingSrvDesc.Buffer.FirstElement = 0;
    lightingSrvDesc.Buffer.NumElements = particleCount;
    lightingSrvDesc.Buffer.StructureByteStride = 16;  // sizeof(float4) = 16 bytes
    lightingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE lightingSrvHandle = m_descriptorAllocator->GetCPUHandle(m_particleLightingSrvIndex);
    m_device->CreateShaderResourceView(m_particleLightingBuffer.Get(), &lightingSrvDesc, lightingSrvHandle);

    // 2.5. Create readback buffers for diagnostics (CPU-readable copies)
    D3D12_HEAP_PROPERTIES readbackHeapProps = {};
    readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC readbackGridDesc = {};
    readbackGridDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackGridDesc.Width = 64 * sizeof(float) * 4;  // Sample first 64 grid cells
    readbackGridDesc.Height = 1;
    readbackGridDesc.DepthOrArraySize = 1;
    readbackGridDesc.MipLevels = 1;
    readbackGridDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackGridDesc.SampleDesc.Count = 1;
    readbackGridDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    readbackGridDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = m_device->CreateCommittedResource(
        &readbackHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackGridDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_emissionGridReadback));
    if (FAILED(hr)) {
        LOGE("Failed to create emission grid readback buffer: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    D3D12_RESOURCE_DESC readbackLightingDesc = {};
    readbackLightingDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackLightingDesc.Width = 16 * sizeof(float) * 4;  // Sample first 16 particles' lighting
    readbackLightingDesc.Height = 1;
    readbackLightingDesc.DepthOrArraySize = 1;
    readbackLightingDesc.MipLevels = 1;
    readbackLightingDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackLightingDesc.SampleDesc.Count = 1;
    readbackLightingDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    readbackLightingDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = m_device->CreateCommittedResource(
        &readbackHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackLightingDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_particleLightingReadback));
    if (FAILED(hr)) {
        LOGE("Failed to create particle lighting readback buffer: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    LOGI("Created diagnostic readback buffers (64 grid cells, 16 particles)");

    // 3. Create constant buffer for grid builder shader
    const UINT constantBufferSize = 256;  // Align to 256 bytes for CBV

    D3D12_RESOURCE_DESC constantBufferDesc = {};
    constantBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    constantBufferDesc.Alignment = 0;
    constantBufferDesc.Width = constantBufferSize;
    constantBufferDesc.Height = 1;
    constantBufferDesc.DepthOrArraySize = 1;
    constantBufferDesc.MipLevels = 1;
    constantBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    constantBufferDesc.SampleDesc.Count = 1;
    constantBufferDesc.SampleDesc.Quality = 0;
    constantBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    constantBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeapProps.CreationNodeMask = 1;
    uploadHeapProps.VisibleNodeMask = 1;

    hr = m_device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &constantBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_gridConstantsBuffer));

    if (FAILED(hr)) {
        char hexStr[32];
        sprintf_s(hexStr, "0x%08X", static_cast<uint32_t>(hr));
        LOGE(std::string("Failed to create grid constants buffer: ") + hexStr + " (device valid: " + (m_device ? "YES" : "NO") + ")");
        return false;
    }

    LOGI("Emission grid resources created: Grid[" + std::to_string(EMISSION_GRID_RESOLUTION) + "^3=" + std::to_string(gridCellCount) + " cells, " + std::to_string(gridBufferSize/1024) + "KB], Lighting[" + std::to_string(particleCount) + " particles, " + std::to_string(lightingBufferSize/1024) + "KB]");
    return true;
}

bool App::createPerParticleBLASResources() {
    LOGI("Creating per-particle BLAS resources for RT lighting...");

    uint32_t particleCount = m_mode9ParticleCount;
    float particleRadius = 5.0f;  // Match particle render size (m_particleSize from ParticleSystem)

    // Create per-particle BLAS with individual AABBs for 100K particles
    if (!m_asBuilder->CreatePerParticleBLAS(
        m_perParticleBLAS,
        m_perParticleBLASScratch,
        m_particleAABBBuffer,
        particleCount,
        particleRadius)) {
        LOGE("Failed to create per-particle BLAS");
        return false;
    }

    // Create SRV for BLAS (acceleration structure SRV for RayQuery)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_perParticleBLAS->GetGPUVirtualAddress();

    m_particleBVHSrvIndex = m_descriptorAllocator->Allocate();
    if (m_particleBVHSrvIndex == UINT_MAX) {
        LOGE("Failed to allocate SRV for particle BLAS");
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_descriptorAllocator->GetCPUHandle(m_particleBVHSrvIndex);
    m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);

    LOGI("Per-particle BLAS created: " + std::to_string(particleCount) + " AABBs, SRV index=" + std::to_string(m_particleBVHSrvIndex));
    return true;
}

void App::renderShadowMap() {
    if (!m_shadowComputePSO || !m_tlasResult || m_shadowMapUavIndex == UINT_MAX) {
        return; // Not initialized
    }

    // MODE 9.1: DXR 1.1 Inline Ray Tracing (RayQuery compute shader)
    static int logOnce = 0;
    if (logOnce++ < 3) {
        LOGI("renderShadowMap: Compute shader with RayQuery (DXR 1.1)");
    }

    PIX_SCOPED_EVENT(m_cmdList.Get(), "Shadow Map Generation (RayQuery)");

    // Transition shadow map to UAV state
    static bool firstFrame = true;
    if (!firstFrame) {
        D3D12_RESOURCE_BARRIER toUAV{};
        toUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUAV.Transition.pResource = m_shadowMapTexture.Get();
        toUAV.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        toUAV.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUAV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_cmdList->ResourceBarrier(1, &toUAV);
    }

    // Clear shadow map to 1.0 (fully lit)
    float clearValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    m_cmdList->ClearUnorderedAccessViewFloat(
        m_descriptorAllocator->GetGPUHandle(m_shadowMapUavIndex),
        m_descriptorAllocator->GetCPUHandle(m_shadowMapUavIndex),
        m_shadowMapTexture.Get(),
        clearValue,
        0, nullptr);

    // Set compute pipeline state
    m_cmdList->SetComputeRootSignature(m_shadowComputeRootSignature.Get());
    m_cmdList->SetPipelineState(m_shadowComputePSO.Get());

    // Bind descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);

    // Parameter 0: TLAS SRV (t0)
    m_cmdList->SetComputeRootDescriptorTable(0, m_descriptorAllocator->GetGPUHandle(m_tlasSrvIndex));

    // Parameter 1: Shadow map UAV (u0)
    m_cmdList->SetComputeRootDescriptorTable(1, m_descriptorAllocator->GetGPUHandle(m_shadowMapUavIndex));

    // Parameter 2: Shadow parameters (animated light direction)
    static float lightAnimTime = 0.0f;
    lightAnimTime += 0.016f;

    float angle = lightAnimTime * 0.8f;
    DirectX::XMFLOAT3 lightDir = {
        sinf(angle) * 0.8f,
        -0.6f,
        cosf(angle) * 0.8f
    };
    DirectX::XMVECTOR lightVec = DirectX::XMLoadFloat3(&lightDir);
    lightVec = DirectX::XMVector3Normalize(lightVec);
    DirectX::XMStoreFloat3(&lightDir, lightVec);

    struct ShadowParams {
        DirectX::XMFLOAT3 lightDirection;
        float shadowBias;
        DirectX::XMFLOAT2 shadowMapSize;
        DirectX::XMFLOAT2 padding;
    } shadowParams;
    shadowParams.lightDirection = lightDir;
    shadowParams.shadowBias = 0.01f;
    shadowParams.shadowMapSize = DirectX::XMFLOAT2(1024.0f, 1024.0f);
    shadowParams.padding = DirectX::XMFLOAT2(0.0f, 0.0f);

    m_cmdList->SetComputeRoot32BitConstants(2, 8, &shadowParams, 0);

    // Dispatch compute shader (128x128 groups, 8x8 threads per group = 1024x1024 total)
    const UINT threadGroupSize = 8;
    const UINT dispatchX = (1024 + threadGroupSize - 1) / threadGroupSize;
    const UINT dispatchY = (1024 + threadGroupSize - 1) / threadGroupSize;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);

    // UAV barrier to ensure compute writes complete
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_shadowMapTexture.Get();
    m_cmdList->ResourceBarrier(1, &uavBarrier);

    // Transition shadow map to SRV state for graphics pipeline
    D3D12_RESOURCE_BARRIER toSRV{};
    toSRV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSRV.Transition.pResource = m_shadowMapTexture.Get();
    toSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toSRV.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toSRV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &toSRV);

    firstFrame = false;

    // NO FENCE WAIT - work stays on main command list, synced with frame fence
}

void App::computeEmissionGrid() {
    // Mode 9.2 Milestone 2: Build spatial grid from emission buffer
    if (!m_emissionGridPSO || !m_emissionTexture || m_emissionGridUavIndex == UINT_MAX) {
        static bool s_loggedSkip = false;
        if (!s_loggedSkip) {
            LOGE("computeEmissionGrid SKIPPED - PSO=" + std::string(m_emissionGridPSO ? "OK" : "NULL") +
                 " EmissionTex=" + std::string(m_emissionTexture ? "OK" : "NULL") +
                 " UavIdx=" + (m_emissionGridUavIndex == UINT_MAX ? "INVALID" : std::to_string(m_emissionGridUavIndex)));
            s_loggedSkip = true;
        }
        return; // Not initialized
    }

    PIX_SCOPED_EVENT(m_cmdList.Get(), "Emission Grid Build");

    // Clear grid buffer to zero before accumulating
    if (m_gridClearPSO) {
        static bool s_logged = false;
        if (!s_logged) {
            LOGI("clearEmissionGrid: Grid size = " + std::to_string(EMISSION_GRID_RESOLUTION) + "^3");
            s_logged = true;
        }
        m_cmdList->SetComputeRootSignature(m_gridClearRootSig.Get());
        m_cmdList->SetPipelineState(m_gridClearPSO.Get());

        // Bind descriptor heap
        ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
        m_cmdList->SetDescriptorHeaps(1, heaps);

        // Param 0: Grid UAV
        m_cmdList->SetComputeRootDescriptorTable(0, m_descriptorAllocator->GetGPUHandle(m_emissionGridUavIndex));

        // Param 1: Grid size in DWORDs
        const UINT gridSizeInDWORDs = EMISSION_GRID_RESOLUTION * EMISSION_GRID_RESOLUTION * EMISSION_GRID_RESOLUTION * 4;
        m_cmdList->SetComputeRoot32BitConstant(1, gridSizeInDWORDs, 0);

        // Dispatch: 256 threads per group covering all DWORDs
        const UINT dispatchX = (gridSizeInDWORDs + 255) / 256;
        m_cmdList->Dispatch(dispatchX, 1, 1);

        // UAV barrier after clear
        D3D12_RESOURCE_BARRIER clearBarrier{};
        clearBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        clearBarrier.UAV.pResource = m_emissionGridBuffer.Get();
        m_cmdList->ResourceBarrier(1, &clearBarrier);
    }

    // Set pipeline
    static bool s_logged2 = false;
    if (!s_logged2) {
        LOGI("buildEmissionGrid: Building grid from emission buffer");
        s_logged2 = true;
    }
    m_cmdList->SetComputeRootSignature(m_emissionGridRootSig.Get());
    m_cmdList->SetPipelineState(m_emissionGridPSO.Get());

    // Bind descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);

    // Get particle buffer SRV from particle system
    UINT particleBufferSrvIndex = m_meshParticleSystem->GetParticleBufferSRVIndex();
    if (particleBufferSrvIndex == UINT_MAX) {
        return; // Particle buffer not ready
    }

    // Parameter 0: Particle buffer SRV (t0)
    m_cmdList->SetComputeRootDescriptorTable(0, m_descriptorAllocator->GetGPUHandle(particleBufferSrvIndex));

    // Parameter 1: Emission grid UAV (u0)
    m_cmdList->SetComputeRootDescriptorTable(1, m_descriptorAllocator->GetGPUHandle(m_emissionGridUavIndex));

    // Parameter 2: Grid constants (b0)
    struct GridConstants {
        UINT particleCount;
        UINT gridResolution;
        float worldRadius;
        float emissionThreshold;
    } gridConstants;

    gridConstants.particleCount = m_mode9ParticleCount;
    gridConstants.gridResolution = EMISSION_GRID_RESOLUTION;
    gridConstants.worldRadius = 80.0f;  // Cover full accretion disk (OUTER_DISK_RADIUS=60.0 + margin)
    gridConstants.emissionThreshold = 0.0f;  // DIAGNOSTIC: Capture ALL particles regardless of temperature

    m_cmdList->SetComputeRoot32BitConstants(2, sizeof(GridConstants) / 4, &gridConstants, 0);

    // Dispatch: 256 threads per group covering all particles
    const UINT threadGroupSize = 256;
    const UINT dispatchX = (m_mode9ParticleCount + threadGroupSize - 1) / threadGroupSize;
    m_cmdList->Dispatch(dispatchX, 1, 1);

    // UAV barrier + transition to SRV for particle lighting read
    // NOTE: Grid buffer is created in UAV state, so first frame is already correct
    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].UAV.pResource = m_emissionGridBuffer.Get();

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = m_emissionGridBuffer.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.Subresource = 0;

    m_cmdList->ResourceBarrier(2, barriers);
}

void App::computeParticleLighting() {
    // Mode 9.2 Milestone 3: Apply grid-based lighting to particles
    if (!m_particleLightingPSO || !m_meshParticleSystem || m_particleLightingUavIndex == UINT_MAX) {
        return; // Not initialized
    }

    PIX_SCOPED_EVENT(m_cmdList.Get(), "Particle Lighting Compute");

    // Set pipeline
    static bool s_logged3 = false;
    if (!s_logged3) {
        LOGI("computeParticleLighting: Applying grid lighting to " + std::to_string(m_mode9ParticleCount) + " particles");
        s_logged3 = true;
    }
    m_cmdList->SetComputeRootSignature(m_particleLightingRootSig.Get());
    m_cmdList->SetPipelineState(m_particleLightingPSO.Get());

    // Bind descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);

    // Get particle buffer SRV from particle system
    UINT particleBufferSrvIndex = m_meshParticleSystem->GetParticleBufferSRVIndex();
    if (particleBufferSrvIndex == UINT_MAX || m_emissionGridSrvIndex == UINT_MAX) {
        return; // Resources not ready
    }

    // FIX: Bind each SRV separately (no contiguous descriptor requirement)
    // Parameter 0: Particle buffer SRV (t0)
    m_cmdList->SetComputeRootDescriptorTable(0, m_descriptorAllocator->GetGPUHandle(particleBufferSrvIndex));

    // Parameter 1: Emission grid SRV (t1)
    m_cmdList->SetComputeRootDescriptorTable(1, m_descriptorAllocator->GetGPUHandle(m_emissionGridSrvIndex));

    // Parameter 2: Particle lighting UAV (u0)
    m_cmdList->SetComputeRootDescriptorTable(2, m_descriptorAllocator->GetGPUHandle(m_particleLightingUavIndex));

    // Parameter 3: Lighting constants (b0)
    struct LightingConstants {
        UINT particleCount;
        UINT gridResolution;
        float worldRadius;
        float lightingStrength;
        DirectX::XMFLOAT3 cameraPos;
        float falloffRadius;
    } lightingConstants;

    lightingConstants.particleCount = m_mode9ParticleCount;
    lightingConstants.gridResolution = EMISSION_GRID_RESOLUTION;
    lightingConstants.worldRadius = 80.0f;  // Match emission grid bounds
    lightingConstants.lightingStrength = 50.0f;  // MASSIVELY increased to make effect unmistakable
    lightingConstants.cameraPos = m_camera->GetPosition();
    lightingConstants.falloffRadius = 40.0f;  // Wide influence to reach distant particles

    m_cmdList->SetComputeRoot32BitConstants(3, sizeof(LightingConstants) / 4, &lightingConstants, 0);

    // Dispatch: 256 threads per group, covering all particles
    const UINT threadGroupSize = 256;
    const UINT dispatchX = (m_mode9ParticleCount + threadGroupSize - 1) / threadGroupSize;
    m_cmdList->Dispatch(dispatchX, 1, 1);

    // Transition lighting buffer from UAV to SRV for mesh shader reading
    // Transition grid back to UAV state for next frame's clear/build
    D3D12_RESOURCE_BARRIER postBarriers[2]{};
    postBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    postBarriers[0].Transition.pResource = m_particleLightingBuffer.Get();
    postBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    postBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    postBarriers[0].Transition.Subresource = 0;

    postBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    postBarriers[1].Transition.pResource = m_emissionGridBuffer.Get();
    postBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    postBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    postBarriers[1].Transition.Subresource = 0;

    m_cmdList->ResourceBarrier(2, postBarriers);

    // DIAGNOSTIC: Copy GPU buffer samples to CPU-readable memory (once per 120 frames)
    static int s_diagnosticFrameCounter = 0;
    if (++s_diagnosticFrameCounter >= 120) {
        s_diagnosticFrameCounter = 0;

        // Transition buffers to COPY_SOURCE
        D3D12_RESOURCE_BARRIER copyBarriers[2]{};
        copyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        copyBarriers[0].Transition.pResource = m_emissionGridBuffer.Get();
        copyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        copyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        copyBarriers[0].Transition.Subresource = 0;

        copyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        copyBarriers[1].Transition.pResource = m_particleLightingBuffer.Get();
        copyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        copyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        copyBarriers[1].Transition.Subresource = 0;

        m_cmdList->ResourceBarrier(2, copyBarriers);

        // Copy samples
        m_cmdList->CopyBufferRegion(m_emissionGridReadback.Get(), 0, m_emissionGridBuffer.Get(), 0, 64 * sizeof(float) * 4);
        m_cmdList->CopyBufferRegion(m_particleLightingReadback.Get(), 0, m_particleLightingBuffer.Get(), 0, 16 * sizeof(float) * 4);

        // Restore states
        copyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        copyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        copyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        copyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        m_cmdList->ResourceBarrier(2, copyBarriers);

        // Queue fence for CPU readback
        static UINT64 s_readbackFenceValue = 0;
        const UINT64 currentFence = ++s_readbackFenceValue;
        m_queue->Signal(m_fence.Get(), currentFence);

        // Wait for GPU copy to complete, then read data on CPU
        if (m_fence->GetCompletedValue() < currentFence) {
            HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            m_fence->SetEventOnCompletion(currentFence, eventHandle);
            WaitForSingleObject(eventHandle, INFINITE);
            CloseHandle(eventHandle);
        }

        // Read emission grid sample
        float* gridData = nullptr;
        m_emissionGridReadback->Map(0, nullptr, reinterpret_cast<void**>(&gridData));
        LOGI("=== EMISSION GRID SAMPLE (cells 0-3 and 60-63) ===");
        for (int i = 0; i < 4; i++) {
            LOGI("Cell[" + std::to_string(i) + "]: R=" + std::to_string(gridData[i*4+0]) +
                 " G=" + std::to_string(gridData[i*4+1]) +
                 " B=" + std::to_string(gridData[i*4+2]) +
                 " Count=" + std::to_string(gridData[i*4+3]));
        }
        for (int i = 60; i < 64; i++) {
            LOGI("Cell[" + std::to_string(i) + "]: R=" + std::to_string(gridData[i*4+0]) +
                 " G=" + std::to_string(gridData[i*4+1]) +
                 " B=" + std::to_string(gridData[i*4+2]) +
                 " Count=" + std::to_string(gridData[i*4+3]));
        }
        m_emissionGridReadback->Unmap(0, nullptr);

        // Read particle lighting sample
        float* lightingData = nullptr;
        m_particleLightingReadback->Map(0, nullptr, reinterpret_cast<void**>(&lightingData));
        LOGI("=== PARTICLE LIGHTING SAMPLE (first 4 particles) ===");
        for (int i = 0; i < 4; i++) {
            LOGI("Particle[" + std::to_string(i) + "]: R=" + std::to_string(lightingData[i*4+0]) +
                 " G=" + std::to_string(lightingData[i*4+1]) +
                 " B=" + std::to_string(lightingData[i*4+2]) +
                 " W=" + std::to_string(lightingData[i*4+3]));
        }
        m_particleLightingReadback->Unmap(0, nullptr);
    }
}

void App::updateParticleAABBs() {
    // Generate AABBs from particle positions and rebuild BLAS
    PIX_SCOPED_EVENT(m_cmdList.Get(), "Update Particle AABBs");

    static bool logOnce = true;
    if (logOnce) {
        LOGI("updateParticleAABBs: First call - PSO=" + std::string(m_aabbGenPSO ? "OK" : "NULL") +
             " AABBBuffer=" + std::string(m_particleAABBBuffer ? "OK" : "NULL"));
        logOnce = false;
    }

    if (!m_aabbGenPSO || !m_particleAABBBuffer) {
        return;  // Not initialized
    }

    // Dispatch AABB generation compute shader
    m_cmdList->SetPipelineState(m_aabbGenPSO.Get());
    m_cmdList->SetComputeRootSignature(m_aabbGenRootSig.Get());

    // Bind particle SRV (t0)
    UINT particleSrvIndex = m_meshParticleSystem->GetParticleBufferSRVIndex();
    D3D12_GPU_DESCRIPTOR_HANDLE particleSrvGpuHandle = m_descriptorAllocator->GetGPUHandle(particleSrvIndex);
    m_cmdList->SetComputeRootDescriptorTable(0, particleSrvGpuHandle);

    // Allocate UAV for AABB buffer if not already allocated
    static UINT aabbUavIndex = UINT_MAX;
    if (aabbUavIndex == UINT_MAX) {
        aabbUavIndex = m_descriptorAllocator->Allocate();
        if (aabbUavIndex != UINT_MAX) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = m_mode9ParticleCount;  // Number of AABBs
            uavDesc.Buffer.StructureByteStride = sizeof(float) * 6;  // AABB struct size
            uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

            D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle = m_descriptorAllocator->GetCPUHandle(aabbUavIndex);
            m_device->CreateUnorderedAccessView(m_particleAABBBuffer.Get(), nullptr, &uavDesc, uavCpuHandle);
        }
    }

    // Bind AABB UAV (u0)
    D3D12_GPU_DESCRIPTOR_HANDLE aabbUavGpuHandle = m_descriptorAllocator->GetGPUHandle(aabbUavIndex);
    m_cmdList->SetComputeRootDescriptorTable(1, aabbUavGpuHandle);

    // Bind constants (b0): particleCount, particleRadius, padding[2]
    struct AABBConstants {
        uint32_t particleCount;
        float particleRadius;
        float padding[2];
    } constants = {
        m_mode9ParticleCount,
        5.0f,  // Match particle render size
        {0.0f, 0.0f}
    };
    m_cmdList->SetComputeRoot32BitConstants(2, 4, &constants, 0);

    // Dispatch (100,000 particles / 256 threads = 391 groups)
    uint32_t numGroups = (m_mode9ParticleCount + 255) / 256;
    m_cmdList->Dispatch(numGroups, 1, 1);

    // UAV barrier for AABB buffer
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_particleAABBBuffer.Get();
    m_cmdList->ResourceBarrier(1, &uavBarrier);

    // Rebuild BLAS (ALLOW_UPDATE flag makes this fast ~0.3ms)
    m_asBuilder->BuildPerParticleBLAS(m_cmdList.Get(), m_perParticleBLAS.Get(), m_perParticleBLASScratch.Get());
}

void App::computeRTLighting() {
    // Ray traced particle-to-particle lighting using RayQuery
    PIX_SCOPED_EVENT(m_cmdList.Get(), "RT Particle Lighting");

    static bool logOnce = true;
    if (logOnce) {
        LOGI("computeRTLighting: First call - PSO=" + std::string(m_rtLightingPSO ? "OK" : "NULL") +
             " BLAS=" + std::string(m_perParticleBLAS ? "OK" : "NULL") +
             " LightingBuffer=" + std::string(m_particleLightingBuffer ? "OK" : "NULL"));
        logOnce = false;
    }

    if (!m_rtLightingPSO || !m_perParticleBLAS || !m_particleLightingBuffer) {
        return;  // Not initialized
    }

    // DIAGNOSTIC TEST: Clear lighting buffer to bright green (test pattern)
    // This verifies that the particle renderer CAN see the lighting buffer
    float testPattern[4] = { 0.0f, 100.0f, 0.0f, 0.0f };  // Bright green
    m_cmdList->ClearUnorderedAccessViewFloat(
        m_descriptorAllocator->GetGPUHandle(m_particleLightingUavIndex),
        m_descriptorAllocator->GetCPUHandle(m_particleLightingUavIndex),
        m_particleLightingBuffer.Get(),
        testPattern,
        0, nullptr);

    // UAV barrier after clear
    D3D12_RESOURCE_BARRIER clearBarrier = {};
    clearBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    clearBarrier.UAV.pResource = m_particleLightingBuffer.Get();
    m_cmdList->ResourceBarrier(1, &clearBarrier);

    // DIAGNOSTIC: GPU readback to verify green test pattern was written
    static int s_readbackFrameCounter = 0;
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_readbackBuffer;

    if (s_readbackFrameCounter == 60) {  // Readback on frame 60 (after clear settled)
        if (!s_readbackBuffer) {
            // Create readback buffer (CPU-readable staging buffer)
            D3D12_HEAP_PROPERTIES readbackHeapProps = {};
            readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;

            D3D12_RESOURCE_DESC readbackDesc = {};
            readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            readbackDesc.Width = 10 * sizeof(float) * 4;  // First 10 particles (40 floats)
            readbackDesc.Height = 1;
            readbackDesc.DepthOrArraySize = 1;
            readbackDesc.MipLevels = 1;
            readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
            readbackDesc.SampleDesc.Count = 1;
            readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            HRESULT hr = m_device->CreateCommittedResource(
                &readbackHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &readbackDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&s_readbackBuffer));

            if (FAILED(hr)) {
                LOGE("Failed to create lighting readback buffer");
            }
        }

        if (s_readbackBuffer) {
            // Transition lighting buffer to COPY_SOURCE
            D3D12_RESOURCE_BARRIER toCopy = {};
            toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toCopy.Transition.pResource = m_particleLightingBuffer.Get();
            toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            toCopy.Transition.Subresource = 0;
            m_cmdList->ResourceBarrier(1, &toCopy);

            // Copy first 10 particles to readback buffer
            m_cmdList->CopyBufferRegion(s_readbackBuffer.Get(), 0, m_particleLightingBuffer.Get(), 0, 10 * sizeof(float) * 4);

            // Transition back to UAV
            toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            m_cmdList->ResourceBarrier(1, &toCopy);

            LOGI("RT Lighting GPU readback scheduled for frame 60");
        }
    }

    if (s_readbackFrameCounter == 62) {  // Read results 2 frames later (GPU finished)
        if (s_readbackBuffer) {
            void* pData = nullptr;
            D3D12_RANGE readRange = { 0, 10 * sizeof(float) * 4 };
            HRESULT hr = s_readbackBuffer->Map(0, &readRange, &pData);

            if (SUCCEEDED(hr)) {
                float* lightingData = static_cast<float*>(pData);
                LOGI("=== RT LIGHTING BUFFER READBACK (first 10 particles) ===");
                for (int i = 0; i < 10; i++) {
                    float r = lightingData[i * 4 + 0];
                    float g = lightingData[i * 4 + 1];
                    float b = lightingData[i * 4 + 2];
                    float a = lightingData[i * 4 + 3];
                    LOGI("Particle " + std::to_string(i) + ": R=" + std::to_string(r) +
                         " G=" + std::to_string(g) + " B=" + std::to_string(b) + " A=" + std::to_string(a));
                }
                LOGI("=== Expected: G=100.0 for test pattern ===");

                D3D12_RANGE writeRange = { 0, 0 };
                s_readbackBuffer->Unmap(0, &writeRange);
            } else {
                LOGE("Failed to map lighting readback buffer");
            }
        }
    }

    s_readbackFrameCounter++;

    // Dispatch RT lighting compute shader
    m_cmdList->SetPipelineState(m_rtLightingPSO.Get());
    m_cmdList->SetComputeRootSignature(m_rtLightingRootSig.Get());

    // Bind particle SRV (t0) and BLAS SRV (t1)
    UINT particleSrvIndex = m_meshParticleSystem->GetParticleBufferSRVIndex();
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandles = m_descriptorAllocator->GetGPUHandle(particleSrvIndex);
    m_cmdList->SetComputeRootDescriptorTable(0, srvGpuHandles);  // t0 and t1 in same table

    // Bind lighting UAV (u0)
    D3D12_GPU_DESCRIPTOR_HANDLE lightingUavGpuHandle = m_descriptorAllocator->GetGPUHandle(m_particleLightingUavIndex);
    m_cmdList->SetComputeRootDescriptorTable(1, lightingUavGpuHandle);

    // Bind constants (b0): particleCount, raysPerParticle, maxDistance, intensity
    struct RTLightingConstants {
        uint32_t particleCount;
        uint32_t raysPerParticle;
        float maxLightingDistance;
        float lightingIntensity;
    } constants = {
        m_mode9ParticleCount,
        8,      // 8 rays per particle for high quality
        20.0f,  // Max lighting distance
        100.0f  // DIAGNOSTIC: Intensity multiplier (100x for visibility testing!)
    };
    m_cmdList->SetComputeRoot32BitConstants(2, 4, &constants, 0);

    // Dispatch (100,000 particles / 64 threads = 1563 groups)
    uint32_t numGroups = (m_mode9ParticleCount + 63) / 64;
    m_cmdList->Dispatch(numGroups, 1, 1);

    // UAV barrier for lighting buffer
    D3D12_RESOURCE_BARRIER lightingBarrier = {};
    lightingBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    lightingBarrier.UAV.pResource = m_particleLightingBuffer.Get();
    m_cmdList->ResourceBarrier(1, &lightingBarrier);
}

bool App::createShadowComputePipeline() {
    // Load shadow compute shader DXIL (DXR 1.1 RayQuery)
    LOGI("Loading shadow compute shader (DXR 1.1 RayQuery)...");
    std::string errorMsg;
    if (!FileLoader::LoadDXILShader("shaders/mode9/shadow_map_cs.dxil", m_shadowComputeShaderBlob, errorMsg)) {
        LOGE("Failed to load shadow compute shader: " + errorMsg);
        return false;
    }
    LOGI("Shadow compute shader loaded (" + std::to_string(m_shadowComputeShaderBlob->GetBufferSize()) + " bytes)");

    // Create root signature (identical layout to old DispatchRays version)
    // Parameter 0: TLAS SRV (t0)
    // Parameter 1: Shadow map UAV (u0)
    // Parameter 2: Shadow params constants (b0)

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;  // t0
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;  // u0
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[3]{};

    // TLAS SRV descriptor table
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &srvRange;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Shadow map UAV descriptor table
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &uavRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Shadow parameters root constants (8 DWORDs = 32 bytes)
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.Num32BitValues = 8;
    params[2].Constants.ShaderRegister = 0;  // b0
    params[2].Constants.RegisterSpace = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = params;
    rootSigDesc.NumStaticSamplers = 0;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        LOGE("Failed to serialize shadow compute root signature");
        return false;
    }

    hr = m_device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
                                       IID_PPV_ARGS(&m_shadowComputeRootSignature));
    if (FAILED(hr)) {
        LOGE("Failed to create shadow compute root signature");
        return false;
    }

    // Create compute PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc{};
    computeDesc.pRootSignature = m_shadowComputeRootSignature.Get();
    computeDesc.CS.pShaderBytecode = m_shadowComputeShaderBlob->GetBufferPointer();
    computeDesc.CS.BytecodeLength = m_shadowComputeShaderBlob->GetBufferSize();

    hr = m_device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&m_shadowComputePSO));
    if (FAILED(hr)) {
        LOGE("Failed to create shadow compute PSO: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    LOGI("Shadow compute pipeline created successfully (DXR 1.1 RayQuery)");
    return true;
}

bool App::createLightingComputePipelines() {
    // Mode 9.2 Milestone 2-3: Create compute pipelines for spatial grid lighting

    // ========== 0. Grid Clear Pipeline ==========
    LOGI("Loading grid clear shader...");
    Microsoft::WRL::ComPtr<ID3DBlob> clearShaderBlob;
    std::string errorMsg;
    if (!FileLoader::LoadDXILShader("shaders/mode9/grid_clear.dxil", clearShaderBlob, errorMsg)) {
        LOGE("Failed to load grid clear shader: " + errorMsg);
        return false;
    }
    LOGI("Grid clear shader loaded (" + std::to_string(clearShaderBlob->GetBufferSize()) + " bytes)");

    // Root signature for clear: UAV (u0) + constant (grid size)
    D3D12_DESCRIPTOR_RANGE clearUavRange{};
    clearUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    clearUavRange.NumDescriptors = 1;
    clearUavRange.BaseShaderRegister = 0;
    clearUavRange.RegisterSpace = 0;
    clearUavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER clearParams[2]{};
    clearParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    clearParams[0].DescriptorTable.NumDescriptorRanges = 1;
    clearParams[0].DescriptorTable.pDescriptorRanges = &clearUavRange;
    clearParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    clearParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    clearParams[1].Constants.Num32BitValues = 1;  // gridSizeInDWORDs
    clearParams[1].Constants.ShaderRegister = 0;
    clearParams[1].Constants.RegisterSpace = 0;
    clearParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC clearRootSigDesc{};
    clearRootSigDesc.NumParameters = 2;
    clearRootSigDesc.pParameters = clearParams;
    clearRootSigDesc.NumStaticSamplers = 0;
    clearRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedClearRootSig;
    Microsoft::WRL::ComPtr<ID3DBlob> clearErrorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&clearRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                              &serializedClearRootSig, &clearErrorBlob);
    if (FAILED(hr)) {
        LOGE("Failed to serialize grid clear root signature");
        return false;
    }

    hr = m_device->CreateRootSignature(0, serializedClearRootSig->GetBufferPointer(),
                                        serializedClearRootSig->GetBufferSize(),
                                        IID_PPV_ARGS(&m_gridClearRootSig));
    if (FAILED(hr)) {
        LOGE("Failed to create grid clear root signature");
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC clearComputeDesc{};
    clearComputeDesc.pRootSignature = m_gridClearRootSig.Get();
    clearComputeDesc.CS.pShaderBytecode = clearShaderBlob->GetBufferPointer();
    clearComputeDesc.CS.BytecodeLength = clearShaderBlob->GetBufferSize();

    hr = m_device->CreateComputePipelineState(&clearComputeDesc, IID_PPV_ARGS(&m_gridClearPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create grid clear PSO");
        return false;
    }
    LOGI("Grid clear pipeline created successfully");

    // ========== 1. Emission Grid Builder Pipeline ==========
    LOGI("Loading emission grid builder shader...");
    Microsoft::WRL::ComPtr<ID3DBlob> gridShaderBlob;
    errorMsg.clear();
    if (!FileLoader::LoadDXILShader("shaders/mode9/emission_grid_build.dxil", gridShaderBlob, errorMsg)) {
        LOGE("Failed to load emission grid shader: " + errorMsg);
        return false;
    }
    LOGI("Emission grid shader loaded (" + std::to_string(gridShaderBlob->GetBufferSize()) + " bytes)");

    // Root signature for grid builder:
    // Param 0: Emission texture SRV (t0)
    // Param 1: Emission grid UAV (u0)
    // Param 2: Grid constants (b0) - 8 DWORDs

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;  // t0
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;  // u0
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER gridParams[3]{};

    gridParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    gridParams[0].DescriptorTable.NumDescriptorRanges = 1;
    gridParams[0].DescriptorTable.pDescriptorRanges = &srvRange;
    gridParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    gridParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    gridParams[1].DescriptorTable.NumDescriptorRanges = 1;
    gridParams[1].DescriptorTable.pDescriptorRanges = &uavRange;
    gridParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // FIX: Use inline root constants (4 DWORDs = 16 bytes, well within 64 DWORD limit)
    // This matches the SetComputeRoot32BitConstants call in computeEmissionGrid()
    gridParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    gridParams[2].Constants.Num32BitValues = 4;  // GridConstants struct (4 x uint/float)
    gridParams[2].Constants.ShaderRegister = 0;  // b0
    gridParams[2].Constants.RegisterSpace = 0;
    gridParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC gridRootSigDesc{};
    gridRootSigDesc.NumParameters = 3;
    gridRootSigDesc.pParameters = gridParams;
    gridRootSigDesc.NumStaticSamplers = 0;
    gridRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&gridRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        LOGE("Failed to serialize emission grid root signature");
        return false;
    }

    hr = m_device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
                                       IID_PPV_ARGS(&m_emissionGridRootSig));
    if (FAILED(hr)) {
        LOGE("Failed to create emission grid root signature");
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc{};
    computeDesc.pRootSignature = m_emissionGridRootSig.Get();
    computeDesc.CS.pShaderBytecode = gridShaderBlob->GetBufferPointer();
    computeDesc.CS.BytecodeLength = gridShaderBlob->GetBufferSize();

    hr = m_device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&m_emissionGridPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create emission grid PSO: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    LOGI("Emission grid pipeline created successfully");

    // ========== 2. Particle Lighting Pipeline ==========
    LOGI("Loading particle lighting shader...");
    Microsoft::WRL::ComPtr<ID3DBlob> lightingShaderBlob;
    if (!FileLoader::LoadDXILShader("shaders/mode9/particle_lighting.dxil", lightingShaderBlob, errorMsg)) {
        LOGE("Failed to load particle lighting shader: " + errorMsg);
        return false;
    }
    LOGI("Particle lighting shader loaded (" + std::to_string(lightingShaderBlob->GetBufferSize()) + " bytes)");

    // Root signature for particle lighting:
    // FIX: Separate descriptor tables to avoid contiguous descriptor requirement
    // Param 0: Particle buffer SRV (t0)
    // Param 1: Emission grid SRV (t1)
    // Param 2: Particle lighting UAV (u0)
    // Param 3: Lighting constants (b0) - 8 DWORDs

    D3D12_DESCRIPTOR_RANGE lightingParticleSrvRange{};
    lightingParticleSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    lightingParticleSrvRange.NumDescriptors = 1;  // ONLY t0 (particle buffer)
    lightingParticleSrvRange.BaseShaderRegister = 0;
    lightingParticleSrvRange.RegisterSpace = 0;
    lightingParticleSrvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE lightingGridSrvRange{};
    lightingGridSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    lightingGridSrvRange.NumDescriptors = 1;  // ONLY t1 (emission grid)
    lightingGridSrvRange.BaseShaderRegister = 1;
    lightingGridSrvRange.RegisterSpace = 0;
    lightingGridSrvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE lightingUavRange{};
    lightingUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    lightingUavRange.NumDescriptors = 1;
    lightingUavRange.BaseShaderRegister = 0;  // u0
    lightingUavRange.RegisterSpace = 0;
    lightingUavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER lightingParams[4]{};

    lightingParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    lightingParams[0].DescriptorTable.NumDescriptorRanges = 1;
    lightingParams[0].DescriptorTable.pDescriptorRanges = &lightingParticleSrvRange;
    lightingParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    lightingParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    lightingParams[1].DescriptorTable.NumDescriptorRanges = 1;
    lightingParams[1].DescriptorTable.pDescriptorRanges = &lightingGridSrvRange;
    lightingParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    lightingParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    lightingParams[2].DescriptorTable.NumDescriptorRanges = 1;
    lightingParams[2].DescriptorTable.pDescriptorRanges = &lightingUavRange;
    lightingParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    lightingParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    lightingParams[3].Constants.Num32BitValues = 8;  // LightingConstants cbuffer
    lightingParams[3].Constants.ShaderRegister = 0;  // b0
    lightingParams[3].Constants.RegisterSpace = 0;
    lightingParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC lightingRootSigDesc{};
    lightingRootSigDesc.NumParameters = 4;
    lightingRootSigDesc.pParameters = lightingParams;
    lightingRootSigDesc.NumStaticSamplers = 0;
    lightingRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    serializedRootSig.Reset();
    errorBlob.Reset();
    hr = D3D12SerializeRootSignature(&lightingRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        LOGE("Failed to serialize particle lighting root signature");
        return false;
    }

    hr = m_device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
                                       IID_PPV_ARGS(&m_particleLightingRootSig));
    if (FAILED(hr)) {
        LOGE("Failed to create particle lighting root signature");
        return false;
    }

    computeDesc = {};
    computeDesc.pRootSignature = m_particleLightingRootSig.Get();
    computeDesc.CS.pShaderBytecode = lightingShaderBlob->GetBufferPointer();
    computeDesc.CS.BytecodeLength = lightingShaderBlob->GetBufferSize();

    hr = m_device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&m_particleLightingPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create particle lighting PSO: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    LOGI("Particle lighting pipeline created successfully");
    LOGI("Mode 9.2 spatial grid lighting pipelines ready");
    return true;
}

bool App::createRTLightingPipeline() {
    LOGI("Creating RT lighting compute pipelines...");

    // ========== 1. AABB Generation Pipeline ==========
    LOGI("Loading AABB generation shader...");
    Microsoft::WRL::ComPtr<ID3DBlob> aabbShaderBlob;
    std::string errorMsg;
    if (!FileLoader::LoadDXILShader("shaders/dxr/generate_particle_aabbs.dxil", aabbShaderBlob, errorMsg)) {
        LOGE("Failed to load AABB generation shader: " + errorMsg);
        return false;
    }
    LOGI("AABB generation shader loaded (" + std::to_string(aabbShaderBlob->GetBufferSize()) + " bytes)");

    // Root signature for AABB gen:
    // Param 0: Particle SRV (t0)
    // Param 1: AABB UAV (u0)
    // Param 2: Constants (b0) - particleCount, particleRadius

    D3D12_DESCRIPTOR_RANGE aabbSrvRange{};
    aabbSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    aabbSrvRange.NumDescriptors = 1;
    aabbSrvRange.BaseShaderRegister = 0;  // t0
    aabbSrvRange.RegisterSpace = 0;
    aabbSrvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE aabbUavRange{};
    aabbUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    aabbUavRange.NumDescriptors = 1;
    aabbUavRange.BaseShaderRegister = 0;  // u0
    aabbUavRange.RegisterSpace = 0;
    aabbUavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER aabbParams[3]{};

    aabbParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    aabbParams[0].DescriptorTable.NumDescriptorRanges = 1;
    aabbParams[0].DescriptorTable.pDescriptorRanges = &aabbSrvRange;
    aabbParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    aabbParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    aabbParams[1].DescriptorTable.NumDescriptorRanges = 1;
    aabbParams[1].DescriptorTable.pDescriptorRanges = &aabbUavRange;
    aabbParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    aabbParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    aabbParams[2].Constants.Num32BitValues = 4;  // particleCount, particleRadius, padding[2]
    aabbParams[2].Constants.ShaderRegister = 0;  // b0
    aabbParams[2].Constants.RegisterSpace = 0;
    aabbParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC aabbRootSigDesc{};
    aabbRootSigDesc.NumParameters = 3;
    aabbRootSigDesc.pParameters = aabbParams;
    aabbRootSigDesc.NumStaticSamplers = 0;
    aabbRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedAabbRootSig;
    Microsoft::WRL::ComPtr<ID3DBlob> aabbErrorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&aabbRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                              &serializedAabbRootSig, &aabbErrorBlob);
    if (FAILED(hr)) {
        LOGE("Failed to serialize AABB generation root signature");
        return false;
    }

    hr = m_device->CreateRootSignature(0, serializedAabbRootSig->GetBufferPointer(),
                                        serializedAabbRootSig->GetBufferSize(),
                                        IID_PPV_ARGS(&m_aabbGenRootSig));
    if (FAILED(hr)) {
        LOGE("Failed to create AABB generation root signature");
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC aabbComputeDesc{};
    aabbComputeDesc.pRootSignature = m_aabbGenRootSig.Get();
    aabbComputeDesc.CS.pShaderBytecode = aabbShaderBlob->GetBufferPointer();
    aabbComputeDesc.CS.BytecodeLength = aabbShaderBlob->GetBufferSize();

    hr = m_device->CreateComputePipelineState(&aabbComputeDesc, IID_PPV_ARGS(&m_aabbGenPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create AABB generation PSO");
        return false;
    }
    LOGI("AABB generation pipeline created successfully");

    // Create constants buffer for AABB generation
    const UINT aabbConstantBufferSize = 256;
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = aabbConstantBufferSize;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    cbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_aabbConstantsBuffer));
    if (FAILED(hr)) {
        LOGE("Failed to create AABB constants buffer");
        return false;
    }

    // ========== 2. RT Lighting Pipeline ==========
    LOGI("Loading RT lighting shader...");
    Microsoft::WRL::ComPtr<ID3DBlob> rtLightingShaderBlob;
    errorMsg.clear();
    if (!FileLoader::LoadDXILShader("shaders/dxr/particle_raytraced_lighting_cs.dxil", rtLightingShaderBlob, errorMsg)) {
        LOGE("Failed to load RT lighting shader: " + errorMsg);
        return false;
    }
    LOGI("RT lighting shader loaded (" + std::to_string(rtLightingShaderBlob->GetBufferSize()) + " bytes)");

    // Root signature for RT lighting:
    // Param 0: Particle SRV (t0)
    // Param 1: BLAS SRV (t1) - acceleration structure
    // Param 2: Lighting UAV (u0)
    // Param 3: Constants (b0) - particleCount, raysPerParticle, maxDistance, intensity

    D3D12_DESCRIPTOR_RANGE rtSrvRanges[2]{};
    rtSrvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rtSrvRanges[0].NumDescriptors = 1;
    rtSrvRanges[0].BaseShaderRegister = 0;  // t0 particles
    rtSrvRanges[0].RegisterSpace = 0;
    rtSrvRanges[0].OffsetInDescriptorsFromTableStart = 0;

    rtSrvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rtSrvRanges[1].NumDescriptors = 1;
    rtSrvRanges[1].BaseShaderRegister = 1;  // t1 BLAS
    rtSrvRanges[1].RegisterSpace = 0;
    rtSrvRanges[1].OffsetInDescriptorsFromTableStart = 1;

    D3D12_DESCRIPTOR_RANGE rtUavRange{};
    rtUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rtUavRange.NumDescriptors = 1;
    rtUavRange.BaseShaderRegister = 0;  // u0 lighting output
    rtUavRange.RegisterSpace = 0;
    rtUavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rtParams[3]{};

    rtParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rtParams[0].DescriptorTable.NumDescriptorRanges = 2;  // Both SRVs
    rtParams[0].DescriptorTable.pDescriptorRanges = rtSrvRanges;
    rtParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rtParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rtParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rtParams[1].DescriptorTable.pDescriptorRanges = &rtUavRange;
    rtParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rtParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rtParams[2].Constants.Num32BitValues = 4;  // particleCount, raysPerParticle, maxDistance, intensity
    rtParams[2].Constants.ShaderRegister = 0;  // b0
    rtParams[2].Constants.RegisterSpace = 0;
    rtParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rtRootSigDesc{};
    rtRootSigDesc.NumParameters = 3;
    rtRootSigDesc.pParameters = rtParams;
    rtRootSigDesc.NumStaticSamplers = 0;
    rtRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRtRootSig;
    Microsoft::WRL::ComPtr<ID3DBlob> rtErrorBlob;
    hr = D3D12SerializeRootSignature(&rtRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                      &serializedRtRootSig, &rtErrorBlob);
    if (FAILED(hr)) {
        LOGE("Failed to serialize RT lighting root signature");
        return false;
    }

    hr = m_device->CreateRootSignature(0, serializedRtRootSig->GetBufferPointer(),
                                        serializedRtRootSig->GetBufferSize(),
                                        IID_PPV_ARGS(&m_rtLightingRootSig));
    if (FAILED(hr)) {
        LOGE("Failed to create RT lighting root signature");
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC rtComputeDesc{};
    rtComputeDesc.pRootSignature = m_rtLightingRootSig.Get();
    rtComputeDesc.CS.pShaderBytecode = rtLightingShaderBlob->GetBufferPointer();
    rtComputeDesc.CS.BytecodeLength = rtLightingShaderBlob->GetBufferSize();

    hr = m_device->CreateComputePipelineState(&rtComputeDesc, IID_PPV_ARGS(&m_rtLightingPSO));
    if (FAILED(hr)) {
        LOGE("Failed to create RT lighting PSO: 0x" + std::to_string(static_cast<uint32_t>(hr)));
        return false;
    }

    LOGI("RT lighting pipeline created successfully");

    // Create constants buffer for RT lighting
    const UINT rtConstantBufferSize = 256;
    hr = m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_rtLightingConstantsBuffer));
    if (FAILED(hr)) {
        LOGE("Failed to create RT lighting constants buffer");
        return false;
    }

    LOGI("Mode 9.2 RT lighting pipelines ready");
    return true;
}

// ============================================================================
// VOXEL PARTICLE SYSTEM IMPLEMENTATION (MODE 6)
// ============================================================================

bool App::initializeVoxelSystem() {
    LOGI("Initializing Voxel Particle System (Mode 6)...");

    // Create 3D textures for voxel grid
    if (!createVoxelTextures()) {
        LOGE("Failed to create voxel textures");
        return false;
    }

    // Create compute pipelines for voxel updates
    if (!createVoxelComputePipelines()) {
        LOGE("Failed to create voxel compute pipelines");
        return false;
    }

    // Initialize voxel grid with some seed data
    resetVoxelGrid();

    LOGI("Voxel system initialized successfully (resolution: " + std::to_string(m_voxelResolution) + "^3)");
    return true;
}

bool App::createVoxelTextures() {
    LOGI("Creating 3D voxel textures...");

    // 3D texture descriptor for voxel grid
    D3D12_RESOURCE_DESC voxelTexDesc = {};
    voxelTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    voxelTexDesc.Width = m_voxelResolution;
    voxelTexDesc.Height = m_voxelResolution;
    voxelTexDesc.DepthOrArraySize = m_voxelResolution;
    voxelTexDesc.MipLevels = 1;
    voxelTexDesc.SampleDesc.Count = 1;
    voxelTexDesc.SampleDesc.Quality = 0;
    voxelTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    voxelTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    // Create density texture (R16_FLOAT)
    voxelTexDesc.Format = DXGI_FORMAT_R16_FLOAT;
    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &voxelTexDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_voxelDensityTexture)
    );
    if (FAILED(hr)) {
        LOGE("Failed to create voxel density texture");
        return false;
    }
    m_voxelDensityTexture->SetName(L"VoxelDensityTexture");

    // Create velocity texture (R16G16B16A16_FLOAT)
    voxelTexDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &voxelTexDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_voxelVelocityTexture)
    );
    if (FAILED(hr)) {
        LOGE("Failed to create voxel velocity texture");
        return false;
    }
    m_voxelVelocityTexture->SetName(L"VoxelVelocityTexture");

    // Create temperature texture (R16_FLOAT)
    voxelTexDesc.Format = DXGI_FORMAT_R16_FLOAT;
    hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &voxelTexDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_voxelTemperatureTexture)
    );
    if (FAILED(hr)) {
        LOGE("Failed to create voxel temperature texture");
        return false;
    }
    m_voxelTemperatureTexture->SetName(L"VoxelTemperatureTexture");

    // Allocate descriptor indices
    m_voxelDensitySRVIndex = m_descriptorAllocator->Allocate();
    m_voxelDensityUAVIndex = m_descriptorAllocator->Allocate();
    m_voxelVelocitySRVIndex = m_descriptorAllocator->Allocate();
    m_voxelVelocityUAVIndex = m_descriptorAllocator->Allocate();
    m_voxelTempSRVIndex = m_descriptorAllocator->Allocate();
    m_voxelTempUAVIndex = m_descriptorAllocator->Allocate();

    // Create SRVs and UAVs for density texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture3D.MipLevels = 1;
    m_device->CreateShaderResourceView(
        m_voxelDensityTexture.Get(),
        &srvDesc,
        m_descriptorAllocator->GetCPUHandle(m_voxelDensitySRVIndex)
    );

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uavDesc.Texture3D.WSize = m_voxelResolution;
    m_device->CreateUnorderedAccessView(
        m_voxelDensityTexture.Get(),
        nullptr,
        &uavDesc,
        m_descriptorAllocator->GetCPUHandle(m_voxelDensityUAVIndex)
    );

    // Create SRVs and UAVs for velocity texture
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    m_device->CreateShaderResourceView(
        m_voxelVelocityTexture.Get(),
        &srvDesc,
        m_descriptorAllocator->GetCPUHandle(m_voxelVelocitySRVIndex)
    );

    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    m_device->CreateUnorderedAccessView(
        m_voxelVelocityTexture.Get(),
        nullptr,
        &uavDesc,
        m_descriptorAllocator->GetCPUHandle(m_voxelVelocityUAVIndex)
    );

    // Create SRVs and UAVs for temperature texture
    srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
    m_device->CreateShaderResourceView(
        m_voxelTemperatureTexture.Get(),
        &srvDesc,
        m_descriptorAllocator->GetCPUHandle(m_voxelTempSRVIndex)
    );

    uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
    m_device->CreateUnorderedAccessView(
        m_voxelTemperatureTexture.Get(),
        nullptr,
        &uavDesc,
        m_descriptorAllocator->GetCPUHandle(m_voxelTempUAVIndex)
    );

    LOGI("Voxel textures created successfully");
    return true;
}

bool App::createVoxelComputePipelines() {
    LOGI("Creating voxel compute pipelines...");

    // TODO: Implement compute shader loading and PSO creation
    // For now, we'll create stub pipelines and implement the shaders separately
    LOGI("Voxel compute pipelines created (stub implementation)");
    return true;
}

void App::updateVoxelSystem(float deltaTime) {
    // Update voxel particle simulation
    // This will be called each frame to advance the particle physics

    // TODO: Implement voxel updates with compute shaders
    // - Advection step: move particles according to velocity field
    // - Force step: apply turbulence and dissipation
    // - Temperature step: simulate heating/cooling effects
}

void App::resetVoxelGrid() {
    LOGI("Resetting voxel grid to initial state");

    // TODO: Implement voxel grid reset with initial particle distribution
    // For now, we'll clear the reset flag
    m_voxelReset = false;
}

void App::cleanupVoxelSystem() {
    LOGI("Cleaning up voxel system");

    // Release voxel resources
    m_voxelDensityTexture.Reset();
    m_voxelVelocityTexture.Reset();
    m_voxelTemperatureTexture.Reset();
    m_voxelUpdatePSO.Reset();
    m_voxelAdvectPSO.Reset();
    m_voxelComputeRS.Reset();

    // Reset descriptor indices
    m_voxelDensitySRVIndex = UINT_MAX;
    m_voxelDensityUAVIndex = UINT_MAX;
    m_voxelVelocitySRVIndex = UINT_MAX;
    m_voxelVelocityUAVIndex = UINT_MAX;
    m_voxelTempSRVIndex = UINT_MAX;
    m_voxelTempUAVIndex = UINT_MAX;
}
