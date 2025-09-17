## STATUS_ILLEGAL_INSTRUCTION (0xC000001D) — PlasmaDX Diagnostics and Plan

What we did now
- Added an unhandled exception filter with mini stack logging (DXR_0013)
- Enabled D3D12/DXGI InfoQueue, DRED checks, and wrapped Execute/Present HRESULTs (DXR_0011)
- Prepared a device-creation matrix to bisect Agility/debug influence (DXR_0014)
- Added a safe minimal baseline mode to validate swapchain path (DXR_0015)

Likely causes
- Agility DLL/runtime mismatch or early CPU feature dispatch in D3D12Core.dll
- Debug-layer interaction during device creation
- Corrupt DXIL or invalid PSO usage (less likely if crash happens pre-swapchain)

Immediate steps
1) Run baseline mode (0015) for 2+ minutes: proves Win32 + DXGI + swapchain are stable.
2) Run device matrix (0014): collect rows for {Agility on/off} x {Debug on/off}.
3) If only Agility=on crashes: try Agility 1.613 retail or disable temporarily.
4) If crash persists without Agility: try Release build; verify VC++ redist.

Answers to the prompt
- CPU ISA: Ryzen 5950X supports AVX/AVX2; Agility should work. Illegal instruction implies bad dispatch path or wrong DLL loaded.
- Runtime deps: VC++ 2015–2022 redist; Windows 10/11 up-to-date; no .NET required.
- DLL load: Use our filter and Process Monitor to confirm which DLL faults; ensure `D3D12Core.dll` is from Agility `D3D12/` folder.
- Memory/ASLR: Unlikely root cause; still try Release config.
- Tools: WinDbg (attach, `!analyze -v`), ProcMon (stack of faulting thread), PIX GPU capture post-fix.

Output to share
- Console log with exception code + stack frames
- Device matrix rows
- Baseline ON run confirmation


