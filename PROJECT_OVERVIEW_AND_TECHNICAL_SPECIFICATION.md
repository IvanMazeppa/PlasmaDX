# PlasmaDX: Real-Time Ray-Traced Volumetric Plasma/Gas Simulation

## 🎯 Project Vision

PlasmaDX is a **real-time volumetric fluid dynamics and plasma simulation engine** built from the ground up using **DirectX 12 + DirectX Raytracing (DXR)**. This project recreates and enhances the capabilities of our previous PlasmaVulkan engine, leveraging the more accessible DXR API to achieve real-time ray-traced volumetric rendering with advanced lighting and shadowing.

### Core Concept
Simulate and render **dynamic particle-based fluid systems** that generate **3D volumetric density fields**, which are then **ray-marched in real-time** to produce realistic plasma, gas, and fluid visualizations with **physically-based lighting, scattering, and self-shadowing**.

## 🌟 End Product Vision

### Visual Modes and Simulations
1. **Plasma/Gas Accretion Disc** - Swirling matter orbiting around a gravitational center (black hole simulation)
2. **Confinement Systems** - Plasma contained within geometric constraints (spheres, torus, custom meshes)
3. **Free-form Gas Expansion** - Gas clouds spreading from emission points with turbulence
4. **Dual-Galaxy Systems** - Multiple gravitational bodies with particle interactions
5. **Relativistic Jets** - High-energy particle streams (astrophysical simulation)

### Real-Time Controls and Parameters
- **Physics**: Gravity strength, angular momentum, turbulence, damping, particle density
- **Rendering**: Emission intensity, opacity, color temperature, scattering parameters
- **Quality**: Volumetric resolution, ray marching steps, temporal anti-aliasing
- **Visualization**: Color palettes (temperature-based), debug overlays, density slices

### Visual Quality Features
- **HDR Volumetric Rendering** with tone mapping and bloom
- **Physically-Based Scattering** (Henyey-Greenstein phase function)
- **Beer-Lambert Absorption** with temperature-based emission
- **Real-Time Ray-Traced Shadows** (self-shadowing and external occluders)
- **Temporal Anti-Aliasing (TAA)** for smooth animation
- **Empty-Space Skipping** via hierarchical acceleration structures

## 🔧 Technical Architecture

### Core Technologies
- **DirectX 12** (modern D3D12 API with Agility SDK)
- **DirectX Raytracing (DXR)** for volumetric ray marching and shadows
- **HLSL Shader Model 6.x** with DXIL compilation
- **PIX for Windows** for GPU debugging and profiling
- **C++20** with RAII patterns and modern practices

### Rendering Pipeline Architecture
```
Particles (GPU Compute) → 3D Density Grid → Ray Marching (DXR) → HDR Buffer → TAA → Composite → Display
                      ↗                  ↗               ↗          ↗       ↗
               Simulation              Mip-Chain    Lighting    History   Tone Mapping
```

### Project Structure
```
src/
├── core/           # App lifecycle, device/swapchain, window management
├── dxr/            # Acceleration structures, DXR pipeline, shader binding tables
├── volumetric/     # Particle simulation, density grid, ray marching compute
├── renderer/       # HDR pipeline, composite, TAA, post-processing
└── utils/          # Logging, environment, descriptor allocation, file I/O
```

## 📊 Current Implementation Status

### ✅ Completed Infrastructure (DXR_0020-0022)
- **Core D3D12 Framework**: Window, device, swapchain, command queues
- **DXR Infrastructure**: State objects, acceleration structures, shader binding tables
- **HDR Pipeline**: R16G16B16A16_FLOAT render targets with fullscreen composite
- **Descriptor Management**: Dynamic descriptor heap allocator
- **PIX Integration**: Comprehensive GPU event markers for debugging
- **Enhanced Logging**: File-based logging with thread safety
- **Environment System**: Safe environment variable helpers
- **Input System**: Keyboard toggles for debugging (P=pause, F1=debug, F2=checkpoint)

### 🔄 Currently Debugging
- **CreateStateObject E_INVALIDARG**: DXR pipeline creation failing (missing valid shaders)
- **HDR Texture E_INVALIDARG**: Format support issue (R16G16B16A16_FLOAT UAV compatibility)

