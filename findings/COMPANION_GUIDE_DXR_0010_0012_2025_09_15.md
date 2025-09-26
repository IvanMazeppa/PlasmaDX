### Companion Guide: Fix Build/Run Baseline (DXR-0010..0012)

Problem
- Build succeeds but runtime exits immediately (white window/no output).
- Likely missing or incomplete DXR modules, no console, no diagnostics, and DXR shaders compiled with D3DCompile instead of DXC. Agility SDK optional but helpful.

Apply in order
1) DXR-0010: Stub DXR modules
   - Add `src/dxr/ASBuilder.*`, `Pipeline.*`, `SBT.*` with minimal APIs used by `App.cpp`.
   - Wire into CMake. This fixes compile/link gaps and allows incremental DXR.
   - Check: project compiles; App can call DXR init paths without link errors.

2) DXR-0011: Console + InfoQueue + DRED
   - Attach console in Debug; print banner.
   - Enable D3D12 debug layer and DXGI/D3D12 InfoQueue (break on ERROR in Debug).
   - Wrap `ExecuteCommandLists` and `Present` with HRESULT checks; on failure, call `checkDeviceRemoved(hr)` that logs `GetDeviceRemovedReason()` and dumps DRED breadcrumbs/page faults.
   - Check: on any failure you see explicit console errors; PIX markers present.

3) DXR-0012: CMake DXC + optional Agility SDK
   - Find and require `dxc.exe` for DXR lib_6_3 shader compilation; generate `.dxil` artifacts.
   - Optionally integrate Agility SDK (1.616 retail or 1.717 preview) to access latest features; set `D3D12SDKVersion` and `D3D12SDKPath`.
   - Check: DXC compiles shaders; app logs detected Agility SDK version at startup.

Tools and references
- Agility SDK downloads and guidance: [DirectX 12 Agility SDK](https://devblogs.microsoft.com/directx/directx12agility/)
- DXC (DirectXShaderCompiler): `https://github.com/microsoft/DirectXShaderCompiler`
- DRED: `https://learn.microsoft.com/en-us/windows/win32/direct3d12/d3d12-device-removed-extended-data`
- PIX for Windows: `https://devblogs.microsoft.com/pix/documentation/`

Acceptance
- App opens and renders animated clear color (fallback) or DXR gradient.
- On failure, console prints detailed HRESULT, info queue messages, and DRED.
- PIX GPU capture shows valid barriers and markers.

Notes
- If Agility SDK 1.717 preview is used, ensure the matching `D3D12Core.dll`/`d3d12SDKLayers.dll` are deployed and env vars are set. If instability occurs, prefer 1.616 retail per Microsoft guidance.
