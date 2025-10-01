PlasmaDX v0.5.4 - RayQuery Success Backup
Date: 2025-10-01
Branch: 0.5.4 (GitHub)

Major Changes:
- Migrated from DispatchRays (DXR 1.0) to RayQuery (DXR 1.1) inline raytracing
- Removed ~300 lines of RTPSO/SBT/separate command list infrastructure
- Simplified shadow map generation to compute shader on main command list
- Eliminated all fence synchronization complexity
- Zero Map() errors, stable 60fps rendering

Key Files:
- src/core/App.h: Replaced DispatchRays members with RayQuery compute pipeline
- src/core/App.cpp: Rewrote createShadowComputePipeline() and renderShadowMap()
- shaders/mode9/shadow_map_cs.hlsl: New RayQuery compute shader (DXR 1.1)
- shaders/mode9/shadow_map_cs.dxil: Compiled shader

Success Criteria Met:
✅ Rotating triangle shadow visible on particle cloud
✅ Zero Map() failures
✅ Stable frame pacing
✅ DXR 1.1 inline raytracing confirmed working

Next Steps:
- Replace debug triangle BLAS with particle system BLAS for self-shadowing
- Exaggerate shadow effect for verification

