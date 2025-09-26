### Companion Guide: DXR Hello → AS/SBT Utilities → Volumetric Scaffold (DXR-0001..0003)

Goal
- Stand up a robust DXR baseline and a volumetric compute/HDR path with clean PIX validation and minimal moving parts.

DXR-0001: Hello Pipeline
- Implement BLAS/TLAS for a single triangle; create DXR PSO and SBT; DispatchRays to backbuffer.
- Checks:
  - Output shows raygen gradient or solid color.
  - PIX capture: barriers around backbuffer, SBT sizes/strides align to constants, root signatures match shaders.
  - No debug layer errors.
- References:
  - Microsoft Learn – DirectX Raytracing
  - DirectX Graphics Samples – D3D12RaytracingHelloWorld
  - DirectX-Headers – d3d12.h raytracing enums

DXR-0002: AS + SBT Utilities
- Create reusable BLAS/TLAS and SBT builders; add camera CBV and update per-frame.
- Checks:
  - Moving camera changes output.
  - AS rebuild logs scratch sizes; no leaks across frames.
  - PIX events: AS build, SBT build, DispatchRays present; GPU capture looks healthy (no hazard warnings).
- References:
  - D3D12RaytracingSimpleLighting sample
  - DXR state object + SBT docs on Learn

DXR-0003: Volumetric Scaffold
- Add HDR UAV target (R16G16B16A16_FLOAT) and fullscreen composite; 3D density texture (R16_FLOAT) and a compute debug pass (slice visualization + pattern write).
- Checks:
  - HDR shows test pattern; composite to swapchain visible.
  - Density bounds respected; slice index clamped.
  - Resize recreates resources and remains error-free.
- References:
  - Learn: resource states/UAV barriers in D3D12
  - Samples: fullscreen composite patterns

Common pitfalls to avoid
- SBT alignment/stride mistakes → assert section sizes; log bytes per record.
- Root signature parameter mismatch → reflect shader bindings; keep RS minimal.
- Resource state transitions → present↔RTV; UAV ↔ SRV transitions before reads.
- Resize leaks → release RTVs/UAVs before ResizeBuffers; rebuild descriptors after.

PIX usage
- Add PIX events for: AS build, SBT build, DispatchRays, HDR write, composite.
- Capture 60 frames post feature enable; check barrier timeline and durations.

Next after 0003
- DXR-0004 Density from Particles (SRV for particles, compute splat, mip/occupancy)
- DXR-0005 Ray Marcher v1 (Beer–Lambert, HG, STBN)
- DXR-0006 TAA with neighborhood clamping