### ❌ Missing Core Components (The Real Content)
- **Particle System**: GPU compute simulation with physics
- **3D Density Grid**: Particle splatting to volumetric texture
- **Ray Marching Shaders**: Actual DXR shaders for volumetric rendering
- **Geometry Data**: Triangle data for acceleration structures
- **Camera Integration**: Connected camera movement and input handling

## 🚧 Current Pipeline Problem

**The main issue**: We've built excellent scaffolding but **no actual content to render**. Even if we fix the DXR errors, we'd still see the same gradient because:
1. No particle data or simulation
2. No volumetric density generation
3. No actual ray marching implementation
4. Empty acceleration structures

## 📋 Recommended Pipeline Revision

### Priority 1: Get Something Visible (Particle System First)
```
1. Simple Particle System (GPU Compute)
   ├── Basic particle buffer (position, velocity, life)
   ├── Compute shader for simple physics (gravity, movement)
   └── Debug particle visualization (point sprites)

2. 3D Density Grid Generation
   ├── Splatting particles to 3D texture
   ├── Simple density accumulation
   └── Density slice visualization for debugging

3. Basic Ray Marching (Compute, not DXR initially)
   ├── Compute shader ray marching through density
   ├── Simple Beer-Lambert absorption
   └── Output to HDR texture

4. DXR Integration (Once we have working compute path)
   ├── Move ray marching to DXR raygen shader
   ├── Add acceleration structures for geometry
   └── Implement ray-traced shadows
```

### Priority 2: Physics and Visual Quality
```
5. Advanced Particle Physics
   ├── SPH (Smoothed Particle Hydrodynamics)
   ├── Constraint systems (sphere, torus, disc)
   └── Multiple simulation modes

6. Advanced Volumetric Rendering
   ├── Henyey-Greenstein scattering
   ├── Temperature-based emission
   ├── Mip-chain optimization
   └── Empty-space skipping

7. Post-Processing Pipeline
   ├── Temporal Anti-Aliasing (TAA)
   ├── Tone mapping
   └── Bloom effects
```

## 🎮 Workflow: Claude ↔ GPT-5 ↔ Ben

### GPT-5 Role (Planning & Specification)
- Creates detailed task specifications in `changes/DXR_XXXX_name.json`
- Defines acceptance criteria and implementation requirements
- Provides architectural guidance and technical direction

### Claude Role (Implementation & Integration)
- Implements tasks according to GPT-5 specifications
- Creates matching result files in `results/DXR_XXXX_name_result.json`
- Maintains codebase consistency and handles debugging

### Ben Role (Testing & Validation)
- Tests implementations and provides feedback
- Validates visual output and performance
- Requests features and reports issues

### Critical Workflow Rules
1. **Tasks defined in `changes/`** → **Results documented in `results/`**
2. **Always create result files** when completing tasks (prevents context loss)
3. **Log everything** to `PlasmaDX.log` for debugging multi-session work
4. **PIX captures** for validating GPU work and debugging

## 🎯 Immediate Next Steps

### For Getting Visuals Working
1. **Create basic particle system** with simple GPU compute simulation
2. **Implement 3D density splatting** from particles to texture
3. **Add compute-based ray marching** as stepping stone to DXR
4. **Debug and fix DXR pipeline** for final ray-traced implementation

## 🔍 Key Technical References

### PlasmaVulkan Comparison
The original Vulkan implementation achieved:
- **100k-1M particle simulation** at 60+ FPS
- **Real-time volumetric rendering** with cone-stepped sampling
- **Multiple constraint modes** (sphere, disc, torus, accretion)
- **Advanced scattering and absorption** with temperature mapping
- **Recording system** for captures and analysis

### DXR Advantages Over Vulkan RT
- **Simpler pipeline creation** (fewer descriptor sets, cleaner state management)
- **Better tooling integration** (PIX for Windows)
- **More mature shader debugging** support
- **Cleaner acceleration structure management**

### Target Performance
- **1080p @ 60+ FPS** for standard quality
- **4K @ 30+ FPS** for high quality captures
- **Scalable quality** via grid resolution and step counts
- **Robust empty-space skipping** for performance optimization

---

*This document represents the complete technical specification and current status of PlasmaDX as of September 17, 2025. All implementation work should reference this document for consistency and architectural alignment.*