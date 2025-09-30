# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 🚨 Critical Directory Context
**ALWAYS verify you are in `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX`**, not PlasmaVulkan.
PlasmaVulkan has been shelved. Claude Code may default to wrong directory - verify at session start.

## Project Overview
PlasmaDX: Real-time ray-traced volumetric plasma renderer using DirectX 12 + DXR 1.2 (with SER support).
Core feature: **Ray-traced lighting must remain functional across all modes**.

## Build Commands

### From Windows (MSBuild)
```bash
# Initial CMake configuration (one-time or when CMakeLists.txt changes)
cmake -S . -B build-vs2022 -G "Visual Studio 17 2022" -A x64

# Build entire project (compiles shaders automatically)
"/mnt/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  "D:\Users\dilli\AndroidStudioProjects\PlasmaDX\build-vs2022\PlasmaDX.sln" \
  /p:Configuration=Debug /p:Platform=x64 /m /nologo

# Run executable
./build-vs2022/Debug/PlasmaDX.exe
```

### From WSL (shader compilation only)
```bash
# Compile individual shaders with local DXC
cd /mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX

# Compute shaders (CS 6.5)
./tools/dxc.exe -T cs_6_5 -E main shaders/particles/particle_physics.hlsl -Fo shaders/particles/particle_physics.dxil

# Mesh shaders (MS 6.5)
./tools/dxc.exe -T ms_6_5 -E main shaders/particles/particle_mesh.hlsl -Fo shaders/particles/particle_mesh.dxil

# Pixel shaders (PS 6.5)
./tools/dxc.exe -T ps_6_5 -E PSMain shaders/particles/particle_mesh.hlsl -Fo shaders/particles/particle_pixel.dxil

# DXR library shaders (lib_6_3)
./tools/dxc.exe -T lib_6_3 shaders/dxr/raytracing_lib.hlsl -Fo shaders/dxr/raytracing_lib.dxil
```

**Note**: Application must be built with MSVC on Windows. WSL is for shader compilation only.

## Architecture

### Demo Mode System
The application has 9 demo modes (switchable with F6 + number keys):

1. **SphereRT**: Pure DXR sphere baseline
2. **TorchlightDemo**: Interactive torch control
3. **VolumetricDemo**: Compact volumetric with moving elements
4. **VolumetricSculpture**: Static volumetric shape with sweeping RT lighting
5. **PlasmaAccretion**: Orbital plasma accretion disk with volumetric self-shadowing
6. **VoxelParticles**: Voxel-based particle simulation
7. **MetaballSPH**: SPH metaball physics with density field rendering
8. **DXR12Test**: DXR 1.2 feature testing (SER, OMM, pipeline validation)
9. **AccretionMeshParticles**: NASA-quality accretion disk with 100K mesh shader particles

Mode selection is in `App::WndProc()` (F6 + number), mode-specific rendering in `App::renderFrame()`.

### Core Rendering Pipeline Architecture

**DXR Path** (Modes 1-2, 8):
```
App::renderFrame() → App::renderFrameDXR()
  → ASBuilder (BLAS/TLAS)
  → Pipeline (DXR subobjects)
  → SBT (Shader Binding Table)
  → DispatchRays() → HDR texture (UAV)
  → Composite (HDR → backbuffer)
```

**Volumetric Path** (Modes 3-7):
```
App::renderFrame()
  → Particles::Update() [compute shader: particle physics]
  → DensityVolume::Fill() [compute shader: density grid from particles]
  → RayMarcher::Render() [compute shader: volumetric ray march → HDR]
  → DXR lighting (optional additive blend)
  → Composite (HDR → backbuffer)
```

**Mesh Shader Path** (Mode 9):
```
App::renderFrame() (AccretionMeshParticles branch)
  → ParticleSystem::UpdatePhysics() [compute shader: curl noise, gravity, constraints]
  → ParticleSystem::RenderParticles() [mesh shader + pixel shader: direct to backbuffer]
  → No DXR, no volumetrics (isolated fast path)
```

### Key Architectural Patterns

**Resource Ownership**:
- `App` owns: device, swapchain, command objects, global descriptor heaps
- Subsystems (ASBuilder, Pipeline, SBT, Particles, DensityVolume, RayMarcher, ParticleSystem) are created/destroyed per mode
- Use `std::unique_ptr` for subsystems; `ComPtr` for D3D12 resources

**Descriptor Management**:
- `DescriptorHeap` class provides global SRV/UAV/CBV allocation (index-based)
- HDR texture, TLAS, density volumes all allocate descriptors from shared heap
- Shader binding uses descriptor table indexing (not raw descriptors)

**Shader Constants**:
- DXR uses root constants (`GlobalParams`) - 128 bytes max
- Volumetrics use structured buffers mapped with `Map()`/`Unmap()`
- Mesh shaders use constant buffers (CBVs) uploaded per frame

**State Transitions**:
- HDR texture: `D3D12_RESOURCE_STATE_UNORDERED_ACCESS` (DXR/compute write) → `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE` (composite read)
- Backbuffer: `D3D12_RESOURCE_STATE_PRESENT` → `D3D12_RESOURCE_STATE_RENDER_TARGET` → back to `PRESENT`
- Always use `CD3DX12_RESOURCE_BARRIER` helpers

**PIX Integration**:
- `PIX_SCOPED_EVENT(cmdList, "EventName")` for GPU profiling
- Enabled in Debug builds, no-op in Release
- Use for major pipeline stages: AS build, SBT setup, DispatchRays, compute dispatches

