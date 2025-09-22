### DXR Reports Index

Use this index to navigate the DXR technique guides and pick the right approach for your feature.

- DXR Acceleration Structures: BLAS/TLAS build, update, compaction — `DXR_Acceleration_Structures_Guide.md`
  - When: any DXR feature. Start here for correct and efficient AS setup.

- Inline Ray Tracing (RayQuery): `DXR_Inline_Ray_Tracing_RayQuery_Guide.md`
  - When: simple, single-hit queries inside raster/compute passes (shadows, AO, probes).

- Ray-Traced Shadows: `DXR_Ray_Traced_Shadows_Guide.md`
  - When: hard/soft shadows for few-to-many lights, ReSTIR DI scaling, NRD SIGMA denoising.

- Ray-Traced Reflections (Hybrid SSR): `DXR_Ray_Traced_Reflections_Guide.md`
  - When: stable reflections at 1 rpp; SSR first with RT fallback; NRD ReLAX/ReBLUR.

- ReSTIR DI/GI: `DXR_ReSTIR_DI_GI_Guide.md`
  - When: many lights/emissives, one shadow ray per pixel with spatio-temporal reuse.

- Denoising (NRD Integration): `DXR_Denoising_NRD_Integration_Guide.md`
  - When: stabilize low-spp shadows/reflections/GI with robust inputs and history.

Notes
- All guides assume D3D12 + DXR 1.0/1.1 (Agility SDK) and include HLSL/C++ snippets.
- Prefer RayQuery for inline visibility; use RTPSO when you need complex hit shading.


