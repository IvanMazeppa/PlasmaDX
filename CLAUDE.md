## Claude Starter Brief — PlasmaDX (DX12 + DXR)

### 🚨 CRITICAL DIRECTORY GUARDRAIL
**ALWAYS** ensure you are working in `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX`, NOT PlasmaVulkan.
PlasmaVulkan has been completely shelved. Claude Code may default to PlasmaVulkan directory - immediately switch to PlasmaDX at start of every session.

### Mission
Build a real-time (and offline-capable) ray traced volumetric plasma renderer using DirectX 12 + DXR, with strong tooling (PIX) and clear, testable milestones.

### Primary references (use these first)
- DXR overview and guide: [DirectX Raytracing on Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-raytracing)
- D3D12 programming model: [D3D12 Concepts and How-To](https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-programming-guide)
- HLSL reference (SM 6.x, DXIL intrinsics): [HLSL Reference](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl)
- Official specs and proposals: [microsoft/DirectX-Specs (DXR)](https://github.com/microsoft/DirectX-Specs)
- Official headers (API surface): [microsoft/DirectX-Headers](https://github.com/microsoft/DirectX-Headers)
- Official samples (DXR): [microsoft/DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples)
  - Start with: D3D12RaytracingHelloWorld, D3D12RaytracingSimpleLighting
- Shader compiler + DXIL tools: [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler)
- PIX (GPU debugger/profiler): [PIX for Windows docs](https://devblogs.microsoft.com/pix/documentation/)
- Agility SDK (optional, latest D3D12): [D3D12 Agility SDK](https://learn.microsoft.com/en-us/windows/win32/direct3d12/d3d12-dynamic-linking)

### Local environment and artifacts
- Windows SDK headers: `d3d12.h`, `d3d12shader.h`, `dxgi1_6.h` (via VS2022 + Windows 10/11 SDK).
- DXC (dxc.exe): install from DXC releases or VS Build Tools; compile HLSL 6.x and DXR shaders.
- PIX installed: capture `.wpix` traces, analyze GPU timings, resource lifetimes, and root signature binds.

### How to work (expectations for Claude)
- Code style: C++20, RAII, `ComPtr`, explicit errors checks (HRESULT → fail fast with logs), small utilities not frameworks.
- Instrumentation: Add PIX events for major steps (frame begin/end, AS build, SBT build, dispatchRays, copy/transition). Use `ID3D12GraphicsCommandList::BeginEvent/EndEvent` or PIX helpers.
- Feature checks: Query `D3D12_FEATURE_D3D12_OPTIONS5` (DXR tier). Gracefully degrade if not supported.
- Synchronization: Use fences for GPU/CPU sync; minimize `WaitForSingleObject` in the render loop. Always transition resources with correct states.
- Resize-safety: Recreate swapchain, RTVs, and any size-dependent resources. Validate null before release.
- DXR pipeline hygiene:
  - Keep global and local root signatures minimal and explicit.
  - Subobjects: DXIL library, hit groups, shader/ pipeline config, root signatures, and association subobjects.
  - Shader Binding Table: respect alignment constants from headers; build stable, contiguous sections (raygen/miss/hit) with correct strides.
  - BLAS/TLAS: correct geometry flags, build flags, scratch/AS alignment, and instance transforms.

### Typical pitfalls to avoid
- Root signature mismatch vs. shaders (parameter slots, descriptor ranges).
- Descriptor heap sizes too small for global + local SRVs/UAVs/CBVs.
- Missing resource state transitions (e.g., UAV to SRV before ray dispatch reads).
- SBT alignment/stride errors; wrong record sizes or section offsets.
- Reusing scratch buffers with insufficient size for next build.

### PIX usage (please do this in every feature PR)
- Add PIX events around: swapchain present, AS build (BLAS/TLAS), SBT build, `DispatchRays`, composite, and copies.
- Take a GPU capture for a few frames after enabling a new feature; verify barriers, timings, and binds.
- Use timing captures to spot stalls (e.g., command queue waits or long AS builds).

### Query/lookup strategy for Claude
- Prefer Microsoft Learn + DirectX-Headers for API questions; cross-check with DirectX-Graphics-Samples.
- For shader semantics and intrinsics, use HLSL reference and the DXC repo issues/README.
- When in doubt about alignment/flags, search DirectX-Headers for the exact constants and comments.
- For architecture patterns (SBT layout, AS build flows), skim HelloWorld/SimpleLighting DXR samples.

### Immediate goals (first implementation wave)
- Establish frame loop (already present), debug layer on in Debug, DXR tier logged.
- Add a small DXR “Hello Ray” pipeline:
  - Minimal BLAS/TLAS (single triangle or quad).
  - Minimal raygen/miss/closest-hit that outputs a gradient to the backbuffer.
  - Build SBT and call `DispatchRays` once per frame.
- Add a compute path skeleton for volumetric: create an HDR texture UAV, clear and composite.

### File/layout preferences
- `src/core`: App, device/swapchain, command objects, resize handling.
- `src/dxr`: AS builder, pipeline state (subobjects), SBT builder, RT pass.
- `src/volumetric`: density grid management, ray marching compute, TAA.
- `src/utils`: logging, helpers.

### PR acceptance checklist
- Compiles on VS2022 x64 Debug/Release.
- No debug layer errors; PIX capture shows valid transitions and binds.
- Logs include DXR tier, resource creation, and key step timings.
- Resize-safe; no leaks on exit.

### Nice-to-haves (later)
- Integrate Agility SDK to access latest DXR features.
- Add DXC build step in CMake for shaders (raygen/miss/hit and compute) with `/Zi` for debugging.
- Basic CPU UI (imgui later) to toggle features and capture settings.

Use these sources actively while implementing; cite which sample/API page you followed in PR descriptions so we can audit decisions quickly.
