## VOL_0003C – UVW Visualizer + Step Heatmap (Implementation + How to Validate)

This note documents two new debug modes in the ray marcher and a checker toggle in the analytic fill, plus how to validate quickly in PIX.

What changed
- Ray marcher debug modes extended to six:
  - 0: Off
  - 1: RayDir
  - 2: Bounds/AABB
  - 3: DensityProbe (single entry sample)
  - 4: UVW visualizer at entry (rgb = uvw)
  - 5: Step-count heatmap (grayscale = steps / maxSteps)
- Sampler is CLAMP and sampling uses SampleLevel(..., 0) to avoid mip ambiguity during debugging.
- `density_fill_sphere.hlsl`: added optional 3D checker pattern toggle (`kEnableChecker`) to reveal WRAP/repetition vs CLAMP.

Files edited
- `shaders/vol/ray_march_cs.hlsl`: added modes 4 and 5.
- `src/volumetric/RayMarcher.h`: cycle range now 0..5.
- `shaders/vol/density_fill_sphere.hlsl`: optional checker pattern.

How to use
1) Build Debug: `cmake --build build-vs2022b --config Debug`
2) Run PlasmaDX (compute path): ensure `PLASMADX_DISABLE_DXR=1`.
3) Press F4 to cycle modes (6 total). Capture one frame per mode in PIX (v2507.11): Off, RayDir, Bounds, DensityProbe, UVW, Steps.

What to expect (quick read)
- UVW (mode 4):
  - Colors should vary smoothly across the visible volume entry surface.
  - If color repeats in tiles or clamps prematurely, mapping or address mode is wrong.
- Steps (mode 5):
  - Brighter = more distance inside the volume along the ray.
  - If flat uniform or very dark everywhere, the loop/step size is wrong.
- DensityProbe (mode 3):
  - Should show a crisp circle for the analytic sphere baseline.
  - If noisy/banded planes, sampling UVW is wrong or texture addressing is WRAP.

Optional checker density
- In `shaders/vol/density_fill_sphere.hlsl`, set `kEnableChecker = true;` and rebuild.
- You should see a 3D checker on the ray entry if mapping is correct.
  - Tiled/repeating planes → address mode WRAP or out-of-range UVW
  - Smooth, non-repeating pattern → CLAMP + correct normalization

PIX checklist (per capture)
- March dispatch present (threads ≈ screen/16, e.g., 120x68 for 1920x1080).
- CBV1 (`VolumeConstants`) values sane: bounds, stepSize, maxSteps, exposure.
- `g_debugMode` is 0..5 as expected for each capture.
- `g_density` SRV history shows write from density fill dispatch earlier in the frame.
- Barriers: HDR UAV barrier after dispatch; density SRV/UAV transitions around writes/reads.

Next actions based on findings
- If UVW looks wrong or tiles: fix world→UVW mapping and enforce CLAMP.
- If Steps dim/flat: shrink `g_stepSize` (0.005–0.02), increase `g_maxSteps` (256–512).
- If Off looks like DensityProbe: investigate accumulation path (absorption/exposure) or confirm loop increments `t`.

Reference
- Agility SDK setup guidance and known issues (for device init and correct redist layout): https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/



