# PlasmaDX Volumetric Stability Test Guide

## Test Environment Setup

1. **Build the project**: Use Visual Studio 2022 x64 Debug configuration
2. **Clear previous logs**: Delete contents of `logs/` folder
3. **Ensure PIX is available**: For GPU capture analysis if needed

## Critical Test Cases

### Test 1: Mode 9 - AccretionMeshParticles (PRIMARY TARGET)
```cmd
set PLASMADX_DEBUG_MODE=9
PlasmaDX.exe
```

**Expected Behavior:**
- ✅ Application starts without crash
- ✅ Log shows "Demo Mode: Accretion Mesh Particles"
- ✅ Log shows "Volumetric systems initialized successfully"
- ✅ Log shows "App initialized, starting run loop..."
- ✅ **NO** access violation 0xC0000005
- ✅ Either renders mesh particles + volumetric lighting OR shows clear error messages

### Test 2: Mode 5 - PlasmaAccretion
```cmd
set PLASMADX_DEBUG_MODE=5
PlasmaDX.exe
```

**Expected Behavior:**
- ✅ Application starts without crash
- ✅ Plasma accretion rendering OR graceful degradation with error logs

### Test 3: Mode 6 - VoxelParticles
```cmd
set PLASMADX_DEBUG_MODE=6
PlasmaDX.exe
```

**Expected Behavior:**
- ✅ Application starts without crash
- ✅ Voxel particle rendering OR graceful degradation with error logs

### Test 4: Regression Test - Mode 2 - TorchlightDemo
```cmd
set PLASMADX_DEBUG_MODE=2
PlasmaDX.exe
```

**Expected Behavior:**
- ✅ Application starts without crash (baseline validation)
- ✅ Interactive torchlight demo continues to work

## Error Pattern Analysis

### GOOD - Graceful Degradation Logs
Look for these patterns in logs:
```
[WARN] Density volume resource not available for curl advection
[WARN] DXR: Density volume not available, falling back to TLAS only
[ERROR] Volumetric system error: [specific error] - skipping volumetric rendering this frame
[INFO] Volumetric systems not available - using basic DXR mode
```

### BAD - Critical Failure Patterns
These indicate the fix didn't work:
```
=== UNHANDLED EXCEPTION: 0xC0000005 ===
Exception address: [any address]
Access violation
```

### CRITICAL - Resource Validation Logs
These indicate the fix is working:
```
[ERROR] DXR: Descriptor allocator is null - cannot bind SRV table
[ERROR] DXR: Invalid SRV table handle - cannot bind descriptors
[ERROR] DXR: HDR texture is null - cannot proceed with DXR
[ERROR] Critical DXR resources null - aborting DispatchRays
```

## Advanced Diagnostics

### Environment Variables for Testing
```cmd
# Test curl-advection path
set PLASMADX_USE_CURL=1

# Test metaball lava lamp path
set PLASMADX_USE_CURL=0
set PLASMADX_LAVA_LAMP=1

# Disable DXR for compute-only testing
set PLASMADX_DISABLE_DXR=1

# Test different volume presets
set PLASMADX_VOLUME_PRESET=Small
set PLASMADX_VOLUME_PRESET=Medium
set PLASMADX_VOLUME_PRESET=Large
```

### Log File Analysis
Check latest log file in `logs/plasmadx_*.log`:

1. **Initialization Success**: Look for consecutive descriptor allocation:
   ```
   [INFO] Allocated consecutive SRV descriptors: TLAS=8, DensityVolume=9
   [INFO] TLAS SRV recreated at consecutive descriptor location
   [INFO] Density volume SRV created at consecutive descriptor location
   ```

2. **Render Loop Entry**: Should see smooth transition:
   ```
   [INFO] App initialized, starting run loop...
   [INFO] DXR: Starting DispatchRays with valid SBT addresses
   ```

3. **Error Recovery**: Look for graceful error handling:
   ```
   [ERROR] [Specific volumetric error] - skipping volumetric rendering this frame
   [INFO] DXR: Disabled this frame (Lava Lamp mode active)
   ```

## Success Criteria

### ✅ PASS Conditions
- All modes start without access violations
- Clear, informative error messages for resource issues
- Graceful degradation to basic DXR when volumetric fails
- Mode 9 can attempt mesh particle + volumetric integration
- Existing DXR modes continue working

### ❌ FAIL Conditions
- Any access violation 0xC0000005 crashes
- Silent failures with no error messages
- Regression in basic DXR modes (1,2,8)
- Complete loss of volumetric functionality

## Next Steps After Testing

### If Tests PASS
1. Proceed with volumetric feature development
2. Implement Mode 9 mesh shader particle rendering
3. Optimize volumetric DXR ray traced lighting integration

### If Tests FAIL
1. Check logs for specific error patterns
2. Revert to previous App.cpp version using git
3. Debug incrementally with simpler volumetric configurations
4. Consider additional resource validation layers

## Performance Testing (Optional)
```cmd
# Test with PIX GPU capture
set PLASMADX_DEBUG_MODE=9
# Launch PlasmaDX.exe and take GPU capture
# Analyze resource binding, barriers, and DispatchRays timing
```

This validation ensures the volumetric architecture is stable before implementing advanced features like NASA-quality accretion disk rendering with 100K mesh shader particles.