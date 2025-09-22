# Codex Analysis — PlasmaDX Rendering Bring-Up (2025-09-18)

## 1. Summary
- The swapchain and composite passes execute, but the HDR render target spends most frames in the wrong state, so the fallback clear/compute writes never surface onscreen.
- The legacy DXR “direct-to-backbuffer” branch still binds UAVs via root descriptors while the new global root signature expects descriptor tables, so any attempt to create or use the RT PSO will keep failing.
- The shader binding table remains a stub that returns zeroed GPU addresses; every guarded `DispatchRays` would immediately hit `StartAddress` validation errors even if the PSO linked.
- Particle scaffolding is half wired: buffers exist, but data is never uploaded, the update dispatch is commented out, and debug draw forces `particleCount = 0`, so the shaders cannot influence the HDR target.

## 2. Current Behaviour & Evidence
- **Runtime output** – With `PLASMADX_DISABLE_DXR=1` (default), the frame loop goes through `renderFrameDXR` → fallback HDR clear. Logs confirm thousands of successful presents but the window stays dark.
- **Device warnings** – Earlier logs capture D3D12 warnings about UAV/SRV state mismatches and null SBT pointers, matching the code paths highlighted below (`logs/logs_10.txt`, `PlasmaDX.log`).

## 3. Detailed Findings

### 3.1 HDR State Tracking Regression
- `renderFrameDXR` unconditionally sets `m_hdrIsInSRVForRead = false` right after `Present` (`src/core/App.cpp:1174-1175`).
- On the next frame, the compute/UAV path sees the flag is already **false**, skips the `SRV → UAV` transition, and starts clearing the texture while it is still in SRV state. PIX/InfoQueue warnings mirror this mismatch (`PlasmaDX.log`, look for `Before state (0x8)` vs `state (0xC0)`).
- Fallback path (`renderFrame`) also relies on the same flag to gate transitions; both paths are affected.

**Fix**: Do not mutate the tracking flag outside recorded barriers. Either remove the manual reset or insert an explicit transition back to UAV before the next UAV write.

### 3.2 DXR Direct-To-Backbuffer Path Out of Date
- The global root signature now uses a descriptor table for the HDR UAV (`src/core/App.cpp:805-827`), but the legacy branch still pushes the backbuffer as a root UAV (`src/core/App.cpp:1104-1131`).
- D3D12 forbids typed UAVs bound through root descriptors; the debug layer records `CreateStateObject` errors about “typed UAV using a register mapped to a root descriptor” (see `PlasmaDX.log`, repeated `CreateStateObject failed: 0x80070057`).
- The same branch would also try to `DispatchRays` even though the SBT has no GPU addresses (see §3.3).

**Fix**: Either retire this branch until DXR parity work resumes, or modernise it: allocate a UAV descriptor for the target and bind via `SetComputeRootDescriptorTable` like the HDR path.

### 3.3 Shader Binding Table Stub Blocks DXR
- `SBT::GetDispatchRaysDesc` returns a struct with width/height but leaves all shader table addresses at zero (`src/dxr/SBT.cpp:27-39`).
- Guard code in `renderFrameDXR` checks for non-zero addresses, logging “DXR dispatch skipped: SBT addresses are not set” when the fallback is enabled. If the guard is bypassed, the runtime raises `DispatchRays: StartAddress can't be null` (`PlasmaDX.log`, lines around `395303`).

**Fix**: Implement the SBT buffer upload (raygen/miss/hit sections) before re-enabling DXR dispatch.

### 3.4 Particle Pipeline Incomplete
- Initialization creates CPU data but never uploads it to the GPU; the log even warns that particles will be zero on GPU (`src/volumetric/Particles.cpp:262-283`).
- The update shader dispatch is commented out (`src/volumetric/Particles.cpp:292-305`), so nothing animates.
- `WriteDebugPattern` forces `particleCount = 0` to avoid reading uninitialised data (`src/volumetric/Particles.cpp:318-333`), which means even when the compute path is fixed, particle influence stays disabled.

**Fix**: Add an upload pass (default buffer + staging upload), re-enable the update dispatch, and only zero the count if no valid buffer is bound.

## 4. Recommendations (Execution Order)
1. **Stabilise the frame lifecycle**
   - Remove the manual reset of `m_hdrIsInSRVForRead`, or add a recorded transition back to UAV at the top of each frame before compute writes.
   - Re-run to confirm the animated HDR clear appears (PIX should show a `ClearUnorderedAccessViewFloat`).
2. **Simplify DXR handling during VOL tasks**
   - Keep DXR disabled by default; wrap the direct-to-backbuffer branch in the same guard or delete it until SBT work is ready.
   - If the branch must stay, allocate a descriptor-table UAV and bind it correctly.
3. **Complete SBT implementation**
   - Allocate GPU buffers for raygen/miss/hit records, copy identifiers, and set the dispatch descriptor fields.
4. **Finish particle bring-up**
   - Implement buffer upload (or generate data on GPU), restore the update dispatch, and pass the actual `particleCount` to the debug shader.
   - Once particles write meaningful data, move on to VOL-0002 density integration.

## 5. Suggested Validation
- Run with `PLASMADX_DISABLE_DXR=1` and capture a PIX frame; verify HDR UAV → SRV → composite ordering.
- After SBT and descriptor fixes, re-enable DXR and confirm `CreateStateObject` succeeds and `DispatchRays` shows populated shader tables.
- Add a short automated check (log assertion) to ensure `DispatchRaysDesc` addresses are non-zero before dispatching.

---
Prepared by Codex (GPT-5) — 2025-09-18.
