## Analysis: Black Window After VOL_0001 (2025-09-18)

### Context
- After completing VOL_0001, the app shows a black window instead of the prior animated color.
- Logs confirm particle system init and repeated debug pattern logs, but visuals are black.

### Signals from logs/code
- DXR PSO creation still fails (expected for now):
  - CreateStateObject 0x80070057
  - Root-signature binding error for typed UAV as root descriptor (not blocking compute path)
- Particles system updates log every 60 frames.
- `Particles::WriteDebugPattern` computes RGB values but does not write to GPU yet (TODO noted).

### Root cause and fix
- Cause: Composite pass drew with no RS viewport/scissor set, so fullscreen triangle copy could be undefined/culled → black.
- Fix implemented: Set viewport and scissor before both composite draws:
  - In `renderFrame()` (fallback HDR→composite path)
  - In `renderFrameDXR()` (HDR→composite path)

### Current expected visual path
1) HDR is cleared each frame to an animated color via `ClearUnorderedAccessViewFloat` (temporary visual until compute shaders write).
2) Barrier: HDR UAV → UAV barrier (for hazard ordering), then backbuffer transitions PRESENT→RTV, composite SRV(hdr) → RTV(backbuffer).
3) Backbuffer transitions RTV→PRESENT, present.

### Validation checklist (Claude)
- Build and run Debug.
- Confirm in logs:
  - "HDR texture descriptors allocated: SRV[...] UAV[...]"
  - "HDR texture created (WxH)"
  - Particle system init lines
- Take a PIX capture:
  - Verify a `ClearUnorderedAccessViewFloat` on the HDR texture each frame.
  - Verify a draw call from the composite pass (fullscreen triangle) with SRV bound to HDR.
  - Verify viewport/scissor are set to swapchain dimensions before the draw.
  - Verify backbuffer state transitions: PRESENT→RTV then RTV→PRESENT.

### If still black
1) Isolate HDR→composite path in `renderFrameDXR()`:
   - Temporarily bypass DXR dispatch section and keep only:
     - HDR clear (UAV)
     - UAV barrier for HDR
     - Backbuffer PRESENT→RTV
     - Composite draw
     - Backbuffer RTV→PRESENT
2) Ensure descriptor indices are valid:
   - `m_hdrSrvIndex != UINT_MAX` and `m_hdrUavIndex != UINT_MAX`
   - Logs already print allocated indices.
3) Verify descriptor heap binding before composite draw:
   - `SetDescriptorHeaps(1, &m_srvUavHeap)`
4) Confirm viewport/scissor set prior to composite in both code paths (DXR and fallback).

### Notes on DXR errors (non-blocking for now)
- The PSO error about typed UAV root descriptors stems from the DXR root signature mapping; we are not relying on DXR output yet.
- These errors do not prevent the HDR clear or composite.

### Next steps
- Once animated color is visible again, proceed with VOL_0002 and VOL_0003:
  - VOL_0002: implement 3D density grid and slice debug.
  - VOL_0003: compute ray marcher writing to HDR (replaces temporary HDR clear).

### Files touched by the fix
- `src/core/App.cpp` — viewport/scissor set before both composite passes.