### Directory Structure
```
src/
  core/       - App, Camera, D3D12 device/swapchain setup
  dxr/        - ASBuilder, Pipeline (subobjects), SBT (shader records)
  renderer/   - Composite (HDR → backbuffer tonemapping)
  utils/      - FileLoader, DescriptorHeap, Logger, Env
  volumetric/ - Particles, DensityVolume, RayMarcher, MetaballSystem
  particles/  - ParticleSystem (mesh shader-based, Mode 9 only)

shaders/
  dxr/        - raytracing_lib.hlsl (raygen/miss/closesthit)
  vol/        - compute shaders (particles_update, density_fill, ray_march_cs, etc.)
  particles/  - particle_physics.hlsl (compute), particle_mesh.hlsl (mesh+pixel)
```

### Runtime Controls (Keyboard)

**Global**:
- `F6 + [1-9]`: Switch demo mode
- `WASD`: Camera movement
- `Q/E`: Camera up/down
- `Ctrl+WASD`: Camera rotation (yaw/pitch) - no movement
- `F1`: Toggle debug layer verbose
- `F2`: Save log to timestamped file
- `F5`: Toggle SER (Shader Execution Reordering) for DXR 1.2

**Mode 9 (AccretionMeshParticles) Specific**:
- `G/H`: Adjust gravity (±50)
- `J/K`: Adjust turbulence (±1.0)
- `Z/X`: Adjust damping (±0.01)
- `V/B`: Adjust angular momentum (±0.1)
- `N/M`: Adjust particle size (±0.5)
- `,/.`: Adjust color temperature offset (±100K)
- `[/]`: Adjust color temperature scale (±0.1)
- `C`: Cycle constraint shape (NONE/SPHERE/DISC/TORUS/ACCRETION_DISK)
- `R`: Reset particles

**Other Modes**:
- `C`: Cycle light colors (torchlight/raymarcher)
- `B`: Toggle DXR additive blend
- `N/M`: Adjust DXR blend scale
- `+/-`: Adjust exposure (volumetric modes)
- See `.cursor/rules` for mode-specific controls (plasma accretion, metaballs, etc.)

### Runtime Environment Variables
Set before launching `PlasmaDX.exe`:
- `PLASMADX_DISABLE_DXR`: "1" to disable DXR initialization
- `PLASMADX_DEBUG_MODE`: Mode number (1-9) to auto-start
- `PLASMADX_TORCHLIGHT_DEMO`: "1" for torchlight-specific controls
- `PLASMADX_NO_QUIT_ON_REMOVAL`: "1" keep app alive on device removal

### DXR 1.2 Features (RTX 40 series)
Detected at runtime in `App::checkAdvancedDXRFeatures()`:
- **SER** (Shader Execution Reordering): Improves ray coherence
- **OMM** (Opacity Micromaps): Alpha-tested geometry optimization
- **DMM** (Displacement Micromaps): Tessellation optimization
- Hardware detection: Ada Lovelace (RTX 40), Ampere (RTX 30), Turing (RTX 20)

See `DXRFeatures` and `SERConfig` structs in `App.h`.

## Development Guidelines

### When Adding Features
- Maintain DXR lighting path functionality (it's the core feature)
- Add PIX events for new GPU work (`PIX_SCOPED_EVENT`)
- Test resize safety (recreate size-dependent resources)
- Log major operations with `LOGI()`/`LOGE()` (see `utils/Logger.h`)
- Use `CD3DX12_*` helpers from `d3dx12.h` (already included)

### Shader Development
- Always compile to `.dxil` (DXIL bytecode, not DXBC)
- Use shader model 6.0+ for compute, 6.3+ for DXR, 6.5+ for mesh shaders
- Test with PIX shader debugger (add `/Zi` to DXC for debug info)
- Match HLSL struct layouts **exactly** with C++ (padding, alignment)

### Common Pitfalls
- **Root signature mismatches**: Verify parameter slots match HLSL register bindings
- **Descriptor heap size**: Ensure enough space for all SRVs/UAVs/CBVs
- **Resource state transitions**: Always transition before access (UAV→SRV, RENDER_TARGET→PRESENT)
- **SBT alignment**: Use `D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT` (32 bytes)
- **Missing includes**: Add `#include <algorithm>` for `std::clamp`, `std::max`, etc.

### Debugging Tools
- **PIX for Windows**: GPU captures, timeline, resource inspection
- **D3D12 Debug Layer**: Enable in Debug builds, check `ID3D12InfoQueue`
- **NSight Graphics** (NVIDIA): Alternative GPU debugger
- **RenderDoc**: Cross-platform GPU debugger (limited DXR support)

### Agility SDK
- Located in `bin/D3D12/` (copied to exe directory post-build)
- Exports defined in `src/core/D3D12AgilitySDK.cpp`
- Provides latest DXR 1.2 features on older Windows versions

## Additional References
- Project roadmap: `PROJECT_ROADMAP_DXR_VOLUME_2025_09_15.md`
- DXR 1.2 upgrade guide: `DXR_12_UPGRADE_GUIDE.md`
- Agent guides (DXR techniques): `agent/DXR_*.md`
- Session summaries: `SESSION_SUMMARY_*.md` files
- See `.cursor/rules` for short-form context and session seed

For detailed feature discussions, consult `cursor_project_architect.md` (comprehensive session history).
