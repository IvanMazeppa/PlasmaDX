# VOL_0003A — Ray Setup & Bounds Debug (Change Log with MCP Citations)

## What changed
- Added two compute debug modes to the ray marcher shader and pipeline:
  - RayDir (mode=1): visualize per-pixel ray direction from camera in RGB
  - Bounds (mode=2): AABB intersection visualization (magenta=miss, green=hit-from-outside, yellow=started-inside)
- Introduced `g_debugMode` to `b1` cbuffer; passed from CPU via `RayMarcher`.
- Added F4 toggle in the app to cycle Off → RayDir → Bounds and log state.
- Ensured descriptor heaps are correctly bound before setting descriptor tables.

## Files edited
- `shaders/vol/ray_march_cs.hlsl`
  - Added `uint g_debugMode;` to `VolumeConstants` (b1), padded to 16-byte alignment.
  - Inserted RayDir visualization after ray reconstruction.
  - Inserted Bounds visualization around AABB test with distinct colors.
- `src/volumetric/RayMarcher.h/.cpp`
  - Added `m_debugMode` state, setters (`SetDebugMode`, `CycleDebugMode`, `GetDebugMode`).
  - Wrote `debugMode` into the b1 cbuffer struct and mapped to HLSL.
  - Bound both the global CBV/SRV/UAV heap and sampler heap before `SetComputeRootDescriptorTable`.
- `src/core/App.cpp`
  - Mapped F4 key to `CycleDebugMode()` and log current state.
- `CMakeLists.txt`
  - Guarded post-build DLL copies with `if(EXISTS ...)` to prevent failing builds on missing local Agility/DXC DLLs.

## Why these changes
- The reported visuals (homogeneous fog, hard to see structure) suggested ray generation or volume intersection might be off. These debug views allow rapid confirmation of:
  - Camera ray reconstruction using the inverse view-projection matrix is correct (RayDir view should smoothly vary as the camera rotates).
  - Ray vs volume AABB intersection behaves as expected (miss vs hit; inside vs outside start).

## Key technical notes
- HLSL cbuffer alignment: Added `float3` padding after `uint g_debugMode` to maintain 16-byte packing.
- UAV ordering: We retain the existing UAV barrier after dispatch to guarantee visibility before composite.
- Descriptor heaps: We bind the descriptor heaps before setting root descriptor tables for SRV/UAV and sampler.

## How to use
- Run the app and use M+K to move. Press F4 to cycle debug modes:
  - Off: normal marching
  - RayDir: normalized ray direction → RGB (0.5 + 0.5 * dir)
  - Bounds: magenta (miss), green (hit from outside), yellow (started inside volume)

## Acceptance checks
- RayDir responds predictably to camera rotation (no inversion/banding).
- Bounds colors match expectation when moving across the volume AABB.
- No validation errors; frame lifecycle intact (single Close→Execute→Present→Signal).

## MCP citations
- Command list & descriptor heaps
  - ID: ID3D12GraphicsCommandList — binding descriptor heaps prior to descriptor tables is required.
    - MCP: ID3D12GraphicsCommandList (Core Interface)
- Resource barriers & states
  - ID: D3D12_RESOURCE_BARRIER — UAV barrier for ordering after unordered writes.
    - MCP: D3D12_RESOURCE_BARRIER (Structure), D3D12_RESOURCE_STATES (Enum)
- Descriptor ranges
  - ID: D3D12_DESCRIPTOR_RANGE1 — descriptor table ranges for SRV/UAV/samplers.
    - MCP: D3D12_DESCRIPTOR_RANGE1 (Structure)
- HLSL shader notes
  - Threading/layout: `[numthreads(16,16,1)]` with `SV_DispatchThreadID` indexing; `RWTexture2D` and `Texture3D.SampleLevel` usage are standard DXIL ops.

## Next
- If RayDir/Bounds confirm correctness, proceed to VOL_0003B (analytic sphere + density probe) to lock in visibility with a known-good density field.
