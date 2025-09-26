## Companion Guide: DXR_0017–DXR_0019

Purpose
- Sequence and acceptance checks for enabling a real DXR pipeline, HDR output path, and camera/SBT.

Order of application
1. DXR_0017 – Real DXR state object
   - Implement the raytracing state object (DXIL lib, hit group, shader/pipeline config, global root).
   - Verify `GetPSOProperties()` is non-null and `GetShaderIdentifier()` returns non-null for `RayGen`, `Miss`, `HitGroup`.
   - PIX: Validate a `DispatchRays` occurs and the PSO appears under Pipeline State Objects.

2. DXR_0018 – HDR UAV + Composite
   - Add R16G16B16A16_FLOAT HDR texture with UAV/SRV as size-dependent resource.
   - DXR/raster write to HDR UAV, then composite to swapchain RTV with a fullscreen PSO.
   - Barriers: HDR UAV → SRV before composite; backbuffer RTV → Present after draw.

3. DXR_0019 – Camera + SBT local roots
   - Add camera CBV (b0) with view/proj/inverse and position. Update per frame.
   - Update raygen to compute rays from inverse view-projection.
   - Keep global CBV first; local roots optional for now.

Acceptance checklist
- App runs without `PSO properties are null` warnings.
- SBT records have non-zero identifiers and expected sizes; no debug-layer errors.
- Composite path shows the same (or improved) visuals via HDR→swapchain.
- Moving the camera changes the image.

PIX tips
- Add PIX markers around AS build, PSO creation, SBT build, DXR dispatch, HDR composite.
- In Timing Capture, confirm `ExecuteCommandLists` encloses transitions, ray dispatch, and composite.

Fallbacks and safety
- If PSO creation fails, log HRESULT and keep raster fallback.
- On resize, wait for GPU, release size-dependent resources (backbuffers, HDR), recreate RTVs and HDR.
- Use the per-frame fence to avoid allocator reset errors.




