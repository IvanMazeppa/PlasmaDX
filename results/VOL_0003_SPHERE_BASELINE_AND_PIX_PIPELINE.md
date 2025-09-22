# VOL_0003B/C: Analytic Sphere Baseline + PIX Two‑Machine Workflow

This document brings Claude up to speed on the current debugging workflow and how to generate actionable data for the volumetric ray marcher.

## Runtime Switches
- PLASMADX_DISABLE_DXR=1 (compute path)
- PLASMADX_NO_DEBUG=1 (no debug layer)
- PLASMADX_FILL_SPHERE=1 (enable analytic UVW sphere baseline)
- PLASMADX_DEBUG_MODE={2|4|5} (Bounds, UVW, Steps)

## Capture Steps (Main PC)
1. Launch PlasmaDX with the env above for each mode (2,4,5).
2. Save captures to `pix/Captures/mode_*.wpix` (automation script available).

CLI (all modes):
```
pwsh pix\Scripts\capture_all_modes.ps1 -App "build-vs2022\Debug\PlasmaDX.exe"
```

## Export Steps (24H2 Laptop)
1. For each `.wpix` in `\\B3\cursor_default\pix\Captures`, export CSV + PNG to `\\B3\cursor_default\pix\Reports` using PIXTool.

Example:
```
$pix = 'C:\\Program Files\\Microsoft PIX\\2507.11\\pixtool.exe'
Get-ChildItem \\B3\cursor_default\pix\Captures -Filter *.wpix | ForEach-Object {
  $cap = $_.FullName; $name = [IO.Path]::GetFileNameWithoutExtension($cap)
  & $pix open-capture $cap `
      save-event-list "\\B3\cursor_default\pix\Reports\$name.csv" --counters=* `
      save-screenshot "\\B3\cursor_default\pix\Reports\$name.png"
}
```

## Report Steps (Main PC)
Generate consolidated markdown:
```
pix\Scripts\process_event_csv.ps1 -CsvDir "\\B3\cursor_default\pix\Reports" -Output "\\B3\cursor_default\pix\workflow_claude\pix_analysis_report.md"
```

## What to Look For
- Bounds (2): Green/Yellow over sphere, Magenta outside → confirms AABB hits.
- UVW (4): Center pixels near (0.5,0.5,0.5) at entry; smooth variation. Clamp issues show as banding at 0/1.
- Steps (5): Higher steps through center, fewer at edges. Flat map indicates wrong stepSize/termination.

## Code Touchpoints
- `src/core/App.cpp`: env‑gated `FillAnalyticSphere` + `TransitionToSRV` before march.
- `src/volumetric/DensityVolume.cpp`: `FillAnalyticSphere` dispatch, UAV barrier; `TransitionToSRV/ToUAV` helpers.
- `src/volumetric/RayMarcher.cpp`: `VolumeConstants.debugMode`, dispatch dims = ceil(width/16) × ceil(height/16).

## Next Small Tasks
- Switch to `pix3.h` markers for cleaner event names in exports.
- Tune `stepSize` (0.01) and `maxSteps` (256) to confirm coverage.

Status: Two‑machine PIX flow validated. Sphere baseline enabled at runtime.


