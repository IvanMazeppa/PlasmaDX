# VOL_0004 Companion Guide: Presets + Curl-Advection Baseline

## TL;DR Run
- PowerShell (not WSL):
```
$env:PLASMADX_DISABLE_DXR='1'
$env:PLASMADX_NO_DEBUG='1'
$env:PLASMADX_USE_CURL='1'
$env:PLASMADX_PRESET='PlasmaBox'
$env:PLASMADX_DEBUG_MODE='2'
& .\build-vs2022\Debug\PlasmaDX.exe
```
- Switch `PLASMADX_DEBUG_MODE` between 2 (Bounds), 4 (UVW), 5 (Steps) to validate.

## What to Expect
- Bounds: green/yellow in the volume area; magenta outside.
- UVW: smooth gradients; center ≈ (0.5, 0.5, 0.5).
- Steps: brighter core (more steps), darker edges.
- Normal: evolving tendrils seeded at center, swirling and decaying.

## Tuning (Env Vars)
- `PLASMADX_CURL_SPEED` (default 0.8)
- `PLASMADX_DECAY` (default 0.995)
- `PLASMADX_INJECT_RATE` (default 1.2)
- `PLASMADX_INJECT_RADIUS` (default 0.12)
- `PLASMADX_FLOW_SCALE` (default 1.5)

## Capture/Export/Report (Two-Machine)
- Capture on main PC (script already updated to set sphere OFF by default; curl is separate):
```
pwsh pix\Scripts\capture_all_modes.ps1 -App "build-vs2022\Debug\PlasmaDX.exe"
```
- Export on 24H2 laptop with PIXTool and regenerate report as before.

## Roadmap After Baseline
1) Emission + Beer-Lambert absorption in marcher.
2) Single-scattering lighting with fixed light.
3) Replace curl velocity with particle-driven or SPH when ready.
4) Re-enable DXR shadows once stable.

---
If any mode shows fully magenta (Bounds) or flat Steps, reduce stepSize (0.01) and raise maxSteps (256); verify density SRV is bound and SRV transition occurs after advect.
