# PlasmaDX RT Volumetric Demo - DXR 1.2 Implementation

## Overview
PlasmaDX demonstrates real-time ray traced volumetric plasma rendering using DirectX 12 + DXR 1.2. This implementation is optimized for RTX 4060Ti and newer hardware with advanced ray tracing capabilities.

## Core Architecture
- **DXR 1.2**: Enhanced ray tracing pipeline with improved performance
- **RTX 4060Ti Optimizations**: Leverages RT cores for volumetric self-shadowing
- **Hybrid Pipeline**: DXR for lighting + compute shaders for volumetric march
- **Static Camera**: Optimal for temporal accumulation and lighting stability

## Demo Modes

### Mode 1: Sphere RT Baseline
**Objective**: Pure DXR lighting demonstration with static geometry
- Single test primitive (sphere/cube AABB) in dark scene
- Static camera with GlobalParams control
- Miss shader near-black (toggle with PLASMADX_YELLOW_BG)
- Strong, unambiguous RT lighting validation

**Command**:
```powershell
$env:PLASMADX_DISABLE_DXR='0'; $env:PLASMADX_DEBUG_MODE='1'; ./build-vs2022/Debug/PlasmaDX.exe
```

### Mode 2: Torchlight Demo
**Objective**: Interactive DXR lighting with mouse control
- Animated spotlight following mouse cursor
- 3D spherical light positioning
- Color cycling and intensity control

**Command**:
```powershell
$env:PLASMADX_TORCHLIGHT_DEMO='1'; $env:PLASMADX_DEBUG_MODE='2'; ./build-vs2022/Debug/PlasmaDX.exe
```

### Mode 3: Compact Volumetric Demo
**Objective**: Small moving volume with DXR lighting and self-shadowing readiness
- Compact metaball cluster or procedural blob
- High plasticity: strong highlights and occlusion boundaries
- Light-occlusion sampling for volumetric shafts
- Additive DXR blending capability

**Commands**:
```powershell
# Compute-only volumetrics
$env:PLASMADX_DISABLE_DXR='1'; $env:PLASMADX_DEBUG_MODE='3'; ./build-vs2022/Debug/PlasmaDX.exe

# Volumetrics + DXR additive blending
$env:PLASMADX_DISABLE_DXR='0'; $env:PLASMADX_DEBUG_MODE='3'; $env:PLASMADX_DXR_BLEND='1'; ./build-vs2022/Debug/PlasmaDX.exe
```

## DXR 1.2 Enhancements

### RTX 4060Ti Specific Features
- **Enhanced RT Cores**: Optimized ray-triangle and ray-AABB intersection
- **Shader Execution Reordering (SER)**: Improved coherence for volumetric rays
- **RT Pipeline State Object v1.1**: Reduced driver overhead
- **Inline Ray Tracing**: Direct ray queries in compute shaders for self-shadowing

### Technical Improvements
- **RTPSO Collections**: Batched pipeline creation for better performance
- **GPU Work Creation**: Leveraging RTX 4060Ti's enhanced scheduling
- **Memory Pool Optimization**: Better VRAM utilization for RT structures
- **Temporal Reuse**: Frame-to-frame coherence for volumetric samples

## Environment Variables

| Variable | Values | Effect |
|----------|--------|---------|
| `PLASMADX_DISABLE_DXR` | `0/1` | Enable/disable DXR pipeline |
| `PLASMADX_DEBUG_MODE` | `1-3` | Demo mode selection |
| `PLASMADX_DXR_BLEND` | `0/1` | Additive DXR + compute blending |
| `PLASMADX_YELLOW_BG` | `0/1` | Miss shader debug background |
| `PLASMADX_USE_DXR_12` | `1` | Force DXR 1.2 features (RTX 4060Ti) |
| `PLASMADX_ENABLE_SER` | `1` | Shader Execution Reordering |

## Runtime Controls

### Lighting Controls
- **C**: Cycle light colors (5 presets)
- **F**: Attach/detach torch to camera
- **Mouse**: 3D torch positioning (torchlight mode)
- **LMB Hold**: Torch on/off

### Volumetric Controls
- **1-4**: Exposure/density presets
- **5/6**: Anisotropy g parameter down/up
- **O/P**: Metaball count down/up
- **B**: DXR blend toggle
- **N/M**: Blend scale down/up

