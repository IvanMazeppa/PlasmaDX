### Companion Guide: DXR_0013..0015 (exception filter, device matrix, minimal baseline)

Scope
- DXR_0013_unhandled_exception_and_stacktrace
- DXR_0014_device_creation_matrix
- DXR_0015_safe_minimal_baseline

Order
1) DXR_0013 → 2) DXR_0014 → 3) DXR_0015

DXR_0013 – Unhandled exception + stack trace
- Install `SetUnhandledExceptionFilter(App::UnhandledExceptionThunk)` early in `App::initialize`.
- On crash you should see:
  - `Unhandled exception: 0xC000001D` (or other code)
  - 5–10 raw return addresses printed (frames)
- If addresses point into `D3D12Core.dll`, suspect Agility/runtime mismatch. If inside our module, we’ll instrument that spot.

DXR_0014 – Device-creation matrix
- Add toggles (env or compile) to flip:
  - Agility on/off
  - Debug layer/factory debug on/off
- For each run, print a one-line summary row:
  - `device {agility=on|off debug=on|off} → {created|failed hr=0x..} tier={NOT_SUPPORTED|1.0|1.1|n/a}`
- Goal: identify which combo triggers illegal instruction.

DXR_0015 – Safe minimal baseline
- CMake option `PLASMADX_MINIMAL_BASELINE=ON`:
  - Skip DXR init (short-circuit after swapchain and RTVs)
  - Render animated clear
- Acceptance: must run indefinitely without crash.

What to capture for the team
- Console logs for each permutation row from 0014
- If a crash reproduces, the 0013 stack frames
- Baseline ON run confirmation (duration > 2 minutes)

Troubleshooting notes
- If only Agility=on crashes, try a different Agility retail (e.g., 1.613) and confirm DLL path is correct (`D3D12/` subdir next to exe) and `D3D12SDKVersion` exports match.
- Ensure VC++ 2015–2022 redistributables are installed (VS installs usually cover this).
- Try Release config once to rule out debug-layer specific crashes.


