## Do Not Restart: Stabilization Plan and Exact Execution Order (2025-09-18)

### Decision
- We will NOT restart the project. The architecture is sound (Agility SDK, offline DXC, descriptor allocator, HDR + Composite, PIX hooks). Current issues are synchronization and binding hygiene, not structural.

### What’s going wrong (from logs_9)
- Device removal loop stems from command list/allocator lifecycle misuse: early returns after Reset/Close lead to “ExecuteCommandList: allocator was reset after the command list was recorded”, then removal.
- DXR PSO creation fails with a root-signature validation: typed RWTexture2D(u0) bound via root UAV descriptor. DXR requires descriptor tables for typed UAV/SRV.
- Visuals are black because HDR is never written: compute debug isn’t dispatched yet; DXR is failing; only a UAV barrier is issued (no transition to SRV before Composite).

### Root causes (succinct)
- Command list flow is inconsistent across early-return paths → allocator reuse before GPU completion.
- Global root signature uses root descriptors where descriptor tables are required (typed UAV/SRV in DXR).
- No content producer active (no DXR, no compute), and missing HDR SRV transition.

### Exact job order and what to do
1) APP_0004: Unify command list lifecycle and fence flow
   - Objective: One Close→Execute→Present→Signal per frame; no early-return paths that leave recorded-but-unexecuted lists.
   - Steps:
     - Refactor `renderFrameDXR()` into phases: BeginFrame(wait/reset) → Update → OptionalDXR (guarded) → Composite → EndFrame(close/execute/present/signal).
     - Replace Close()+return branches with booleans that skip DXR and fall through to Composite/EndFrame.
     - Ensure `waitForFrame()` uses per-backbuffer fence values; signal after Present and store per-backbuffer.
   - Verify: No allocator-reset or “lists must be closed” errors; no device removal in first minute; PIX shows exactly one Close/Execute per frame.

2) APP_0003: Guard DXR; guaranteed HDR visual via UAV clear
   - Objective: Show on-screen output immediately regardless of DXR.
   - Steps:
     - If PSO/SBT/TLAS invalid, skip DXR block entirely.
     - Set descriptor heap; issue `ClearUnorderedAccessViewFloat` on `m_hdrTexture` each frame with a time-varying color.
   - Verify: Animated color visible; PIX shows ClearUAV on HDR before Composite.

3) VOL_0001B: Wire up compute debug dispatch (particles_debug_pattern.hlsl)
   - Objective: Replace fallback clear with actual compute content.
   - Steps:
     - Create compute PSO from `shaders/vol/particles_debug_pattern.dxil`.
     - Use `m_debugRootSignature` and bind: CBV(b0)=constants, SRV(t0)=particles (root SRV OK), UAV(u0)=HDR via a UAV descriptor table (not root UAV).
     - Dispatch `ceil(width/8) x ceil(height/8) x 1` (matches numthreads 8x8x1).
   - Verify: PIX shows compute Dispatch and UAV writes; visible animated pattern.

4) DXR_0024: Correct HDR resource states before Composite
   - Objective: Legal state transitions for HDR sampling.
   - Steps:
     - After last UAV write, do UAV barrier then TRANSITION HDR → `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE`.
     - Before next writes, transition HDR back to UAV as needed.
   - Verify: No debug warnings; PIX shows HDR in SRV state during Composite.

5) DXR_0023: Fix DXR global root signature descriptor tables
   - Objective: Resolve CreateStateObject error 0x80070057.
   - Steps:
     - Build a global root signature with two descriptor tables: SRV range (t0: TLAS), UAV range (u0: output texture).
     - Allocate/bind SRV(TLAS) and UAV(HDR) from the shader-visible heap via `SetComputeRootDescriptorTable` before `DispatchRays`.
   - Verify: PSO creation succeeds; PSO properties non-null; no typed UAV root-descriptor error.

### Sanity/rollback checks
- If visuals remain black after 1)–2), force the fallback `renderFrame()` path temporarily to verify Composite + backbuffer are healthy.
- Keep `PLASMADX_NO_QUIT_ON_REMOVAL=1` during investigation to retain the window after removal, but expect this to be unnecessary once 1) is done.

### Why no restart is needed
- Agility SDK + DXC toolchain + resource plumbing are in place and correct.
- Composite renderer and HDR target exist and are being bound; missing piece is content + state fix.
- Issues are early-phase hygiene (lifecycle, bindings) and are straightforward to correct.

### File references
- Change requests: `changes/APP_0004_unify_cmdlist_and_fence_flow.json`, `changes/APP_0003_hdr_clear_fallback_and_guard_dxr.json`, `changes/VOL_0001B_wire_up_compute_debug_dispatch.json`, `changes/DXR_0024_correct_hdr_state_transitions_for_composite.json`, `changes/DXR_0023_fix_global_root_signature_uav_binding.json`.
- Supporting guides: `findings/COMPANION_GUIDE_BLACK_SCREEN_TO_VISUAL_2025_09_18.md`.


