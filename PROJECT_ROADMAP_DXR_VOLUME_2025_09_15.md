## DXR Volumetric Plasma/Gas Roadmap (2025-09-15)

Objective
- Build a DX12 + DXR renderer for volumetric plasma/gas with directional RT lighting/shadows, TAA, and future god rays. Prioritize robust tooling (PIX), predictable performance, and clean architecture.

Guiding principles
- Separate concerns: `core/` (device/swapchain), `dxr/` (AS, pipeline, SBT), `volumetric/` (density, marcher, TAA), `utils/`.
- Aggressive instrumentation: PIX events, error checks, resize safety.
- Scale quality via grid size, step count, temporal accumulation; enable offline “cinema” mode.

Milestones
1) DXR Hello Pipeline (DXR-0001)
   - Minimal BLAS (triangle) + TLAS (single instance)
   - DXR PSO: DXIL library (raygen/miss/closest-hit), root signatures, subobjects, SBT
   - DispatchRays to backbuffer (gradient)
   - Acceptance: compiles, shows gradient; PIX capture validates transitions, SBT sizes, root bindings

2) AS + SBT Utilities (DXR-0002)
   - Utility: BLAS builder (triangles), TLAS builder (instances, transforms), scratch sizing
   - SBT builder: contiguous sections (raygen/miss/hit), correct strides/alignments
   - Camera constant buffer + viewport handling; per-frame updates
   - Acceptance: camera motion affects raygen; AS rebuilds correctly; PIX events around AS/SBT

3) Volumetric Scaffolding (DXR-0003)
   - HDR render target (R16G16B16A16_UNORM/float UAV) + fullscreen composite to swapchain
   - 3D density texture (R16F) resource + clear/visualize slice compute
   - Marcher stub (compute) writing a pattern to HDR for pipeline validation
   - Acceptance: HDR writes visible; composite works; resize-safe

4) Density from Particles (DXR-0004)
   - Structured buffer for particles; compute splat to 3D density (atomic add to start)
   - Mip chain and occupancy grid (8³ bricks) for empty-space skipping
   - Acceptance: density histogram sane; slice debug shows expected structure; perf stable at 1M

5) Ray Marcher v1 (DXR-0005)
   - STBN jitter; Beer–Lambert transmittance (preintegrated LUT); HG phase
   - Cone/adaptive stepping; early-out on opacity; write to HDR
   - Acceptance: coherent noise, parameters adjustable; PIX shows bounded dispatch time

6) TAA with Neighborhood Clamping (DXR-0006)
   - History/current HDR (R16G16B16A16); jitter-only reprojection; clamp neighborhood
   - Update prev matrix after TAA; reset on resize/mode change
   - Acceptance: no purple/green; ghosting controlled; resize-safe

7) RT Shadows (External Occluders) (DXR-0007)
   - BLAS/TLAS for authored occluders; ray queries in raygen or closest-hit
   - Masking, bias, max distance; debug hit overlay
   - Acceptance: overlay shows hits; shadows consistent with geometry; PIX validates AS reads

8) Self-Shadowing in Volume (DXR-0008)
   - Integrate transmittance along march; optionally inline raytracing for shell proxies
   - Acceptance: visible self-shadow; controllable softness; stable temporally

9) God Rays & Advanced Effects (DXR-0009)
   - Light-space marching or volume-light injection; shafts with temporal accumulation
   - Optional SVGF/temporal reuse for denoising
   - Acceptance: shafts visible with adjustable intensity; stable under motion

10) Quality/Performance Modes (DXR-0010)
   - Half-res marcher + bilateral upscale; preset tiers (Preview/Quality/Cinema)
   - Acceptance: target FPS in Preview; Cinema supports long-accumulation with checkpoints

Risk controls
- Root signatures: keep minimal; validate against shader reflection; unit test parameter indices
- SBT: assert strides and section offsets; log sizes; PIX verify
- Barriers: explicit state transitions for SRV/UAV/RTV; zero barriers during Present
- Resize: release/b recreate size-dependent resources; guard nulls
- Shaders: guard empty workgroups; bounds-checked UAV writes

References
- Microsoft Learn – DirectX Raytracing: [link](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-raytracing)
- DirectX Graphics Samples (DXR): [link](https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-raytracing-samples-win32/)
- DirectX-Specs (DXR): [link](https://github.com/microsoft/DirectX-Specs)
- DirectX-Headers (API surface): [link](https://github.com/microsoft/DirectX-Headers)
- DirectXShaderCompiler (DXC): [link](https://github.com/microsoft/DirectXShaderCompiler)
- PIX for Windows docs: [link](https://devblogs.microsoft.com/pix/documentation/)
- NVIDIA DXR Tutorial (Part 1): [link](https://developer.nvidia.com/blog/dx12-raytracing-tutorials)
- Ray Tracing Gems I/II (Morgan & Kaufmann): [link](https://www.raytracinggems.com/)
- SVGF paper (Schied et al.): [link](https://research.nvidia.com/publication/2017-07_Spatiotemporal-Variance-Guided-Filtering)
- TAA (Karis): [link](https://iryoku.com/downloads/Kernel_Deferred_AA/Karis2014_TAA.pdf)
