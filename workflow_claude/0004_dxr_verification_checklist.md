# DXR Verification Checklist

**Date**: September 22, 2025
**Purpose**: Verify DXR ray tracing is active and working correctly

## Quick Test Commands

### Test 1: DXR Enabled (Default)
```batch
cd D:\Users\dilli\AndroidStudioProjects\PlasmaDX
set PLASMADX_NO_DEBUG=1
build-vs2022\Debug\PlasmaDX.exe
```

### Test 2: Force Compute-Only (Comparison)
```batch
cd D:\Users\dilli\AndroidStudioProjects\PlasmaDX
set PLASMADX_NO_DEBUG=1
set PLASMADX_DISABLE_DXR=1
build-vs2022\Debug\PlasmaDX.exe
```

## What to Look For

### In Console Output
✅ **DXR Active**:
```
[INFO] DXR tier 1.1 supported
[INFO] Creating DXR pipeline...
[INFO] PIX: PSO Create Complete
[INFO] DXR dispatch to HDR texture
```

❌ **DXR Disabled/Failed**:
```
[WARN] DXR dispatch skipped: SBT addresses are not set (compute-only fallback)
[INFO] Compute ray marcher dispatch
```

### Visual Differences

| Feature | Compute-Only | DXR Enabled |
|---------|--------------|-------------|
| Shadows | Soft volume self-shadowing | Hard shadows + volume shadows |
| Performance | ~60 FPS | ~45-50 FPS (RT overhead) |
| Quality | Good | Better (hardware acceleration) |
| Lighting | Single directional | Multiple lights possible |

## PIX Capture Verification

### For DXR Path
1. Launch PIX → GPU Capture
2. Look for `DispatchRays` event
3. Check for TLAS in resource list
4. Verify SBT buffer is bound

### For Compute Path
1. Launch PIX → GPU Capture
2. Look for `Dispatch` with ~(120, 68, 1) threads
3. No `DispatchRays` events
4. No TLAS resources

## Log File Analysis

Check `PlasmaDX.log` for:
```
grep "DXR\|DispatchRays\|TLAS\|SBT" PlasmaDX.log
```

Expected output with DXR:
- "DXR tier 1.1 supported"
- "Creating TLAS"
- "SBT created"
- "DispatchRays"

## Performance Metrics

| Mode | Resolution | FPS | GPU % | Memory |
|------|------------|-----|-------|---------|
| Compute | 1920x1080 | 60+ | 70% | 1.2 GB |
| DXR | 1920x1080 | 45-50 | 85% | 1.5 GB |

## Troubleshooting

### Issue: Black Screen with DXR
**Solution**: Check shader compilation
```batch
dxc -T lib_6_3 shaders\dxr\raygen.hlsl -Fo shaders\dxr\raygen.dxil
```

### Issue: Device Lost
**Solution**: Check TLAS build
- Verify geometry is added to BLAS
- Check instance buffer is valid
- Ensure scratch buffer is large enough

### Issue: No Visual Difference
**Solution**: DXR might be falling back
- Check console for "compute-only fallback"
- Verify m_dxrPipeline is created
- Check SBT addresses are non-zero

## Advanced Verification

### Check DXR Components in Code
1. **Pipeline State**: `src/core/App.cpp:1000`
   - m_dxrPipeline should be created

2. **SBT Creation**: Look for m_sbt initialization

3. **TLAS Build**: Search for BuildTLAS() calls

### Environment Variable Override
```batch
REM Force compute path for comparison
set PLASMADX_DISABLE_DXR=1

REM Enable DXR (default now)
set PLASMADX_DISABLE_DXR=

REM Check which path is active
echo %PLASMADX_DISABLE_DXR%
```

## Success Indicators

✅ **DXR is working if you see**:
1. "DXR dispatch to HDR texture" in logs
2. DispatchRays events in PIX
3. Slightly lower FPS (RT overhead)
4. Enhanced shadow quality
5. No fallback warnings

❌ **DXR is NOT working if you see**:
1. "compute-only fallback" warnings
2. Only Dispatch events (no DispatchRays)
3. Same FPS as compute path
4. Missing TLAS/SBT in PIX resources

## Next Steps After Verification

Once DXR is confirmed working:
1. Add multiple light sources
2. Implement reflections
3. Add area lights
4. Enable recursive rays
5. Implement denoising

---

**Quick Check**: Run the app and press F1 to see if it shows "Renderer: DXR" or "Renderer: Compute"