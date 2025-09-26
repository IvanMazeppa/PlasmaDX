## Companion Guide: From Black Screen to Visual Output (2025-09-18)

### TL;DR
- Root cause: no GPU writes to HDR texture (DXR PSO fails; compute debug not dispatched).
- Immediate bridge: clear HDR UAV each frame and composite.
- Then wire compute debug dispatch; later fix DXR root signature bindings.

### Recommended Execution Order
1) APP_0003: HDR clear fallback and DXR guard
2) VOL_0001B: Wire compute debug dispatch to HDR
3) DXR_0024: Correct HDR state transitions before Composite
4) DXR_0023: Fix DXR global root signature (descriptor tables)

This yields: instant visuals via HDR clear → stable compute visuals → unblocked DXR PSO creation.

### Why this order?
- We want visuals now: APP_0003 requires no new shaders/PSOs and proves Composite is working.
- Compute dispatch (VOL_0001B) gives us content independent of DXR.
- Correct state transitions (DXR_0024) prevent subtle artifacts as content becomes real.
- Fixing DXR binding (DXR_0023) removes the CreateStateObject error and unlocks raygen parity work later.

### Verification Checklist
- Logs show HDR descriptor indices and "HDR texture created".
- PIX shows: ClearUAV on HDR, UAV barrier + transition, Composite draw, backbuffer transitions.
- After VOL_0001B: PIX shows a compute Dispatch with `particles_debug_pattern.hlsl` and UAV writes.
- After DXR_0023: no InfoQueue error about typed UAV root descriptor; PSO created.

### Notes
- The debug message about typed UAV as root descriptor is specific to DXR global roots; switch to descriptor tables for typed UAVs.
- A UAV barrier alone does not change resource state; add an SRV transition before Composite.


