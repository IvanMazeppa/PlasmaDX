# Companion Guide — VOL_0002 → VOL_0005 (2025-09-19)

## Execution Order (do not reorder)
1) VOL_0002: 3D density grid + debug slice
2) VOL_0003: Compute ray marcher (directional light + self-shadow)
3) VOL_0004: Temporal accumulation + jitter hooks
4) VOL_0005: Empty-space skipping
5) DXR_0025: Re-enable DXR once compute parity achieved

## Rationale
- We already have reliable HDR + composite + compute pipeline. Building density and a compute marcher first gives immediate visuals and measurable progress without DXR complexity.

## Validation per step
- VOL_0002: PIX shows density UAV writes; debug slice visible; log heartbeat every ~60 frames.
- VOL_0003: Visible volumetric lighting and shadowing; controls affect image; no device removal.
- VOL_0004: Image stabilizes over time when accumulation is on; history reset works.
- VOL_0005: Average steps per pixel drops; visual diffs minimal when skipping is enabled.

## Notes
- Keep DXR disabled via PLASMADX_DISABLE_DXR=1 until DXR_0025.
- Ensure proper UAV→SRV transitions and UAV barriers before sampling or composite.
- Use presets (96/128/192/256) to manage perf during bring-up.