### Debug Controls
- **F1**: Toggle debug verbose output
- **F2**: Save log checkpoint
- **F3**: Cycle density volume presets
- **F4**: Cycle ray marcher debug modes

## Technical Architecture

### DXR 1.2 Pipeline
```
TLAS/BLAS Construction → RT Pipeline State → Shader Binding Table → DispatchRays
     ↓                        ↓                    ↓                     ↓
RT Structures          Collection PSO        Ray Gen/Miss/Hit      GPU Execution
```

### Self-Shadowing Ready Design
The volumetric system is architected for easy DXR self-shadowing integration:

1. **Light Occlusion Pass**: Compute shader samples toward light source
2. **Transmittance Calculation**: Early exit by opacity threshold
3. **DXR Shadow Rays**: Ready for inline ray tracing queries
4. **Temporal Coherence**: Frame-to-frame shadow cache

### Resource Management
- **UAV Barriers**: Proper synchronization between DXR and compute
- **Descriptor Heaps**: Shader-visible heap for RT descriptors
- **Memory Pools**: Optimized for RTX 4060Ti VRAM layout
- **PIX Markers**: Comprehensive GPU debugging support

## Build Instructions

### Prerequisites
- Visual Studio 2022 with Windows 11 SDK
- DXC Compiler (latest)
- RTX 4060Ti or newer GPU
- PIX for Windows (debugging)

### Build Commands
```powershell
# Configure with DXR 1.2 support
cmake -B build-vs2022 -G "Visual Studio 17 2022" -DPLASMADX_DXR_VERSION=1.2

# Compile shaders and build
cmake --build build-vs2022 --config Debug -j 8
```

## Performance Targets (RTX 4060Ti)

| Mode | Target FPS | RT Rays/Frame | VRAM Usage |
|------|------------|---------------|------------|
| Sphere RT | 120+ | 2M | 200MB |
| Torchlight | 90+ | 2M | 250MB |
| Volumetric | 60+ | 8M | 400MB |

## Implementation Notes

### Phase Function Tuning
```hlsl
// Henyey-Greenstein phase function with anisotropy control
float phase = (1.0 - g*g) / pow(1.0 + g*g - 2.0*g*dot(rayDir, lightDir), 1.5);
```

### Temporal Accumulation
- Alpha blending: `result = lerp(history, current, 0.1)`
- Jitter patterns for sub-pixel sampling
- Motion vector compensation for moving volumes

### Light Occlusion Sampling
```hlsl
// Sample toward light for volumetric shadows
float transmittance = 1.0;
for (int i = 0; i < MAX_LIGHT_SAMPLES; i++) {
    float3 samplePos = rayPos + lightDir * stepSize * i;
    float density = sampleDensity(samplePos);
    transmittance *= exp(-density * absorption * stepSize);
    if (transmittance < 0.01) break; // Early exit
}
```

## Validation Checklist

### DXR 1.2 Compliance
- [ ] RT Pipeline State Object v1.1
- [ ] Enhanced ray-geometry intersection
- [ ] Shader Execution Reordering support
- [ ] Inline ray tracing compatibility

### Resource Management
- [ ] TLAS/BLAS alignment and updates
- [ ] SBT proper stride and alignment
- [ ] UAV barriers for compute-DXR interop
- [ ] Descriptor heap shader visibility

### Performance Verification
- [ ] PIX GPU capture analysis
- [ ] Memory allocation profiling
- [ ] RT structure build times
- [ ] Frame time consistency

## Troubleshooting

### Common Issues
1. **Black screen**: Check DXR tier support and TLAS construction
2. **Shader compilation**: Verify DXC version and SM 6.6 support
3. **Performance drops**: Monitor VRAM usage and RT structure sizes
4. **Debug layer errors**: Validate resource transitions and barriers

### RTX 4060Ti Specific
- Ensure driver version 546.65 or newer
- Enable Hardware Accelerated GPU Scheduling in Windows
- Set GPU memory allocation to "Optimize for performance"
- Use PIX timing captures for RT pipeline bottlenecks

## Future Roadmap

1. **DXR Self-Shadowing**: Inline ray tracing for volumetric shadows
2. **Multi-Resolution**: Adaptive sampling based on distance
3. **Denoising Integration**: AI-based temporal denoising
4. **VRS Support**: Variable Rate Shading for performance
5. **Mesh Shaders**: RT-compatible geometry pipeline