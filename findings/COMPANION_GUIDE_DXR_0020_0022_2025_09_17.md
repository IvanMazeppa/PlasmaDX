## Companion Guide: DXR_0020–DXR_0022

Order
1. DXR_0020 – PIX markers and barriers
   - Add scoped PIX markers around AS build, PSO create, SBT build, DXR dispatch, HDR composite, Present.
   - Insert HDR UAV→SRV barrier before composite; verify swapchain PRESENT↔RTV transitions.
2. DXR_0021 – Descriptor heap mini-allocator
   - Replace hardcoded SRV/UAV indices with allocator calls.
   - Log allocated indices and ensure reuse on HDR recreation.
3. DXR_0022 – Env helpers and input
   - Replace getenv with _dupenv_s wrappers; add P (pause) and F1 (toggle debug logs) handling.

Pitfalls and tips
- PIX markers: remember to bind the correct command list context when issuing events.
- Barriers: ensure sequences are per-frame and after GPU writes (insert after ExecuteCommandLists when needed).
- Descriptor heap: keep capacity >= 32; assert on overflow and log.
- Env: free buffers returned by _dupenv_s; normalize values ("1", "true", "on").

Acceptance checks
- PIX capture shows clearly labeled stages; no state mismatch warnings in debug layer.
- Descriptor indices printed once at allocation and reused on resize; no leaks.
- No C4996 getenv warnings; pressing P/F1 logs state changes.
